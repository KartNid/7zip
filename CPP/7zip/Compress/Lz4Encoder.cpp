// Lz4Encoder.cpp

#include "StdAfx.h"

#include <string.h>

#include "../../../C/Alloc.h"
#include "../../../C/Lz4/lz4frame.h"

#include "../../Common/ComTry.h"

#include "../Common/StreamUtils.h"

#include "Lz4Encoder.h"

namespace NCompress {
namespace NLz4 {

static const UInt32 kInBufSize = 1 << 16;

CEncoder::~CEncoder()
{
  MidFree(_inBuf);
  MidFree(_outBuf);
}

Z7_COM7F_IMF(CEncoder::SetCoderProperties(const PROPID *propIDs, const PROPVARIANT *props, UInt32 numProps))
{
  for (UInt32 i = 0; i < numProps; i++)
  {
    if (propIDs[i] == NCoderPropID::kLevel)
    {
      if (props[i].vt != VT_UI4)
        return E_INVALIDARG;
      UInt32 level = props[i].ulVal;
      if (level > 12)
        level = 12;
      _level = level;
    }
  }
  return S_OK;
}

Z7_COM7F_IMF(CEncoder::WriteCoderProperties(ISequentialOutStream *outStream))
{
  // 1 reserved byte (version/flags)
  Byte prop = 1;
  return WriteStream(outStream, &prop, 1);
}

Z7_COM7F_IMF(CEncoder::Code(ISequentialInStream *inStream, ISequentialOutStream *outStream,
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
    // frame bound for one update chunk
    _outBufSize = (UInt32)LZ4F_compressFrameBound(kInBufSize, NULL) + 64;
    _outBuf = (Byte *)MidAlloc(_outBufSize);
    if (!_inBuf || !_outBuf)
    {
      MidFree(_inBuf); _inBuf = NULL;
      MidFree(_outBuf); _outBuf = NULL;
      return E_OUTOFMEMORY;
    }
    _inBufSize = kInBufSize;
  }

  LZ4F_cctx *cctx = NULL;
  {
    LZ4F_errorCode_t err = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
    if (LZ4F_isError(err) || !cctx)
      return E_FAIL;
  }

  HRESULT result = S_OK;
  UInt64 nextProgress = (UInt64)1 << 22;

  {
    LZ4F_preferences_t prefs;
    memset(&prefs, 0, sizeof(prefs));
    prefs.frameInfo.contentSize = 0;
    prefs.frameInfo.blockMode = LZ4F_blockLinked;
    prefs.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    prefs.compressionLevel = (int)_level;
    prefs.autoFlush = 0;

    size_t cap = _outBufSize;
    size_t w = LZ4F_compressBegin(cctx, _outBuf, cap, &prefs);
    if (LZ4F_isError(w))
    {
      result = E_FAIL;
      goto exit;
    }
    result = WriteStream(outStream, _outBuf, w);
    if (result != S_OK)
      goto exit;
    _outProcessed += w;
  }

  for (;;)
  {
    UInt32 inSize = 0;
    result = inStream->Read(_inBuf, kInBufSize, &inSize);
    if (result != S_OK)
      goto exit;
    if (inSize == 0)
      break;
    _inProcessed += inSize;

    {
      size_t cap = _outBufSize;
      size_t w = LZ4F_compressUpdate(cctx, _outBuf, cap, _inBuf, inSize, NULL);
      if (LZ4F_isError(w))
      {
        result = E_FAIL;
        goto exit;
      }
      if (w)
      {
        result = WriteStream(outStream, _outBuf, w);
        if (result != S_OK)
          goto exit;
        _outProcessed += w;
      }
    }

    if (progress && _inProcessed >= nextProgress)
    {
      nextProgress = _inProcessed + ((UInt64)1 << 22);
      result = progress->SetRatioInfo(&_inProcessed, &_outProcessed);
      if (result != S_OK)
        goto exit;
    }
  }

  {
    size_t cap = _outBufSize;
    size_t w = LZ4F_compressEnd(cctx, _outBuf, cap, NULL);
    if (LZ4F_isError(w))
    {
      result = E_FAIL;
      goto exit;
    }
    if (w)
    {
      result = WriteStream(outStream, _outBuf, w);
      if (result != S_OK)
        goto exit;
      _outProcessed += w;
    }
  }

exit:
  LZ4F_freeCompressionContext(cctx);
  return result;

  COM_TRY_END
}

}}
