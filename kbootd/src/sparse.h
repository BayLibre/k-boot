// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#ifndef SPARSE_H
#define SPARSE_H

#define SPARSE_HEADER_MAGIC 0xed26ff3a

struct sparse_header {
        uint32_t magic;          /* 0xed26ff3a */
        uint16_t major_version;  /* (0x1) - reject images with higher major versions */
        uint16_t minor_version;  /* (0x0) - allow images with higer minor versions */
        uint16_t file_hdr_sz;    /* 28 bytes for first revision of the file format */
        uint16_t chunk_hdr_sz;   /* 12 bytes for first revision of the file format */
        uint32_t blk_sz;         /* block size in bytes, must be a multiple of 4 */
        uint32_t total_blks;     /* total blocks in the non-sparse output image */
        uint32_t total_chunks;   /* total chunks in the sparse input image */
        uint32_t image_checksum; /* CRC32 checksum of the original data */
};

#define CHUNK_TYPE_RAW       0xCAC1
#define CHUNK_TYPE_FILL      0xCAC2
#define CHUNK_TYPE_DONT_CARE 0xCAC3
#define CHUNK_TYPE_CRC32     0xCAC4

struct chunk_header {
        uint16_t chunk_type; /* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
        uint16_t reserved1;
        uint32_t chunk_sz; /* in blocks in output image */
        uint32_t total_sz; /* in bytes of chunk input file including chunk header and data */
};

#define CHUNK_HEADER_LEN sizeof(struct chunk_header)

#endif
