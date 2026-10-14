// Lz4Handler.cpp -- standalone .lz4 frame format

#include "StdAfx.h"

#include "../../../C/Lz4/lz4frame.h"

#include "../../Common/ComTry.h"

#include "../Common/ProgressUtils.h"
#include "../Common/RegisterArc.h"
#include "../Common/StreamUtils.h"

#include "../Compress/CopyCoder.h"
#include "../Compress/Lz4Decoder.h"
#include "../Compress/Lz4Encoder.h"

#include "Common/DummyOutStream.h"
#include "Common/HandlerOut.h"

using namespace NWindows;

namespace NArchive {
namespace NLz4 {

Z7_CLASS_IMP_CHandler_IInArchive_3(
  IArchiveOpenSeq,
  IOutArchive,
  ISetProperties
)
  CMyComPtr<IInStream> _stream;
  CMyComPtr<ISequentialInStream> _seqStream;

  bool _isArc;
  bool _needSeekToStart;

  bool _packSize_Defined;
  bool _unpackSize_Defined;

  UInt64 _packSize;
  UInt64 _unpackSize;

  CSingleMethodProps _props;
};

static const Byte kProps[] =
{
  kpidSize,
  kpidPackSize
};

static const Byte kArcProps[] =
{
  kpidPhySize
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPhySize: if (_packSize_Defined) prop = _packSize; break;
    case kpidUnpackSize: if (_unpackSize_Defined) prop = _unpackSize; break;
    case kpidErrorFlags:
    {
      UInt32 v = 0;
      if (!_isArc) v |= kpv_ErrorFlags_IsNotArc;
      prop = v;
      break;
    }
    default: break;
  }
  prop.Detach(value);
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = 1;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 /* index */, PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPackSize: if (_packSize_Defined) prop = _packSize; break;
    case kpidSize: if (_unpackSize_Defined) prop = _unpackSize; break;
    default: break;
  }
  prop.Detach(value);
  return S_OK;
}

static const Byte kLz4Magic[4] = { 0x04, 0x22, 0x4D, 0x18 };

API_FUNC_static_IsArc IsArc_Lz4(const Byte *p, size_t size)
{
  if (size < 4)
    return k_IsArc_Res_NEED_MORE;
  for (unsigned i = 0; i < 4; i++)
    if (p[i] != kLz4Magic[i])
      return k_IsArc_Res_NO;
  return k_IsArc_Res_YES;
}
}

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 *, IArchiveOpenCallback *))
{
  COM_TRY_BEGIN
  Close();
  {
    Byte buf[4];
    RINOK(ReadStream_FALSE(stream, buf, 4))
    if (IsArc_Lz4(buf, 4) == k_IsArc_Res_NO)
      return S_FALSE;
    _isArc = true;
    _stream = stream;
    _seqStream = stream;
    _needSeekToStart = true;
  }
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::OpenSeq(ISequentialInStream *stream))
{
  Close();
  _isArc = true;
  _seqStream = stream;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Close())
{
  _isArc = false;
  _needSeekToStart = false;
  _packSize_Defined = false;
  _unpackSize_Defined = false;
  _packSize = 0;
  _unpackSize = 0;
  _seqStream.Release();
  _stream.Release();
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback))
{
  COM_TRY_BEGIN
  if (numItems == 0)
    return S_OK;
  if (numItems != (UInt32)(Int32)-1 && (numItems != 1 || indices[0] != 0))
    return E_INVALIDARG;

  if (_packSize_Defined)
    RINOK(extractCallback->SetTotal(_packSize))

  Int32 opRes;
  {
    CMyComPtr<ISequentialOutStream> realOutStream;
    const Int32 askMode = testMode ?
        NExtract::NAskMode::kTest :
        NExtract::NAskMode::kExtract;
    RINOK(extractCallback->GetStream(0, &realOutStream, askMode))
    if (!testMode && !realOutStream)
      return S_OK;

    RINOK(extractCallback->PrepareOperation(askMode))

    if (_needSeekToStart)
    {
      if (!_stream)
        return E_FAIL;
      RINOK(InStream_SeekToBegin(_stream))
    }
    else
      _needSeekToStart = true;

    CMyComPtr2_Create<ICompressCoder, NCompress::NLz4::CDecoder> decoder;

    CMyComPtr2_Create<ISequentialOutStream, CDummyOutStream> outStream;
    outStream->SetStream(realOutStream);
    outStream->Init();

    CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
    lps->Init(extractCallback, true);

    decoder->FinishMode = true;

    HRESULT result = decoder.Interface()->Code(_seqStream, outStream, NULL, NULL, lps);

    if (result != S_FALSE && result != S_OK)
      return result;

    {
      UInt64 inProcessedSize = decoder->GetInProcessedSize();
      _packSize = inProcessedSize;
      _unpackSize = outStream->GetSize();
      _packSize_Defined = true;
      _unpackSize_Defined = true;
      lps.Interface()->SetRatioInfo(&_packSize, &_unpackSize);
    }

    if (!_isArc)
      opRes = NExtract::NOperationResult::kIsNotArc;
    else if (result == S_FALSE)
      opRes = NExtract::NOperationResult::kDataError;
    else
      opRes = NExtract::NOperationResult::kOK;
  }
  return extractCallback->SetOperationResult(opRes);
  COM_TRY_END
}

static HRESULT UpdateArchive(
    ISequentialOutStream *outStream,
    const CProps &props,
    IArchiveUpdateCallback *updateCallback)
{
  UInt64 unpackSize = 0;
  {
    CMyComPtr<ISequentialInStream> fileInStream;
    RINOK(updateCallback->GetStream(0, &fileInStream))
    if (!fileInStream)
      return S_FALSE;
    {
      Z7_DECL_CMyComPtr_QI_FROM(
          IStreamGetSize,
          streamGetSize, fileInStream)
      if (streamGetSize)
      {
        UInt64 size;
        if (streamGetSize->GetSize(&size) == S_OK)
          unpackSize = size;
      }
    }
    RINOK(updateCallback->SetTotal(unpackSize))

    CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
    lps->Init(updateCallback, true);
    {
      CMyComPtr2_Create<ICompressCoder, NCompress::NLz4::CEncoder> encoder;
      RINOK(props.SetCoderProps(encoder.ClsPtr(), NULL))
      RINOK(encoder.Interface()->Code(fileInStream, outStream, NULL, NULL, lps))
    }
  }
  return updateCallback->SetOperationResult(NArchive::NUpdate::NOperationResult::kOK);
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *timeType))
{
  *timeType = GET_FileTimeType_NotDefined_for_GetFileTimeType;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *updateCallback))
{
  COM_TRY_BEGIN

  if (numItems != 1)
    return E_INVALIDARG;

  Int32 newData, newProps;
  UInt32 indexInArchive;
  if (!updateCallback)
    return E_FAIL;
  RINOK(updateCallback->GetUpdateItemInfo(0, &newData, &newProps, &indexInArchive))

  if (IntToBool(newProps))
  {
    {
      NCOM::CPropVariant prop;
      RINOK(updateCallback->GetProperty(0, kpidIsDir, &prop))
      if (prop.vt != VT_EMPTY)
        if (prop.vt != VT_BOOL || prop.boolVal != VARIANT_FALSE)
          return E_INVALIDARG;
    }
  }

  if (IntToBool(newData))
    return UpdateArchive(outStream, _props, updateCallback);

  if (indexInArchive != 0)
    return E_INVALIDARG;

  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(updateCallback, true);

  Z7_DECL_CMyComPtr_QI_FROM(
      IArchiveUpdateCallbackFile,
      opCallback, updateCallback)
  if (opCallback)
  {
    RINOK(opCallback->ReportOperation(
        NEventIndexType::kInArcIndex, 0,
        NUpdateNotifyOp::kReplicate))
  }

  if (_stream)
    RINOK(InStream_SeekToBegin(_stream))

  return NCompress::CopyStream(_stream, outStream, lps);

  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t * const *names, const PROPVARIANT *values, UInt32 numProps))
{
  return _props.SetProperties(names, values, numProps);
}

REGISTER_ARC_IO(
  "lz4", "lz4 lz4l", "* .tar", 'l',
  kLz4Magic, 0
  , NArcInfoFlags::kKeepName
  , 0
  , IsArc_Lz4)

}}
