// Lz4Encoder.h

#ifndef ZIP7_INC_LZ4_ENCODER_H
#define ZIP7_INC_LZ4_ENCODER_H

#include "../../Common/MyCom.h"
#include "../ICoder.h"

namespace NCompress {
namespace NLz4 {

Z7_CLASS_IMP_COM_3(
  CEncoder
  , ICompressCoder
  , ICompressSetCoderProperties
  , ICompressWriteCoderProperties
)
  UInt64 _inProcessed;
  UInt64 _outProcessed;
  Byte *_inBuf;
  Byte *_outBuf;
  UInt32 _inBufSize;
  UInt32 _outBufSize;
  UInt32 _level;
public:
  CEncoder():
      _inProcessed(0), _outProcessed(0),
      _inBuf(NULL), _outBuf(NULL),
      _inBufSize(0), _outBufSize(0),
      _level(1) {}
  ~CEncoder();
};

}}

#endif
