// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#ifndef GPT_H
#define GPT_H

#include <stdint.h>

#define GPT_MAGIC   0x5452415020494645ULL

#define LBA_SIZE    512
#define GUID_LEN    16
#define PARTNAME_SZ 72

struct gpt_header {
        uint64_t magic;
        uint32_t revision;
        uint32_t hdr_size;
        uint32_t hdr_crc32;
        uint32_t reserved;
        uint64_t current_lba;
        uint64_t backup_lba;
        uint64_t first_usable_lba;
        uint64_t last_usable_lba;
        uint8_t disk_guid[GUID_LEN];
        uint64_t first_part_lba;
        uint32_t n_parts;
        uint32_t part_entry_len;
        uint32_t part_array_crc32;
} __attribute__((packed));

struct gpt_entry_attributes {
        uint64_t required_to_function : 1;
        uint64_t no_block_io_protocol : 1;
        uint64_t legacy_bios_bootable : 1;
        uint64_t reserved : 45;
        uint64_t type_guid_specific : 16;
} __attribute__((packed));

struct gpt_entry {
        uint8_t partition_type_guid[GUID_LEN];
        uint8_t unique_partition_guid[GUID_LEN];
        uint64_t starting_lba;
        uint64_t ending_lba;
        struct gpt_entry_attributes attributes;
        uint8_t partition_name[PARTNAME_SZ];
} __attribute__((packed));

struct gpt_partition {
        uint8_t type_guid[GUID_LEN];
        uint8_t part_guid[GUID_LEN];
        uint64_t lba_start;
        uint64_t lba_end;
        uint64_t flags;
        uint16_t name[36];
};

#endif
