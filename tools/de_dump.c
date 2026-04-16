#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../include/formats.h"

// Simple endian readers for dump tool
static uint32_t u32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
static uint64_t u64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= ((uint64_t)p[i] << (i * 8));
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: de_dump <file>\n");
        return 1;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f)
    {
        perror("open");
        return 1;
    }

    uint8_t hdr[128];
    if (fread(hdr, 1, 128, f) != 128)
    {
        fprintf(stderr, "Truncated header\n");
        return 1;
    }

    if (memcmp(hdr, MAGIC_ODAT, 4) == 0)
    {
        printf("FORMAT: ODAT\nRows: %u\nCols: %u\nStride: %u\n",
               u32(hdr + 8), u32(hdr + 12) & 0xFFFF, u32(hdr + 20));
    }
    else if (memcmp(hdr, MAGIC_OVOC, 4) == 0)
    {
        printf("FORMAT: OVOC\nVocab: %u\nDim: %u\nHashCap: %u\n",
               u32(hdr + 8), u32(hdr + 12), u32(hdr + 20));
    }
    else if (memcmp(hdr, MAGIC_OVEC, 4) == 0)
    {
        printf("FORMAT: OVEC\nRows: %u\nDim: %u\n", u32(hdr + 8), u32(hdr + 12));
    }
    else
    {
        printf("UNKNOWN FORMAT\n");
    }

    fclose(f);
    return 0;
}