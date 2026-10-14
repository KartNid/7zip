# Lz4.mak -- compile vendored lz4 library under C/Lz4/
# Append objects to C_OBJS early (recursive macro, $O expands at use).
# Compile rules live in Lz4Rules.mak (included after $O / CFLAGS are defined).

C_OBJS = $(C_OBJS) \
  $O\lz4.obj \
  $O\lz4hc.obj \
  $O\lz4frame.obj \
  $O\xxhash.obj \
