/* An independent caller of the original upstream zlib, assembled by MiniAS.
 * Exercise block and stream compression, checksums, gzip, and varargs. */
#include "zlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { DATA_LEN = 32768 };

static unsigned char input[DATA_LEN];
static unsigned char output[DATA_LEN];

static int test_block(void)
{
    uLongf compressed_len = compressBound(DATA_LEN);
    uLongf restored_len = DATA_LEN;
    unsigned char *compressed = malloc(compressed_len);
    int rc;

    if (!compressed) return 11;
    rc = compress2(compressed, &compressed_len, input, DATA_LEN, 6);
    if (rc != Z_OK) { free(compressed); return 12; }
    rc = uncompress(output, &restored_len, compressed, compressed_len);
    free(compressed);
    if (rc != Z_OK || restored_len != DATA_LEN ||
        memcmp(input, output, DATA_LEN) != 0) return 13;
    return 0;
}

static int test_stream(void)
{
    unsigned char *compressed;
    z_stream compressor = {0};
    z_stream decompressor = {0};
    unsigned long compressed_len;
    int rc;

    compressed = malloc(compressBound(DATA_LEN) + 128);
    if (!compressed) return 21;
    rc = deflateInit2(&compressor, 6, Z_DEFLATED, 15 + 16, 8,
                      Z_DEFAULT_STRATEGY);
    if (rc != Z_OK) { free(compressed); return 22; }
    compressor.next_in = input;
    compressor.avail_in = DATA_LEN;
    compressor.next_out = compressed;
    compressor.avail_out = (uInt)(compressBound(DATA_LEN) + 128);
    rc = deflate(&compressor, Z_FINISH);
    compressed_len = compressor.total_out;
    deflateEnd(&compressor);
    if (rc != Z_STREAM_END) { free(compressed); return 23; }

    rc = inflateInit2(&decompressor, 15 + 32);
    if (rc != Z_OK) { free(compressed); return 24; }
    decompressor.next_in = compressed;
    decompressor.avail_in = (uInt)compressed_len;
    decompressor.next_out = output;
    decompressor.avail_out = DATA_LEN;
    rc = inflate(&decompressor, Z_FINISH);
    inflateEnd(&decompressor);
    free(compressed);
    if (rc != Z_STREAM_END || decompressor.total_out != DATA_LEN ||
        memcmp(input, output, DATA_LEN) != 0) return 25;
    return 0;
}

static int test_gzfile(void)
{
    const char *filename = "minias-zlib-runtime.gz";
    const char *expected = "zlib-write:42:3.25";
    char buffer[64] = {0};
    gzFile handle = gzopen(filename, "wb");
    int n;

    if (!handle) return 31;
    n = gzprintf(handle, "zlib-write:%d:%.2f", 42, 3.25);
    {
        int close_rc = gzclose(handle);
        if (n != (int)strlen(expected) || close_rc != Z_OK) return 32;
    }
    handle = gzopen(filename, "rb");
    if (!handle) return 33;
    n = gzread(handle, buffer, (unsigned int)sizeof(buffer));
    if (gzclose(handle) != Z_OK || n != (int)strlen(expected) ||
        memcmp(buffer, expected, strlen(expected)) != 0) return 34;
    if (remove(filename) != 0) return 35;
    return 0;
}

int main(void)
{
    uLong crc;
    uLong adler;
    int result;
    size_t i;

    for (i = 0; i < DATA_LEN; i++) {
        input[i] = (unsigned char)((i / 17 + i * 13 + i / 509) & 0xff);
    }
    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, input, DATA_LEN);
    adler = adler32(0L, Z_NULL, 0);
    adler = adler32(adler, input, DATA_LEN);

    result = test_block();
    if (result != 0) return result;
    result = test_stream();
    if (result != 0) return result;
    result = test_gzfile();
    if (result != 0) return result;
    /* Independent checksum oracle computed from the input generator. */
    if (crc != 0xba292333UL || adler != 0xcbd1c3f6UL ||
        strcmp(zlibVersion(), "1.3.1") != 0) return 41;

    printf("ZLIB_RUNTIME=PASS bytes=%d crc=%08lx adler=%08lx gzip=PASS\n",
           DATA_LEN, (unsigned long)crc, (unsigned long)adler);
    return 0;
}
