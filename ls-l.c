#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

#include <bsd/string.h>

#include "readdir.h"
#include "unique.h"

#define OUTPUT_BLOCK_SIZE 1024

#define STRMODE_BUF_SIZE 12
#define STRMODE_COLUMN_SIZE 10

typedef int (*cmp_fn)(const void *, const void *);

static int uid_cmp(const uid_t *a, const uid_t *b)
{
    if (*a == *b) {
        return 0;
    }

    return *a < *b ? -1 : 1;
}

static int gid_cmp(const gid_t *a, const gid_t *b)
{
    if (*a == *b) {
        return 0;
    }

    return *a < *b ? -1 : 1;
}

static int entry_cmp(const struct directory_entry **a, const struct directory_entry **b)
{
    if ((*a)->is_dir != (*b)->is_dir) {
        if ((*a)->is_dir) {
            return -1;
        } else {
            return 1;
        }
    }

    return strcoll((*a)->name, (*b)->name);
}

static uintmax_t div_round(uintmax_t value, uintmax_t divisor)
{
    if (value % divisor >= divisor / 2 + divisor % 2) {
        return value / divisor + 1;
    }

    return value / divisor;
}

static uintmax_t block_size_convert(uintmax_t value)
{
    static_assert(
        OUTPUT_BLOCK_SIZE % S_BLKSIZE == 0 || S_BLKSIZE % OUTPUT_BLOCK_SIZE == 0,
        "OUTPUT_BLOCK_SIZE should be divisible by S_BLKSIZE, or S_BLKSIZE should be divisible by OUTPUT_BLOCK_SIZE"
    );

    if (S_BLKSIZE < OUTPUT_BLOCK_SIZE) {
        return div_round(value, OUTPUT_BLOCK_SIZE / S_BLKSIZE);
    } else {
        return value * (S_BLKSIZE / OUTPUT_BLOCK_SIZE);
    }
}

static bool format_mtime(struct timespec *mtime, char *out_buf, size_t out_buf_size, time_t now)
{
    /* Copy-pasted from coreutils' ls.c:
       Consider a time to be recent if it is within the past six months.
       A Gregorian year has 365.2425 * 24 * 60 * 60 == 31556952 seconds
       on the average.  Write this value as an integer constant to
       avoid floating point hassles.  */
    static const unsigned int half_year = 31556952 / 2;
    static const char *recent_format = "%b %e %H:%M";
    static const char *old_format = "%b %e  %Y";

    double dt = difftime(now, mtime->tv_sec);
    const char *format = (dt >= 0 && dt < half_year) ? recent_format : old_format;

    struct tm tm;
    if (localtime_r(&mtime->tv_sec, &tm) == NULL) {
        return false;
    }

    if (strftime(out_buf, out_buf_size, format, &tm) == 0) {
        return false;
    }

    return true;
}

static void cleanup_pointer_free(void *ptr)
{
    void **dptr = ptr;
    free(*dptr);
    *dptr = NULL;
}

static void cleanup_strv(char ***ptr)
{
    if (*ptr == NULL) {
        return;
    }

    for (char **i = *ptr; *i; i++) {
        free(*i);
    }

    cleanup_pointer_free(ptr);
}

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    if (argc > 1) {
        fprintf(stderr, "Command line arguments are not supported\n");
        return EXIT_FAILURE;
    }

    __attribute__((cleanup(directory_entries_free)))
    struct directory_entry_array entries;

    directory_entries_init(&entries);

    if (!directory_entries_read(&entries)) {
        return EXIT_FAILURE;
    }

    qsort(entries.entries, entries.n, sizeof(*entries.entries), (cmp_fn)entry_cmp);

    uintmax_t total_blocks = 0;

    __attribute__((cleanup(cleanup_pointer_free)))
    uid_t *uids = calloc(entries.n, sizeof(uid_t));

    __attribute__((cleanup(cleanup_pointer_free)))
    gid_t *gids = calloc(entries.n, sizeof(gid_t));

    __attribute__((cleanup(cleanup_strv)))
    char **mtimes = calloc(entries.n + 1, sizeof(char *));

    if (uids == NULL || gids == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    size_t n_uids_gids = 0;

    int nlink_column_width = 1;
    int size_column_width = 1;
    int mtime_column_width = 1;

    time_t now;
    if (time(&now) == (time_t)-1) {
        perror("time");
        return EXIT_FAILURE;
    }

    char mtime_buffer[256];

    for (size_t i = 0; i < entries.n; i++) {
        struct directory_entry *entry = entries.entries[i];

        if (!entry->stat_ok || !format_mtime(&entry->stat.st_mtim, mtime_buffer, sizeof(mtime_buffer), now)) {
            strcpy(mtime_buffer, "?");
        }

        mtimes[i] = strdup(mtime_buffer);

        int mtime_len = strlen(mtime_buffer);
        if (mtime_len > mtime_column_width) {
            mtime_column_width = mtime_len;
        }

        if (mtimes[i] == NULL) {
            perror("strdup");
            return EXIT_FAILURE;
        }

        if (!entry->stat_ok) {
            continue;
        }

        uids[n_uids_gids] = entry->stat.st_uid;
        gids[n_uids_gids] = entry->stat.st_gid;

        int nlink_width = snprintf(NULL, 0, "%ju", (uintmax_t)entry->stat.st_nlink);
        if (nlink_width > nlink_column_width) {
            nlink_column_width = nlink_width;
        }

        int size_width = snprintf(NULL, 0, "%ju", (uintmax_t)entry->stat.st_size);
        if (size_width > size_column_width) {
            size_column_width = size_width;
        }

        n_uids_gids += 1;
        total_blocks += entry->stat.st_blocks;
    }

    qsort(uids, n_uids_gids, sizeof(uids[0]), (cmp_fn)uid_cmp);
    size_t n_uids = unique(uids, n_uids_gids, sizeof(uids[0]), (cmp_fn)uid_cmp);

    __attribute__((cleanup(cleanup_strv)))
    char **uid_names = calloc(n_uids + 1, sizeof(char *));

    if (uid_names == NULL && n_uids > 0) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    int max_uid_name = 1;

    for (size_t i = 0; i < n_uids; i++) {
        struct passwd *passwd = getpwuid(uids[i]);
        if (passwd) {
            uid_names[i] = strdup(passwd->pw_name);
            if (uid_names[i] == NULL) {
                perror("strdup");
                return EXIT_FAILURE;
            }

            int len = strlen(passwd->pw_name);
            if (len > max_uid_name) {
                max_uid_name = len;
            }
        }
    }

    qsort(gids, n_uids_gids, sizeof(gids[0]), (cmp_fn)gid_cmp);
    size_t n_gids = unique(gids, n_uids_gids, sizeof(gids[0]), (cmp_fn)gid_cmp);

    __attribute__((cleanup(cleanup_strv)))
    char **gid_names = calloc(n_gids + 1, sizeof(char *));

    if (gid_names == NULL && n_gids > 0) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    int max_gid_name = 1;

    for (size_t i = 0; i < n_gids; i++) {
        struct group *group = getgrgid(gids[i]);
        if (group) {
            gid_names[i] = strdup(group->gr_name);
            if (gid_names[i] == NULL) {
                perror("strdup");
                return EXIT_FAILURE;
            }

            int len = strlen(group->gr_name);
            if (len > max_gid_name) {
                max_gid_name = len;
            }
        }
    }

    fprintf(stdout, "total %ju\n", block_size_convert(total_blocks));

    __attribute__((cleanup(cleanup_pointer_free)))
    char *readlink_buf = NULL;
    size_t readlink_buf_capacity = 0;

    for (size_t i = 0; i < entries.n; i++) {
        struct directory_entry *entry = entries.entries[i];
        const struct stat *stat = &entry->stat;

        char mode_buf[STRMODE_BUF_SIZE];
        if (entry->stat_ok) {
            strmode(stat->st_mode, mode_buf);
        } else {
            memset(mode_buf, '?', STRMODE_COLUMN_SIZE);
        }
        mode_buf[STRMODE_COLUMN_SIZE] = '\0'; /* TODO: acl, semaphore/mq/shm/tmo */

        const char *uid_name = "?";
        if (entry->stat_ok) {
            const uid_t *uid_key = bsearch(&entry->stat.st_uid, uids, n_uids, sizeof(*uids), (cmp_fn)uid_cmp);
            if (uid_key != NULL) {
                uid_name = uid_names[uid_key - uids];
            }
        }

        const char *gid_name = "?";
        if (entry->stat_ok) {
            const gid_t *gid_key = bsearch(&entry->stat.st_gid, gids, n_gids, sizeof(*gids), (cmp_fn)gid_cmp);
            if (gid_key != NULL) {
                gid_name = gid_names[gid_key - gids];
            }
        }

        const char *link = NULL;

        if (entry->stat_ok && S_ISLNK(entry->stat.st_mode)) {
            link = "?";

            if (readlink_buf_capacity < entry->stat.st_size + 1) {
                char *new_readlink_buf = realloc(readlink_buf, entry->stat.st_size + 1);
                if (new_readlink_buf != NULL) {
                    readlink_buf = new_readlink_buf;
                    readlink_buf_capacity = entry->stat.st_size + 1;
                }
            }

            if (readlink_buf_capacity > entry->stat.st_size) {
                ssize_t link_len = readlink(entry->name, readlink_buf, readlink_buf_capacity - 1);
                if (link_len >= 0) {
                    readlink_buf[link_len] = '\0';
                    link = readlink_buf;
                }
            }
        }

        fprintf(
            stdout,
            "%s %*ju %*s %*s %*ju %*s %s%s%s\n",
            mode_buf,
            nlink_column_width, (uintmax_t)entry->stat.st_nlink,
            max_uid_name, uid_name,
            max_gid_name, gid_name,
            size_column_width, (uintmax_t)entry->stat.st_size,
            mtime_column_width, mtimes[i],
            entry->name,
            link ? " -> " : "",
            link ? link : ""
        );
    }

    return EXIT_SUCCESS;
}
