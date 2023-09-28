// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include "boot.h"
#include "fastboot.h"
#include "fb_command.h"
#include "part.h"
#include "utils.h"

#include <sys/select.h>
#include <unistd.h>

#define REBOOT_TO_BOOTLOADER BIT(0)

static void start_console(void)
{
        run_program("console", true);
}

static bool check_reboot_bootloader_flag(void)
{
        uint64_t attr = 0;
        int ret;

        ret = part_read_attr("bootloaders", &attr);
        if (ret == -1) {
                log("cannot read attributes from bootloaders\n");
                return false;
        }

        if (attr & REBOOT_TO_BOOTLOADER) {
                attr &= ~REBOOT_TO_BOOTLOADER;
                ret = part_write_attr("bootloaders", attr);
                if (ret == -1)
                        log("cannot write attributes to bootloaders\n");
                return true;
        }

        return false;
}

static bool prompt_stop_boot(void)
{
        struct timeval timeout;
        fd_set s;
        int i = 4;

        do {
                log("Press any key to stop boot ... %i  \r", i);
                fflush(stdout);
                FD_ZERO(&s);
                FD_SET(STDIN_FILENO, &s);

                timeout.tv_sec = 1;
                timeout.tv_usec = 0;

                select(STDIN_FILENO + 1, &s, NULL, NULL, &timeout);
                if (FD_ISSET(STDIN_FILENO, &s))
                        break;
        } while (i-- > 0);

        return i > 0 ? true : false;
}

static bool stop_boot(void)
{
        bool stop;

        stop = check_reboot_bootloader_flag();
        if (stop) {
                log("reboot bootloader flag detected\n");
                return stop;
        }

        stop = prompt_stop_boot();
        if (stop)
                return stop;

        return false;
}

int main(void)
{
        int ret;

        log("revision: " REVISION "\n");

        ret = part_init();
        if (ret == -1) {
                log("part init failed\n");
                return ret;
        }

        if (stop_boot())
                start_console();
        else
                return boot_android();

        ret = fastboot_init();
        if (ret == -1) {
                log("fastboot init failed\n");
                return ret;
        }

        fb_command_loop();

        log("exit\n");

        return 0;
}
