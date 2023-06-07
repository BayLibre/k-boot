// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "android.h"
#include "part.h"
#include "utils.h"

int boot_android(void)
{
        struct boot_img_hdr_v2 hdr;
        char *path, *buffer;
        char cmdline[BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE];
        size_t cmdline_size = BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE;
        size_t offset;
        int fd, ret;

        path = part_get_path("boot_a");
        if (!path) {
                log("cannot find partition: %s\n", "boot_a");
                return -1;
        }

        ret = part_read(path, &hdr, 0, sizeof(struct boot_img_hdr_v2));
        if (ret == -1) {
                log("read boot image header failed\n");
                return -1;
        }

        /* cmdline */
        fd = open("/boot/cmdline", O_CREAT | O_WRONLY);
        if (fd == -1) {
                log("open /boot/cmdline failed: %s\n", strerror(errno));
                return -1;
        }

        snprintf(cmdline, cmdline_size, "%s %s", hdr.cmdline, hdr.extra_cmdline);

        ret = kwrite(fd, cmdline, cmdline_size);
        if (ret == -1)
                log("save cmdline failed\n");

        close(fd);

        /* Kernel */
        fd = open("/boot/Image", O_CREAT | O_WRONLY);
        if (fd == -1) {
                log("open /boot/Image failed: %s\n", strerror(errno));
                return -1;
        }

        buffer = malloc(hdr.kernel_size);
        offset = hdr.page_size;

        ret = part_read(path, buffer, offset, hdr.kernel_size);
        if (ret == -1) {
                log("read kernel failed\n");
                free(buffer);
                close(fd);
                return -1;
        }

        ret = kwrite_full(fd, buffer, hdr.kernel_size, 4096);
        if (ret == -1)
                log("save kernel failed\n");

        free(buffer);
        close(fd);

        /* Ramdisk */
        fd = open("/boot/ramdisk.img", O_CREAT | O_WRONLY);
        if (fd == -1) {
                log("open /boot/ramdisk.img failed: %s\n", strerror(errno));
                return -1;
        }

        buffer = malloc(hdr.ramdisk_size);
        offset += DIV_ROUND_UP(hdr.kernel_size, hdr.page_size) * hdr.page_size;

        ret = part_read(path, buffer, offset, hdr.ramdisk_size);
        if (ret == -1) {
                log("read ramdisk failed\n");
                free(buffer);
                close(fd);
                return -1;
        }

        ret = kwrite_full(fd, buffer, hdr.ramdisk_size, 4096);
        if (ret == -1)
                log("save ramdisk failed\n");

        free(buffer);
        close(fd);

        /* DTB */
        fd = open("/boot/dtb.img", O_CREAT | O_WRONLY);
        if (fd == -1) {
                log("open /boot/dtb.img failed: %s\n", strerror(errno));
                return -1;
        }

        buffer = malloc(hdr.dtb_size);
        offset += DIV_ROUND_UP(hdr.ramdisk_size, hdr.page_size) * hdr.page_size;
        offset += DIV_ROUND_UP(hdr.second_size, hdr.page_size) * hdr.page_size;
        offset += DIV_ROUND_UP(hdr.recovery_dtbo_size, hdr.page_size) * hdr.page_size;

        ret = part_read(path, buffer, offset, hdr.dtb_size);
        if (ret == -1) {
                log("read dtb failed\n");
                free(buffer);
                close(fd);
                return -1;
        }

        ret = kwrite_full(fd, buffer, hdr.dtb_size, 4096);
        if (ret == -1)
                log("save dtb failed\n");

        free(buffer);
        close(fd);

        return 0;
}
