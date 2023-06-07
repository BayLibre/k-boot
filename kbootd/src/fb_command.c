// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include <errno.h>
#include <linux/reboot.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

#include "boot.h"
#include "fastboot.h"
#include "part.h"
#include "utils.h"

#define MAX_DOWNLOAD_SIZE (256 * SZ_1M)

typedef enum { OKAY, FAIL, INFO, DATA } fb_status;

struct fb_cmd {
        const char *command;
        fb_status (*handler)(char *args, char *rsp);
};

static fb_status cmd_continue(char *args, char *rsp);
static fb_status cmd_download(char *args, char *rsp);
static fb_status cmd_erase(char *args, char *rsp);
static fb_status cmd_flash(char *args, char *rsp);
static fb_status cmd_getvar(char *args, char *rsp);
static fb_status cmd_reboot(char *args, char *rsp);

static const struct fb_cmd cmds[] = {
        {.command = "continue",  .handler = cmd_continue},
        { .command = "download", .handler = cmd_download},
        { .command = "erase",    .handler = cmd_erase   },
        { .command = "flash",    .handler = cmd_flash   },
        { .command = "getvar",   .handler = cmd_getvar  },
        { .command = "reboot",   .handler = cmd_reboot  },
};

static fb_status current_slot(char *args, char *rsp);
static fb_status has_slot(char *args, char *rsp);
static fb_status is_logical(char *args, char *rsp);
static fb_status max_download_size(char *args, char *rsp);

static const struct fb_cmd vars[] = {
        {.command = "current-slot",       .handler = current_slot     },
        { .command = "has-slot",          .handler = has_slot         },
        { .command = "is-logical",        .handler = is_logical       },
        { .command = "max-download-size", .handler = max_download_size},
};

struct flash_node {
        char *path;
        char *data;
        size_t size;
        struct flash_node *next;
};

static struct flash_node *flash_queue;
static pthread_t flash_thread_id;
static pthread_mutex_t flash_mutex;
static bool flash_running;

struct download_node {
        char *data;
        int size;
        struct download_node *next;
};

static struct download_node *download_queue;

static bool fb_exit;

static const struct fb_cmd *find_cmd(const struct fb_cmd *table, int table_size,
                                     char *cmd)
{
        for (int i = 0; i < table_size; i++) {
                if (!strcmp(cmd, table[i].command))
                        return &table[i];
        }

        return NULL;
}

static void fb_response(fb_status status, char *rsp)
{
        char buffer[256] = { '\0' };

        switch (status) {
        case OKAY:
                snprintf(buffer, 256, "OKAY%s", rsp);
                break;
        case FAIL:
                snprintf(buffer, 256, "FAIL%s", rsp);
                break;
        case INFO:
                snprintf(buffer, 256, "INFO%s", rsp);
                break;
        case DATA:
                snprintf(buffer, 256, "DATA%s", rsp);
                break;
        }

        if (fastboot_write(buffer, strlen(buffer)))
                log("fastboot write failed\n");
}

static void fb_okay(char *rsp)
{
        fb_response(OKAY, rsp);
}

static void fb_info(char *rsp)
{
        fb_response(INFO, rsp);
}

static void download_queue_add(char *data, int size)
{
        struct download_node *node;

        node = malloc(sizeof(struct download_node));
        node->data = data;
        node->size = size;
        node->next = NULL;

        if (download_queue == NULL) {
                download_queue = node;
        } else {
                struct download_node *last = download_queue;
                while (last->next)
                        last = last->next;
                last->next = node;
        }
}

static void download_queue_pop(char **data, int *size)
{
        struct download_node *node;

        if (download_queue == NULL)
                return;

        node = download_queue;
        *data = node->data;
        *size = node->size;

        download_queue = node->next;
        free(node);
}

static fb_status cmd_download(char *args, char *rsp)
{
        char *data, *buffer;
        int buffer_size;

        sscanf(args, "%08x", &buffer_size);
        data = malloc(buffer_size);
        if (data == NULL)
                log("malloc failed: %s\n", strerror(errno));

        sprintf(rsp, "%08x", buffer_size);
        fb_response(DATA, rsp);
        memset(rsp, '\0', 256);

        buffer = data;
        if (fastboot_read_full(buffer, buffer_size)) {
                free(data);
                return FAIL;
        }

        download_queue_add(data, buffer_size);

        return OKAY;
}

static fb_status cmd_getvar(char *args, char *rsp)
{
        const struct fb_cmd *fb_var;
        char *cmd, *args2;
        fb_status status = FAIL;

        cmd = strtok_r(args, ":", &args2);
        fb_var = find_cmd(vars, ARRAY_SIZE(vars), cmd);
        if (fb_var)
                status = fb_var->handler(args2, rsp);
        else
                log("getvar: %s not supported\n", cmd);

        return status;
}

static fb_status cmd_erase(char *args, char *rsp)
{
        char *path;
        uint64_t part_size;
        int ret;

        path = part_get_path(args);
        if (!path) {
                log("cannot find partition: %s\n", args);
                return FAIL;
        }

        /* For MMC0 no need to erase the whole partition */
        if (!strcmp(path, MMC_BLK)) {
                part_size = 4096;
        } else {
                part_size = part_get_size(path);
                if (!part_size) {
                        log("partition size returned 0: %s\n", args);
                        return FAIL;
                }
        }

        ret = part_erase(path, part_size);

        return ret ? FAIL : OKAY;
}

static void flash_queue_add(char *path, char *data, size_t size)
{
        struct flash_node *node;

        node = malloc(sizeof(struct flash_node));
        node->path = path;
        node->data = data;
        node->size = size;
        node->next = NULL;

        pthread_mutex_lock(&flash_mutex);

        if (flash_queue == NULL) {
                flash_queue = node;
        } else {
                struct flash_node *last = flash_queue;
                while (last->next)
                        last = last->next;
                last->next = node;
        }

        pthread_mutex_unlock(&flash_mutex);
}

static void flash_queue_pop(char **path, char **data, size_t *size)
{
        struct flash_node *node;

        if (flash_queue == NULL)
                return;

        pthread_mutex_lock(&flash_mutex);

        node = flash_queue;
        *path = node->path;
        *data = node->data;
        *size = node->size;

        flash_queue = node->next;
        free(node);

        pthread_mutex_unlock(&flash_mutex);
}

static void *flash_thread(void *arg)
{
        char *path, *current_path = NULL, *data;
        uint64_t offset;
        size_t size;

        pthread_mutex_lock(&flash_mutex);
        flash_running = true;
        pthread_mutex_unlock(&flash_mutex);

        while (1) {
                path = NULL;
                data = NULL;
                size = 0;

                flash_queue_pop(&path, &data, &size);
                if (path == NULL)
                        break;

                if (current_path == NULL) {
                        current_path = strdup(path);
                        offset = 0;
                } else if (strcmp(current_path, path)) {
                        offset = 0;
                        free(current_path);
                        current_path = strdup(path);
                }

                part_flash(path, data, &offset, size);

                free(data);
        };

        if (current_path)
                free(current_path);

        pthread_mutex_lock(&flash_mutex);
        flash_running = false;
        pthread_mutex_unlock(&flash_mutex);

        return NULL;
}

static void flash_start_thread(void)
{
        if (pthread_create(&flash_thread_id, NULL, flash_thread, NULL))
                log("cannot create flash thread\n");
}

static void flash_wait_done(void)
{
        pthread_join(flash_thread_id, NULL);
}

static fb_status cmd_flash(char *args, char *rsp)
{
        char *path, *data = NULL;
        int size = 0;

        download_queue_pop(&data, &size);
        if (data == NULL) {
                log("no data downloaded\n");
                return FAIL;
        }

        path = part_get_path(args);
        if (!path) {
                log("cannot find partition: %s\n", args);
                return FAIL;
        }

        flash_queue_add(strdup(path), data, size);

        if (!flash_running)
                flash_start_thread();

        return OKAY;
}

static fb_status cmd_continue(char *args, char *rsp)
{
        if (flash_running) {
                fb_info("Waiting ongoing flash ...");
                flash_wait_done();
        }

        boot_android();

        fb_exit = true;

        return OKAY;
}

static fb_status cmd_reboot(char *args, char *rsp)
{
        if (flash_running) {
                fb_info("Waiting ongoing flash ...");
                flash_wait_done();
        }

        fb_okay("");
        reboot(LINUX_REBOOT_CMD_RESTART);

        /* should not reach here */
        return FAIL;
}

static fb_status current_slot(char *args, char *rsp)
{
        sprintf(rsp, "a");

        return OKAY;
}

static fb_status has_slot(char *args, char *rsp)
{
        char part_name[256] = { '\0' };

        snprintf(part_name, 256, "%s_a", args);

        sprintf(rsp, part_get_path(part_name) ? "yes" : "no");

        return OKAY;
}

static fb_status is_logical(char *args, char *rsp)
{
        sprintf(rsp, "no");

        return OKAY;
}

static fb_status max_download_size(char *args, char *rsp)
{
        unsigned long mem = mem_avail() / 3 * 2;
        unsigned long max = mem < MAX_DOWNLOAD_SIZE ? mem : MAX_DOWNLOAD_SIZE;

        sprintf(rsp, "%ld", max);

        return OKAY;
}

void fb_command_loop(void)
{
        char buffer[256] = { '\0' };
        char rsp[256] = { '\0' };
        const struct fb_cmd *fb_cmd;
        fb_status status;
        char *cmd, *args;
        size_t count_read;

        fb_exit = false;
        flash_running = false;
        pthread_mutex_init(&flash_mutex, NULL);

        log("wait fastboot commands ...\n");

        while (fb_exit == false) {
                status = FAIL;
                memset(rsp, '\0', 256);

                count_read = fastboot_read(buffer, 256);
                if (count_read == -1) {
                        log("fastboot read failed\n");
                        continue;
                }

                buffer[count_read] = '\0';
                log("%s\n", buffer);

                cmd = strtok_r(buffer, ":", &args);
                fb_cmd = find_cmd(cmds, ARRAY_SIZE(cmds), cmd);
                if (fb_cmd)
                        status = fb_cmd->handler(args, rsp);
                else
                        log("%s command not supported\n", cmd);

                fb_response(status, rsp);
        }
}
