#ifndef SCANNER_H
#define SCANNER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vs_file_entry
{
    char*    rel_path;  /* UTF-8, '/' separators, relative to root, owned (malloc'd) */
    uint64_t size;
    uint64_t mtime;
    int      is_dir;
} vs_file_entry;

typedef struct vs_file_list
{
    size_t         count;
    size_t         capacity;
    vs_file_entry* items;
} vs_file_list;

void vs_file_list_init(vs_file_list* list);
int vs_file_list_push(vs_file_list* list, const vs_file_entry *f);
void vs_file_list_free(vs_file_list* list);

#ifdef __cplusplus
}
#endif

#endif