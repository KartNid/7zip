"""Exercise the release binaries with real archives on Windows and Linux."""

import pathlib
import random
import struct
import subprocess
import sys
import tempfile


def main():
    binary = str(pathlib.Path(sys.argv[1]).resolve())

    def run(args, cwd, success=True, stdin=None):
        result = subprocess.run(
            [binary, *args], cwd=cwd, input=stdin,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
        if (result.returncode == 0) != success:
            raise AssertionError((args, result.returncode,
                                  result.stdout.decode(errors="replace"),
                                  result.stderr.decode(errors="replace")))
        return result.stdout

    with tempfile.TemporaryDirectory(prefix="7zip-lz4-") as tmp:
        root = pathlib.Path(tmp)
        info = run(["i"], root)
        assert b"lz4" in info and b"LZ4" in info
        rng = random.Random(7)
        samples = [b"", b"hello LZ4\n", b"7-Zip LZ4\n" * 700000,
                   rng.randbytes(1024 * 1024 + 13)]
        for index, data in enumerate(samples):
            source = root / f"sample-{index}.bin"
            source.write_bytes(data)
            for fmt, method in (("lz4", []), ("7z", ["-m0=LZ4"]),
                                ("7z", ["-m0=LZ4", "-mx=9"])):
                archive = root / f"sample-{index}-{len(method)}.{fmt}"
                run(["a", f"-t{fmt}", str(archive), source.name,
                     *method, "-y"], root)
                run(["t", str(archive)], root)
                assert run(["x", str(archive), "-so"], root) == data
            frame = (root / f"sample-{index}-0.lz4").read_bytes()
            assert frame[:4] == bytes.fromhex("04224d18")
            if index == 2:
                good_frame = frame
            print(f"PASS sample {index}: standalone LZ4 and 7z/LZ4 round trips")

        # Consecutive frames, every skippable signature, and stream input.
        for magic in range(0x184D2A50, 0x184D2A60):
            skipped = struct.pack("<II", magic, 5) + b"skip!"
            data = skipped + good_frame + skipped + good_frame
            archive = root / "skippable.lz4"
            archive.write_bytes(data)
            run(["t", str(archive)], root)
            assert run(["x", str(archive), "-so"], root) == samples[2] * 2
        assert run(["x", "-tlz4", "-si", "-so"], root,
                   stdin=good_frame) == samples[2]
        print("PASS concatenation, 16 skippable signatures, and stdin")

        bad = root / "bad.lz4"
        for cut in (4, 7, len(good_frame) // 2, len(good_frame) - 1):
            bad.write_bytes(good_frame[:cut])
            run(["t", str(bad)], root, success=False)
        corrupt = bytearray(good_frame)
        corrupt[-1] ^= 0x80  # content checksum
        bad.write_bytes(corrupt)
        run(["t", str(bad)], root, success=False)
        print("PASS truncated frames and corrupt checksum are rejected")

        # Existing formats and encrypted headers remain usable.
        source = root / "sample-3.bin"
        for fmt in ("7z", "zip", "tar", "gzip", "xz", "bzip2"):
            archive = root / f"regression.{fmt}"
            run(["a", f"-t{fmt}", str(archive), source.name, "-y"], root)
            run(["t", str(archive)], root)
            assert run(["x", str(archive), "-so"], root) == samples[3]
        # Public test password; no credentials are involved.
        password = "-prelease-smoke-test"
        archive = root / "encrypted.7z"
        run(["a", str(archive), source.name, password, "-mhe=on"], root)
        assert run(["x", str(archive), "-so", password], root) == samples[3]
        run(["t", str(archive), "-pwrong-test-password"], root, success=False)
        print("PASS six existing formats, encrypted 7z, and wrong password")

        if "--reference" in sys.argv:
            import lz4.frame
            assert lz4.frame.decompress(good_frame) == samples[2]
            for size in (65536, 4 * 1024 * 1024):
                for linked in (True, False):
                    frame = lz4.frame.compress(
                        samples[2], block_size=(lz4.frame.BLOCKSIZE_MAX64KB
                        if size == 65536 else lz4.frame.BLOCKSIZE_MAX4MB),
                        block_linked=linked, block_checksum=True,
                        content_checksum=True)
                    archive = root / "reference.lz4"
                    archive.write_bytes(frame)
                    assert run(["x", str(archive), "-so"], root) == samples[2]
            print("PASS independent LZ4 library interoperability in both directions")
    print("PASS all release smoke checks")


if __name__ == "__main__":
    main()
