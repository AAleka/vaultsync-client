#ifndef STORAGE_H
#define STORAGE_H

#include "vaultsync/vaultsync.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vs_storage {
    void* ctx;
    int (*has_object)(void* ctx, const char* id, int* exists);
    int (*put_object)(void* ctx, const char* id, const void* data, size_t size);
    int (*get_object)(void* ctx, const char* id, void** data, size_t* size);
    int (*put_snapshot)(void* ctx, const char* id, const void* data, size_t size);
    int (*get_snapshot)(void* ctx, const char* id, void** data, size_t* size);
    int (*list_snapshots)(void* ctx, vs_str_list* out);
    int (*close)(void* ctx);
} vs_storage;

int vs_local_init(const char* repo_path);
int vs_local_open(const char* repo_path, vs_storage* out);

#ifdef __cplusplus
}
#endif

#endif