#include "readdir.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>

void directory_entries_init(struct directory_entry_array *arr)
{
    arr->n = 0;
    arr->entries = NULL;
}

static bool directory_entries_read_open(DIR *dir, struct directory_entry_array *arr)
{
    size_t capacity = arr->n;

    for (;;) {
        errno = 0;

        struct dirent *dirent = readdir(dir);
        if (dirent == NULL) {
            if (errno != 0) {
                perror("readdir");
                return false;
            }

            break;
        }

        if (dirent->d_name[0] == '.') {
            continue;
        }

        size_t name_len = strlen(dirent->d_name);

        struct directory_entry *entry = malloc(sizeof(struct directory_entry) + name_len);

        if (entry == NULL) {
            perror("malloc");
            return false;
        }

        memcpy(entry->name, dirent->d_name, name_len + 1);

        entry->stat_ok = lstat(entry->name, &entry->stat) == 0;

        if (entry->stat_ok) {
            entry->is_dir = S_ISDIR(entry->stat.st_mode);

            if (S_ISLNK(entry->stat.st_mode)) {
                struct stat resolved_stat;
                if (stat(entry->name, &resolved_stat) == 0) {
                    entry->is_dir = S_ISDIR(resolved_stat.st_mode);
                }
            }
        } else {
            memset(&entry->stat, 0, sizeof(entry->stat));
            entry->is_dir = false;
        }

        if (arr->n == capacity) {
            size_t new_capacity = capacity > 0 ? capacity * 2 : 1;
            struct directory_entry **new_entries = reallocarray(arr->entries, new_capacity, sizeof(*arr->entries));

            if (new_entries == NULL) {
                free(entry);
                perror("realloc");
                return false;
            }

            arr->entries = new_entries;
            capacity = new_capacity;
        }

        arr->entries[arr->n++] = entry;
    }

    return true;
}

bool directory_entries_read(struct directory_entry_array *arr)
{
    DIR *dir = opendir(".");

    if (dir == NULL) {
        perror("opendir");
        return false;
    }

    bool res = directory_entries_read_open(dir, arr);

    if (closedir(dir) != 0) {
        perror("closedir");
        return false;
    }

    return res;
}

void directory_entries_free(struct directory_entry_array *arr)
{
    for (size_t i = 0; i < arr->n; i++) {
        free(arr->entries[i]);
    }

    free(arr->entries);

    directory_entries_init(arr);
}
