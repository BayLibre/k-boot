// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#ifndef PART_H
#define PART_H

#include <stddef.h>
#include <stdint.h>

#define MMC_BLK          "/dev/mmcblk0"
#define MMC_BLK_BOOT0    "/dev/mmcblk0boot0"
#define MMC_SYS_BOOT0_RO "/sys/block/mmcblk0boot0/force_ro"
#define MMC_BLK_BOOT1    "/dev/mmcblk0boot1"
#define MMC_SYS_BOOT1_RO "/sys/block/mmcblk0boot1/force_ro"

int part_init(void);

char *part_get_path(char *name);
uint64_t part_get_size(char *path);

int part_read(char *path, void *buffer, size_t offset, size_t size);
int part_flash(char *path, void *data, uint64_t *offset, size_t size);
int part_erase(char *path, size_t len);

int part_read_attr(char *name, uint64_t *attr);
int part_write_attr(char *name, uint64_t attr);

#endif
