// Lz4Decoder.cpp

#include "StdAfx.h"

#include "../../../C/Alloc.h"
#include "../../../C/Lz4/lz4frame.h"

#include "../../Common/ComTry.h"

#include "../Common/StreamUtils.h"

#include "Lz4Decoder.h"

namespace NCompress {
namespace NLz4 {

static const UInt32 kInBufSize = 1 << 16;
static const UInt32 kOutBufSize = 1 << 16;

CDecoder::~CDecoder()
{
  MidFree(_inBuf);
  MidFree(_outBuf);
}

Z7_COM7F_IMF(CDecoder::SetDecoderProperties2(const Byte * /* data */, UInt32 /* size */))
{
  return S_OK;
}

Z7_COM7F_IMF(CDecoder::SetFinishMode(UInt32 finishMode))
{
  FinishMode = (finishMode != 0);
  return S_OK;
}

Z7_COM7F_IMF(CDecoder::GetInStreamProcessedSize(UInt64 *value))
{
  *value = _inProcessed;
  return S_OK;
}

Z7_COM7F_IMF(CDecoder::Code(ISequentialInStream *inStream, ISequentialOutStream *outStream,
    const UInt64 * /* inSize */, const UInt64 * /* outSize */, ICompressProgressInfo *progress))
{
  COM_TRY_BEGIN

  _inProcessed = 0;
  _outProcessed = 0;

  if (!_inBuf || !_outBuf)
  {
    MidFree(_inBuf); _inBuf = NULL;
    MidFree(_outBuf); _outBuf = NULL;
    _inBuf = (Byte *)MidAlloc(kInBufSize);
    _outBuf = (Byte *)MidAlloc(kOutBufSize);
    if (!_inBuf || !_outBuf)
    {
      MidFree(_inBuf); _inBuf = NULL;
      MidFree(_outBuf); _outBuf = NULL;
      return E_OUTOFMEMORY;
    }
    _inBufSize = kInBufSize;
    _outBufSize = kOutBufSize;
  }

  LZ4F_dctx *dctx = NULL;
  {
    LZ4F_errorCode_t err = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    if (LZ4F_isError(err) || !dctx)
      return E_FAIL;
  }

  HRESULT result = S_OK;
  UInt64 nextProgress = (UInt64)1 << 22;
  bool frameNeedsMoreInput = false;

  for (;;)
  {
    UInt32 inSize = 0;
    result = inStream->Read(_inBuf, kInBufSize, &inSize);
    if (result != S_OK)
      goto exit;
    _inProcessed += inSize;

    if (inSize == 0)
    {
      // input exhausted
      if (FinishMode && frameNeedsMoreInput)
        result = S_FALSE; // truncated frame
      break;
    }

    {
      const Byte *src = _inBuf;
      size_t srcRemaining = inSize;

      while (srcRemaining != 0)
      {
        size_t dstCapacity = kOutBufSize;
        size_t srcChunk = srcRemaining;
        size_t res = LZ4F_decompress(dctx, _outBuf, &dstCapacity, src, &srcChunk, NULL);
        if (LZ4F_isError(res))
        {
          result = S_FALSE;
          goto exit;
        }
        src += srcChunk;
        srcRemaining -= srcChunk;
        _outProcessed += dstCapacity;

        if (dstCapacity)
        {
          result = WriteStream(outStream, _outBuf, dstCapacity);
          if (result != S_OK)
            goto exit;
        }

        if (res == 0)
        {
          // frame finished
          frameNeedsMoreInput = false;
          if (srcRemaining == 0)
            break;
          continue;
        }

        // res > 0: needs more input to finish this frame
        frameNeedsMoreInput = true;
        if (srcRemaining == 0)
          break;
      }
    }

    if (progress && _outProcessed >= nextProgress)
    {
      nextProgress = _outProcessed + ((UInt64)1 << 22);
      result = progress->SetRatioInfo(&_inProcessed, &_outProcessed);
      if (result != S_OK)
        goto exit;
    }
  }

exit:
  LZ4F_freeDecompressionContext(dctx);
  return result;

  COM_TRY_END
}

}}
