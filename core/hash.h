#ifndef HASH_H
#define HASH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VS_CHUNK_SIZE   (1024u * 1024u)  /* 1 MiB */
#define VS_HASH_HEX_LEN 64               /* 32 bytes -> 64 hex chars */

typedef int (*vs_chunk_fn)(void* user, const void* data, size_t n);

int vs_hash(const void* data, size_t n, char out_hex[VS_HASH_HEX_LEN + 1]);
int vs_chunk_file(const char* path, vs_chunk_fn on_chunk, void* user);

#ifdef __cplusplus
}
#endif

#endif