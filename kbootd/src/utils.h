// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#ifndef UTILS_H
#define UTILS_H

#define _GNU_SOURCE

#include <search.h>
#include <stdbool.h>
#include <stdio.h>

#define SZ_1K              0x00000400
#define SZ_1M              0x00100000
#define SZ_1G              0x40000000

#define log(...)           printf("[kbootd] " __VA_ARGS__)
#define ARRAY_SIZE(x)      ((sizeof(x)) / (sizeof(x[0])))
#define MIN(a, b)          ((a) < (b) ? (a) : (b))
#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))
#define BIT(nr)            (1 << (nr))

int kread(int fd, char *buffer, size_t buffer_count);
int kread_full(int fd, char *buffer, size_t buffer_count, size_t max_count);
int kwrite(int fd, char *buffer, size_t buffer_count);
int kwrite_full(int fd, char *buffer, size_t buffer_count, size_t max_count);

void run_program(const char *command, bool detach);

int write_to_file(const char *path, char *buffer, size_t buffer_count);
bool file_exist(const char *path);
int wait_file_created(const char *path);

unsigned long mem_avail(void);

void hashmap_add(struct hsearch_data *hashmap, char *key, void *data);
void *hashmap_get(struct hsearch_data *hashmap, char *key);

#endif
