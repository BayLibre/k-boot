// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Baylibre, SAS.
 * Author: Julien Masson <jmasson@baylibre.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <pthread.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"

#define INOTIFY_EVENT_SIZE    (sizeof(struct inotify_event))
#define INOTIFY_EVENT_BUF_LEN (1024 * (INOTIFY_EVENT_SIZE + 16))

int kread(int fd, char *buffer, size_t buffer_count)
{
        size_t count_read;

        count_read = read(fd, buffer, buffer_count);
        if (count_read == -1) {
                log("read failed: %s\n", strerror(errno));
                return -1;
        }

        if (count_read != buffer_count) {
                log("invalid read count: %ld != %ld\n", count_read, buffer_count);
                return -1;
        }

        return 0;
}

int kread_full(int fd, char *buffer, size_t buffer_count, size_t max_count)
{
        size_t count_total = 0;
        size_t count, count_read;

        while (count_total < buffer_count) {
                count = MIN(buffer_count - count_total, max_count);

                count_read = read(fd, buffer, count);
                if (count_read == -1) {
                        log("read failed: %s\n", strerror(errno));
                        return -1;
                }

                if (count_read != count) {
                        log("invalid read count: %ld != %ld\n", count_read, count);
                        return -1;
                }

                count_total += count_read;
                buffer += count_read;
        }

        return 0;
}

int kwrite(int fd, char *buffer, size_t buffer_count)
{
        size_t count_write;

        count_write = write(fd, buffer, buffer_count);
        if (count_write == -1) {
                log("write failed: %s\n", strerror(errno));
                return -1;
        }

        if (count_write != buffer_count) {
                log("invalid write count: %ld != %ld\n", count_write, buffer_count);
                return -1;
        }

        return 0;
}

int kwrite_full(int fd, char *buffer, size_t buffer_count, size_t max_count)
{
        size_t write_total = 0;
        size_t count, count_write;

        while (write_total < buffer_count) {
                count = MIN(buffer_count - write_total, max_count);

                count_write = write(fd, buffer, count);
                if (count_write == -1) {
                        log("write failed: %s\n", strerror(errno));
                        return -1;
                }

                if (count_write != count) {
                        log("invalid write count: %ld != %ld\n", count_write, count);
                        return -1;
                }

                write_total += count_write;
                buffer += count_write;
        }

        return 0;
}

void run_program(const char *command, bool detach)
{
        pid_t pid = fork();

        if (pid == -1) {
                log("fork failed\n");
                return;
        }

        /* child process */
        if (pid == 0) {
                if (detach)
                        setsid();
                execvp(command, NULL);
        }

        /* parent process */
        if (!detach)
                waitpid(pid, NULL, 0);
}

int write_to_file(const char *path, char *buffer, size_t buffer_count)
{
        int fd, ret;

        fd = open(path, O_WRONLY);
        if (fd == -1) {
                log("open %s failed: %s\n", path, strerror(errno));
                return -1;
        }

        ret = kwrite(fd, buffer, buffer_count);

        close(fd);

        return ret;
}

bool file_exist(const char *path)
{
        return !access(path, F_OK);
}

int wait_file_created(const char *path)
{
        char buffer[INOTIFY_EVENT_BUF_LEN];
        char *dir = dirname(strdup(path));
        char *name = basename(strdup(path));
        struct inotify_event *event;
        int fd, wd, count_read, i, ret = 0;

        fd = inotify_init();

        wd = inotify_add_watch(fd, dir, IN_CREATE);
        if (wd == -1) {
                printf("could not watch: %s\n", path);
                return -1;
        }

        while (1) {
                count_read = read(fd, buffer, INOTIFY_EVENT_BUF_LEN);
                if (count_read < 0) {
                        printf("read failed\n");
                        ret = -1;
                        goto exit;
                }

                i = 0;
                while (i < count_read) {
                        event = (struct inotify_event *)&buffer[i];
                        if (event->len && event->mask & IN_CREATE)
                                if (!strcmp(event->name, name))
                                        goto exit;
                        i++;
                }
        }

exit:
        inotify_rm_watch(fd, wd);
        close(fd);
        return ret;
}

unsigned long mem_avail(void)
{
        struct sysinfo info;

        if (sysinfo(&info) < 0)
                return 0;

        return info.freeram;
}

void hashmap_add(struct hsearch_data *hashmap, char *key, void *data)
{
        ENTRY e = { .key = key, .data = data };
        ENTRY *ep;

        if (!hsearch_r(e, ENTER, &ep, hashmap))
                log("cannot add hashmap entry: %s\n", key);
}

void *hashmap_get(struct hsearch_data *hashmap, char *key)
{
        ENTRY e = { .key = key };
        ENTRY *ep;

        if (!hsearch_r(e, FIND, &ep, hashmap))
                ep = NULL;

        return ep ? ep->data : NULL;
}
