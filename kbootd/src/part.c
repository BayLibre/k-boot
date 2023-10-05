// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#define _LARGEFILE64_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gpt.h"
#include "part.h"
#include "sparse.h"
#include "utils.h"

static struct hsearch_data *partitions_htab;

static int mbr_valid(const char *mbr)
{
        return (mbr[510] == 0x55 && mbr[511] == 0xaa);
}

static void gpt_get_name(uint16_t *s, char *name)
{
        char buf[37];
        int i = 0;
        while (i < ARRAY_SIZE(buf) - 1) {
                if (s[i] == 0)
                        break;
                buf[i] = (0x20 <= s[i] && s[i] < 0x7f) ? s[i] : '?';
                i++;
        }
        buf[i] = '\0';
        memcpy(name, buf, ARRAY_SIZE(buf));
}

static void part_fill_hashmap(struct gpt_partition *partitions, int partitions_nbr)
{
        struct gpt_partition *part;
        char *name, *path;

        partitions_htab = malloc(sizeof(struct hsearch_data));
        memset(partitions_htab, 0, sizeof(struct hsearch_data));
        hcreate_r(partitions_nbr, partitions_htab);

        hashmap_add(partitions_htab, "mmc0", MMC_BLK);
        hashmap_add(partitions_htab, "mmc0boot0", MMC_BLK_BOOT0);
        hashmap_add(partitions_htab, "mmc0boot1", MMC_BLK_BOOT1);

        for (int i = 0; i < partitions_nbr; i++) {
                part = partitions + i;
                if (part->lba_start) {
                        name = malloc(256 * sizeof(char));
                        gpt_get_name(part->name, name);

                        path = malloc(256 * sizeof(char));
                        snprintf(path, 256, "%sp%d", MMC_BLK, i + 1);

                        hashmap_add(partitions_htab, name, path);
                }
        }
}

char *part_get_path(char *name)
{
        return hashmap_get(partitions_htab, name);
}

uint64_t part_get_size(char *path)
{
        uint64_t size;
        int fd;

        fd = open(path, O_RDONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", path, strerror(errno));
                return 0;
        }

        ioctl(fd, BLKGETSIZE64, &size);

        close(fd);

        return size;
}

int part_read(char *path, void *buffer, size_t offset, size_t size)
{
        int fd, ret;

        fd = open(path, O_RDONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", path, strerror(errno));
                return -1;
        }

        ret = lseek(fd, offset, SEEK_SET);
        if (ret == -1) {
                log("lseek part read failed: %s\n", strerror(errno));
                goto exit;
        }

        ret = kread_full(fd, buffer, size, 4096);
        if (ret == -1)
                log("read part %s failed\n", path);

exit:
        close(fd);
        return ret;
}

static int part_write_sparse(int fd, void *data, uint64_t part_size)
{
        struct sparse_header *sparse_header = (struct sparse_header *)data;
        struct chunk_header *chunk_header;
        uint64_t chunk, chunk_data_sz, fill_size, offset = 0;
        uint32_t *fill_buf, fill_val;
        int ret;

        data += sizeof(struct sparse_header);

        for (chunk = 0; chunk < sparse_header->total_chunks; chunk++) {
                chunk_header = (struct chunk_header *)data;

                data += CHUNK_HEADER_LEN;
                if (sparse_header->chunk_hdr_sz > CHUNK_HEADER_LEN) {
                        /* Skip the remaining bytes in a header that is longer than
			 * we expected.
			 */
                        data += (sparse_header->chunk_hdr_sz - CHUNK_HEADER_LEN);
                }

                chunk_data_sz = chunk_header->total_sz - sparse_header->chunk_hdr_sz;

                switch (chunk_header->chunk_type) {
                case CHUNK_TYPE_RAW:
                        if ((offset + chunk_data_sz) > part_size) {
                                log("request would exceed partition size\n");
                                return -1;
                        }

                        ret = kwrite(fd, data, chunk_data_sz);
                        if (ret == -1) {
                                log("write RAW chunk failed\n");
                                return -1;
                        }

                        data += chunk_data_sz;
                        break;

                case CHUNK_TYPE_FILL:
                        if (chunk_data_sz != sizeof(uint32_t)) {
                                log("bogus chunk size for chunk type FILL");
                                return -1;
                        }

                        fill_size = chunk_header->chunk_sz * sparse_header->blk_sz;

                        if ((offset + fill_size) > part_size) {
                                log("request would exceed partition size\n");
                                return -1;
                        }

                        fill_val = *(uint32_t *)data;

                        fill_buf = malloc(fill_size);
                        if (!fill_buf) {
                                log("malloc failed for: CHUNK_TYPE_FILL\n");
                                return -1;
                        }

                        uint32_t *tmp_buf;
                        tmp_buf = (uint32_t *)fill_buf;
                        for (uint32_t i = 0; i < (fill_size / sizeof(fill_val)); i++)
                                tmp_buf[i] = fill_val;

                        ret = kwrite(fd, (char *)fill_buf, fill_size);
                        free(fill_buf);
                        if (ret == -1) {
                                log("write FILL chunk failed\n");
                                return -1;
                        }

                        data += sizeof(uint32_t);
                        break;

                case CHUNK_TYPE_DONT_CARE:
                        uint64_t len = chunk_header->chunk_sz * sparse_header->blk_sz;
                        ret = lseek64(fd, len, SEEK_CUR);
                        if (ret == -1) {
                                log("lseek chunk DONT_CARE failed: %s\n",
                                    strerror(errno));
                                return -1;
                        }
                        break;

                case CHUNK_TYPE_CRC32:
                        log("chunk type CRC32 not supported\n");
                        data += sizeof(uint32_t);
                        break;

                default:
                        log("unknown chunk type: %x\n", chunk_header->chunk_type);
                        return -1;
                }
        }

        return 0;
}

static bool sparse_image(void *data)
{
        struct sparse_header *sparse_header = (struct sparse_header *)data;

        if ((sparse_header->magic == SPARSE_HEADER_MAGIC) &&
            (sparse_header->major_version) == 1)
                return true;

        return false;
}

static int part_write_raw(int fd, void *data, uint64_t *offset, size_t size)
{
        int ret;

        ret = lseek(fd, *offset, SEEK_SET);
        if (ret == -1) {
                log("lseek part write failed: %s\n", strerror(errno));
                return -1;
        }

        ret = kwrite(fd, data, size);
        if (ret == -1) {
                log("write to RAW partition failed\n");
                return -1;
        }

        *offset += size;

        return 0;
}

int part_flash(char *path, void *data, uint64_t *offset, size_t size)
{
        uint64_t part_size;
        int fd, ret;

        fd = open(path, O_WRONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", path, strerror(errno));
                return -1;
        }

        if (sparse_image(data)) {
                part_size = part_get_size(path);
                ret = part_write_sparse(fd, data, part_size);
        } else {
                ret = part_write_raw(fd, data, offset, size);
        }

        close(fd);
        return ret;
}

int part_erase(char *path, size_t len)
{
        char buf[4096] = { 0 };
        uint64_t range[2];
        int ret, fd;

        fd = open(path, O_WRONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", path, strerror(errno));
                return -1;
        }

        /* First attempt with BLKSECDISCARD */
        range[0] = 0;
        range[1] = len;

        ret = ioctl(fd, BLKSECDISCARD, &range);
        if (!ret)
                goto exit;

        /* Second attempt with BLKDISCARD */
        range[0] = 0;
        range[1] = len;

        ret = ioctl(fd, BLKDISCARD, &range);
        if (!ret)
                goto exit;

        /* Thirst attempt write zeros */
        ret = kwrite(fd, buf, 4096);
        if (ret == -1) {
                log("write zeros failed\n");
                goto exit;
        }

        fsync(fd);

exit:
        close(fd);
        return ret;
}

static void gpt_convert_efi_name_to_char(char *s, void *es, int n)
{
        char *ess = es;
        int i, j;

        memset(s, '\0', n);

        for (i = 0, j = 0; j <= n; i += 2, j++) {
                s[j] = ess[i];
                if (!ess[i])
                        return;
        }
}

static int find_gpt_entry(int fd, const char *name, struct gpt_entry *gpt_e,
                          off_t *offset)
{
        struct gpt_header *gpt_hdr;
        char part[PARTNAME_SZ];
        char data[LBA_SIZE];
        int ret;

        /* GPT header on LBA 1 */
        ret = lseek(fd, LBA_SIZE * 1, SEEK_SET);
        if (ret == -1) {
                log("lseek LBA 1 failed: %s\n", strerror(errno));
                return ret;
        }

        memset(data, '\0', LBA_SIZE);
        ret = kread(fd, data, LBA_SIZE);
        if (ret == -1) {
                log("read GPT header failed\n");
                return -1;
        }
        gpt_hdr = (struct gpt_header *)data;

        for (int i = 0; i < gpt_hdr->n_parts; i++) {
                memset(data, '\0', LBA_SIZE);
                ret = kread(fd, data, LBA_SIZE);
                if (ret == -1) {
                        log("read GPT entry failed\n");
                        return -1;
                }
                memcpy(gpt_e, data, sizeof(struct gpt_entry));

                gpt_convert_efi_name_to_char(part, gpt_e->partition_name, PARTNAME_SZ);
                if (!strcmp(part, name)) {
                        *offset = (i + 2) * LBA_SIZE;
                        return 0;
                }
        }

        return -1;
}

int part_read_attr(char *name, uint64_t *attr)
{
        struct gpt_entry gpt_e;
        off_t offset;
        int fd, ret = 0;

        fd = open(MMC_BLK, O_RDONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", MMC_BLK, strerror(errno));
                return -1;
        }

        ret = find_gpt_entry(fd, name, &gpt_e, &offset);
        if (ret == -1) {
                log("find GPT entry failed\n");
                goto exit;
        }

        *attr = gpt_e.attributes.type_guid_specific;

exit:
        close(fd);
        return ret;
}

int part_write_attr(char *name, uint64_t attr)
{
        struct gpt_entry gpt_e;
        off_t offset;
        int fd, ret;

        fd = open(MMC_BLK, O_RDWR);
        if (fd == -1) {
                log("open %s failed: %s\n", MMC_BLK, strerror(errno));
                return -1;
        }

        ret = find_gpt_entry(fd, name, &gpt_e, &offset);
        if (ret == -1) {
                log("find GPT entry failed\n");
                goto exit;
        }

        gpt_e.attributes.type_guid_specific = attr;

        ret = lseek(fd, offset, SEEK_SET);
        if (ret == -1) {
                log("lseek part write attr failed: %s\n", strerror(errno));
                goto exit;
        }

        ret = kwrite(fd, (char *)&gpt_e, sizeof(struct gpt_entry));
        if (ret == -1)
                log("write GPT entry failed\n");

exit:
        close(fd);
        return ret;
}

int part_init(void)
{
        char mbr[LBA_SIZE];
        struct gpt_header gpt_hdr;
        struct gpt_partition *partitions;
        uint64_t partitions_size;
        int fd, ret;

        if (!file_exist(MMC_BLK)) {
                if (wait_file_created(MMC_BLK)) {
                        log("cannot get mmc node\n");
                        return -1;
                }
        }

        fd = open(MMC_BLK, O_RDONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", MMC_BLK, strerror(errno));
                return -1;
        }

        ret = kread(fd, mbr, LBA_SIZE);
        if (ret == -1) {
                log("read MBR failed\n");
                goto exit;
        }

        if (!mbr_valid(mbr)) {
                log("invalid MBR\n");
                ret = -1;
                goto exit;
        }

        /* GPT header on LBA 1 */
        ret = lseek(fd, LBA_SIZE * 1, SEEK_SET);
        if (ret == -1) {
                log("lseek LBA 1 failed: %s\n", strerror(errno));
                goto exit;
        }

        ret = kread(fd, (char *)&gpt_hdr, LBA_SIZE);
        if (ret == -1) {
                log("read GPT header failed\n");
                goto exit;
        }

        if (gpt_hdr.magic != GPT_MAGIC) {
                log("invalid GPT header\n");
                ret = -1;
                goto exit;
        }

        /* TODO: check crc32 */

        ret = lseek(fd, LBA_SIZE * gpt_hdr.first_part_lba, SEEK_SET);
        if (ret == -1) {
                log("lseek first part LBA failed: %s\n", strerror(errno));
                goto exit;
        }

        partitions_size = gpt_hdr.n_parts * gpt_hdr.part_entry_len;
        partitions = malloc(partitions_size);

        char *buffer = (char *)partitions;
        ret = kread_full(fd, buffer, partitions_size, 4096);
        if (ret == -1) {
                log("read partitions failed\n");
                free(partitions);
                goto exit;
        }

        part_fill_hashmap(partitions, gpt_hdr.n_parts);
        free(partitions);

        /* enable write access to boot partitions */
        if (write_to_file(MMC_SYS_BOOT0_RO, "0", 1))
                log("cannot enable write access on %sn", MMC_SYS_BOOT0_RO);
        if (write_to_file(MMC_SYS_BOOT1_RO, "0", 1))
                log("cannot enable write access on %sn", MMC_SYS_BOOT1_RO);

exit:
        close(fd);
        return ret;
}
