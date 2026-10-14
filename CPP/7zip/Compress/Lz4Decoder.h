// Lz4Decoder.h

#ifndef ZIP7_INC_LZ4_DECODER_H
#define ZIP7_INC_LZ4_DECODER_H

#include "../../Common/MyCom.h"
#include "../ICoder.h"

namespace NCompress {
namespace NLz4 {

Z7_CLASS_IMP_COM_4(
  CDecoder
  , ICompressCoder
  , ICompressSetDecoderProperties2
  , ICompressSetFinishMode
  , ICompressGetInStreamProcessedSize
)
  UInt64 _inProcessed;
  UInt64 _outProcessed;
  Byte *_inBuf;
  Byte *_outBuf;
  UInt32 _inBufSize;
  UInt32 _outBufSize;
public:
  bool FinishMode;
  UInt64 GetInProcessedSize() const { return _inProcessed; }
  UInt64 GetOutProcessedSize() const { return _outProcessed; }
  CDecoder():
      _inProcessed(0), _outProcessed(0),
      _inBuf(NULL), _outBuf(NULL),
      _inBufSize(0), _outBufSize(0),
      FinishMode(true) {}
  ~CDecoder();
};

}}

#endif
