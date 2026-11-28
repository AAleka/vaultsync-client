#ifndef MANIFEST_H
#define MANIFEST_H

/*
vaultsync-manifest 1\n
source <rest of line>\n
created <16 chars>\n
dir <size> <mtime> <path>\n
file <size> <mtime> <chunk_count> <path>\n
chunk <64 hex>\n
end <entry_count>\n
*/

#include "scanner.h"
#include "hash.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
* uint64_t size;
* uint64_t mtime;
* int      is_dir;
* char*    rel_path;
*/
typedef struct vs_manifest_entry {
    vs_file_entry_t info;
    size_t          chunk_count;
    size_t          chunk_capacity;
    char            (*ids)[VS_HASH_HEX_LEN + 1];
} vs_manifest_entry_t;

typedef struct vs_manifest {
    size_t               count;
    size_t               capacity;
    vs_manifest_entry_t* items;
    char*                source;
    char                 created[17];
} vs_manifest_t;

int vs_manifest_create(const char* source, const char* created, vs_manifest_t** out);
int vs_manifest_add_entry(vs_manifest_t* man, const vs_file_entry_t* info);
int vs_manifest_add_chunk(vs_manifest_t* man, const char* id);
int vs_manifest_parse(const void* data, const size_t size, vs_manifest_t** out);
int vs_manifest_serialize(const vs_manifest_t* man, void** data, size_t* size);
void vs_manifest_free(vs_manifest_t* man);

#ifdef __cplusplus
}
#endif

#endif