---
feature: lz4-support
status: delivered
updated: 2026-10-15
branch: feature/lz4-support
commits: 5c0bc32..97478ba
---

# 7-Zip LZ4 Compression / Decompression Support

## Report

**What was built** — 7-Zip now compresses and decompresses LZ4 in two ways. (1) Standalone `.lz4` frame files: `Lz4Handler` recognizes magic `04 22 4D 18`, presents a single-item archive, and round-trips via the vendored lz4 1.10.0 frame API (`lz4`/`lz4hc`/`lz4frame`/`xxhash` under `C/Lz4/`). (2) 7z compression method `LZ4` with ID `0x04F71104` (the `DOC/Methods.txt` external-codec slot): `Lz4Encoder`/`Lz4Decoder` registered through `REGISTER_CODEC_2`, so `7zz a -m0=LZ4` works. Both paths use the LZ4F frame format as the coded stream (amended from the original "raw block" wording — frame format is the only practical self-delimiting streaming option and matches the `.lz4` handler). `7zHeader.h` gained `k_LZ4`/`k_ZSTD`; `7zHandlerOut.cpp` assigns a 64 KiB dictionary size for solid-size tuning. Full x64 suite builds with MSVC nmake: `7zz.exe`, `7z.exe`, `7z.dll`, `7zFM.exe`.

**Verification** — `nmake` for Alone2/Alone/Format7zF/Fm: PASS. `7zz i` lists format `lz4` and method `4F71104 LZ4`: PASS. `.lz4` create/list/test/extract SHA256 round-trip (1.1 KB text, 270 KB compressible text, 256 KB random): PASS. 7z `-m0=LZ4` create/extract SHA256 round-trip: PASS. Corrupt mid-stream byte → exit 2: PASS. Truncated frames (magic-only, cut tail) → exit 2 after fix: PASS. Independent review found two critical bugs (truncation accepted as success; LZ4F context leak on `RINOK`); both fixed in `97478ba` and re-review PASS.

**Journey log** —
1. nmake expands `$(OBJS)` at rule-parse time: object lists must be appended (via `Lz4.mak`) *before* `Build.mak` is included, while compile rules (`Lz4Rules.mak`) must come *after* `$O`/`CFLAGS` exist. Splitting the two files was required.
2. Absolute `O=` paths blow the nmake command-line limit; use the default short `x64/` output dir.
3. `Z7_CLASS_IMP_COM_*` places interface methods in the private section after `Z7_COM_ADDREF_RELEASE`; call them via `ClsPtr()`/`Interface()` or add public helpers — not via the concrete pointer.
4. `API_FUNC_static_IsArc` opens an `extern "C" {` that needs a matching close-brace after the function.
5. LZ4F `res>0` at input EOF is not an error; `FinishMode` must be enforced explicitly or truncation is silent success.

## [S1] Problem

Upstream 7-Zip (ip7z/7zip) has no LZ4 codec. `DOC/Methods.txt` already reserves
method ID `04 F7 11 04` for LZ4 under external codecs, but no encoder, decoder,
or archive handler is implemented. Users cannot compress with LZ4 or open
standard `.lz4` frame files.

Goal: add full LZ4 compress/decompress to a built 7-Zip application suite —
both as a 7z compression method and as a standalone `.lz4` frame format.

## [S2] Design

### A. Vendored LZ4 core

Embed the official lz4 library (v1.10.0, BSD-2-Clause / public domain dual
license) under `C/Lz4/` with `LICENSE-lz4.txt`:

| File | Role |
|---|---|
| `lz4.c` / `lz4.h` | block compress / decompress |
| `lz4hc.c` / `lz4hc.h` | high-compression block encoder |
| `lz4frame.c` / `lz4frame.h` | LZ4 frame format (`.lz4` files) |
| `xxhash.c` / `xxhash.h` | frame content checksum (required by lz4frame) |

Compile units: `$O\lz4.obj`, `$O\lz4hc.obj`, `$O\lz4frame.obj`, `$O\xxhash.obj`.
`CPP/7zip/Lz4.mak` appends them to `C_OBJS` (included early, like `Aes.mak`).
`CPP/7zip/Lz4Rules.mak` holds the explicit compile rules with relaxed warnings
(`-W1 -WX-`) and is included from `7zip.mak` after `Build.mak`.

### B. C++ codec layer (7z method)

Method ID: `0x04F71104` (bytes `04 F7 11 04`, from `DOC/Methods.txt`
external codecs / Tino Reichardt / 04 - LZ4). Name string: `"LZ4"`.

Coded stream is the **LZ4F frame format** (amended; original draft said raw
block — frame format is self-delimiting and streaming-safe, and is what
`Lz4Handler` produces).

- `CPP/7zip/Compress/Lz4Decoder.h|.cpp` — `NCompress::NLz4::CDecoder`
  implementing `ICompressCoder`, `ICompressSetDecoderProperties2`,
  `ICompressSetFinishMode`, `ICompressGetInStreamProcessedSize`.
  Streaming `LZ4F_decompress`. **FinishMode contract**: at input EOF, if a
  frame is still open and `FinishMode` is set, return `S_FALSE` (truncated).
  All paths after `LZ4F_createDecompressionContext` free the context via
  `goto exit`.
- `CPP/7zip/Compress/Lz4Encoder.h|.cpp` — `NCompress::NLz4::CEncoder`
  implementing `ICompressCoder`, `ICompressSetCoderProperties`,
  `ICompressWriteCoderProperties`. Streaming `LZ4F_compressBegin/Update/End`
  with content checksum enabled. Level 1–12 mapped to `compressionLevel`.
  Same context-cleanup contract as the decoder.
- `CPP/7zip/Compress/Lz4Register.cpp` — `REGISTER_CODEC_2(Lz4, CreateDec,
  CreateEnc, 0x04F71104, "LZ4")`.

### C. Standalone `.lz4` archive handler

`CPP/7zip/Archive/Lz4Handler.cpp` — `NArchive::NLz4::CHandler`:

- Magic: `04 22 4D 18` (little-endian `0x184D2204`), signature size 4.
- Implements `IInArchive`, `IArchiveOpenSeq`, `ISetProperties`, `IOutArchive`.
- Single-item archive (`KeepName`, like `Bz2Handler`/`ZstdHandler`).
- Decode path uses `Lz4Decoder` with `FinishMode=true`; encode path uses
  `Lz4Encoder`.
- Registration: `REGISTER_ARC_IO("lz4", "lz4 lz4l", "* .tar", 'l',
  k_Signature, 0, NArcInfoFlags::kKeepName, 0, IsArc_Lz4)`.

### D. 7z format integration

- `CPP/7zip/Archive/7z/7zHeader.h`: `const UInt32 k_LZ4 = 0x04F71104;`
  (and `k_ZSTD = 0x04F71101` for the reserved external slot).
- `CPP/7zip/Archive/7z/7zHandlerOut.cpp`: `case k_LZ4: dicSize = 1 << 16`.
- Method name display falls through `AddMethodName` / `FindMethod` to the
  registered codec name `"LZ4"`.

### E. Build system

NMake + MSVC. Objects wired via `Format7zF/Arc.mak` (Alone2, Format7zF, Fm)
and `Alone/makefile`. Delivered x64 artifacts:

| Artifact | Bundle |
|---|---|
| `7zz.exe` | `Bundles/Alone2` |
| `7z.exe` | `Bundles/Alone` |
| `7z.dll` | `Bundles/Format7zF` |
| `7zFM.exe` | `Bundles/Fm` |

### F. Contracts

- Interop: `.lz4` files produced by our encoder are standard LZ4F frames
  (decompressible by reference `lz4` tooling).
- 7z method round-trip: `7zz a -m0=LZ4 t.7z file` then `7zz x t.7z` restores
  byte-identical content.
- Error behavior: corrupt or **truncated** stream → `S_FALSE` / `kDataError`;
  never crash, never read past buffer. LZ4F contexts always freed.

## [S3] Out of Scope

- LZ5 / Lizard / other Tino Reichardt codecs.
- ZIP method slot for LZ4 (ZIP has no standard LZ4 method).
- Legacy `.lz4` block-only files without frame header.
- Dictionary compression (`LZ4F` with external dict).
- Changing upstream 7-Zip version strings, license texts, or installer.
- `Format7zExtract*` reduced bundles (not wired; full suite covers the request).

## Tasks

- [x] T1: Vendor lz4 1.10.0 into `C/Lz4/` and wire into makefiles — acceptance: sources present, `nmake` compiles `lz4`/`lz4hc`/`lz4frame`/`xxhash` objs without error (covers: S2-A)
- [x] T2: Implement `Lz4Decoder` + `Lz4Encoder` + `Lz4Register` — acceptance: code compiles into Alone2; `7zz -i` lists method `LZ4` (covers: S2-B; depends: T1)
- [x] T3: Implement `Lz4Handler` for `.lz4` frame files — acceptance: `7zz l sample.lz4` shows one item; extract matches original (covers: S2-C; depends: T1)
- [x] T4: Integrate LZ4 into 7z method map — acceptance: `7zz a -m0=LZ4 a.7z f` + extract round-trips (covers: S2-D; depends: T2)
- [x] T5: Build full suite with MSVC — acceptance: `7zz.exe`, `7z.exe`, `7z.dll`, `7zFM.exe` produced in build output (covers: S2-E; depends: T2, T3, T4)
- [x] T6: End-to-end verification — acceptance: round-trip tests, truncation/corruption error paths all pass (covers: S2-F; depends: T5)
