// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#ifndef FASTBOOT_H
#define FASTBOOT_H

#include <stddef.h>

int fastboot_init(void);
int fastboot_write(char *buffer, size_t buffer_count);
int fastboot_read(char *buffer, size_t buffer_count);
int fastboot_read_full(char *buffer, size_t buffer_count);

#endif
