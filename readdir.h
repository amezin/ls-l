#pragma once

#include <stddef.h>
#include <stdbool.h>

#include <sys/stat.h>

struct directory_entry {
    struct stat stat;
    bool stat_ok : 1;
    bool is_dir : 1;
    char name[1];
};

struct directory_entry_array {
    size_t n;
    struct directory_entry **entries;
};

void directory_entries_init(struct directory_entry_array *arr);
bool directory_entries_read(struct directory_entry_array *arr);
void directory_entries_free(struct directory_entry_array *arr);
