#ifndef SCANNER_H
#define SCANNER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
/*
* uint64_t size;
* uint64_t mtime;
* int      is_dir;
* char*    rel_path;
*/
typedef struct vs_file_entry
{
    uint64_t size;
    uint64_t mtime;
    int      is_dir;
    char*    rel_path;  /* UTF-8, '/' separators, relative to root, owned (malloc'd) */
} vs_file_entry_t;

typedef struct vs_file_list
{
    size_t           count;
    size_t           capacity;
    vs_file_entry_t* items;
} vs_file_list_t;

void vs_file_list_init(vs_file_list_t* list);
int vs_file_list_push(vs_file_list_t* list, const vs_file_entry_t *f);
void vs_file_list_free(vs_file_list_t* list);

int vs_scan(const char* root, vs_file_list_t* out);

#ifdef __cplusplus
}
#endif

#endif