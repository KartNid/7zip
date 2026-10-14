// Lz4Register.cpp

#include "StdAfx.h"

#include "../Common/RegisterCodec.h"

#include "Lz4Decoder.h"
#ifndef Z7_EXTRACT_ONLY
#include "Lz4Encoder.h"
#endif

namespace NCompress {
namespace NLz4 {

REGISTER_CODEC_CREATE(CreateDec, CDecoder)

#ifndef Z7_EXTRACT_ONLY
REGISTER_CODEC_CREATE(CreateEnc, CEncoder)
#else
#define CreateEnc NULL
#endif

// 04 F7 11 04 - LZ4 (external codecs / Tino Reichardt slot)
REGISTER_CODEC_2(Lz4, CreateDec, CreateEnc, 0x04F71104, "LZ4")

}}
