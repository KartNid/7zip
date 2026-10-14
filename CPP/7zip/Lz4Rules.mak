# Lz4Rules.mak -- explicit compile rules for vendored lz4 (C/Lz4/)
# Include AFTER Build.mak so $O and CFLAGS_C_ALL are defined.

LZ4_WARN = -W1 -WX-
LZ4_DIR = ../../../../C/Lz4

$O\lz4.obj: $(LZ4_DIR)/lz4.c
	$(CC) $(CFLAGS_C_ALL) $(LZ4_WARN) $**

$O\lz4hc.obj: $(LZ4_DIR)/lz4hc.c
	$(CC) $(CFLAGS_C_ALL) $(LZ4_WARN) $**

$O\lz4frame.obj: $(LZ4_DIR)/lz4frame.c
	$(CC) $(CFLAGS_C_ALL) $(LZ4_WARN) $**

$O\xxhash.obj: $(LZ4_DIR)/xxhash.c
	$(CC) $(CFLAGS_C_ALL) $(LZ4_WARN) $**
