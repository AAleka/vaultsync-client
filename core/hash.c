#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <vaultsync/vaultsync.h>

#include "blake3.h"
#include "hash.h"
#include "util.h"

int vs_hash(const void* data, size_t n, char out_hex[VS_HASH_HEX_LEN + 1])
{
    if ((!data && n) || !out_hex)
        return VS_ERR_INVALID;
    
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, n);

    uint8_t digest[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, digest, BLAKE3_OUT_LEN);

    vs_hex(digest, BLAKE3_OUT_LEN, out_hex);

    return VS_OK;
}

static int fill_buffer(HANDLE h, uint8_t* buffer, DWORD* bytes_chunk)
{
    *bytes_chunk = 0;
    while (*bytes_chunk < VS_CHUNK_SIZE)
    {
        DWORD bytes_read = 0;
        if (!ReadFile(h, buffer + *bytes_chunk, VS_CHUNK_SIZE - *bytes_chunk, &bytes_read, NULL))
            return vs_map_win_error(GetLastError());

        if (bytes_read == 0)
            break;  // EOF

        *bytes_chunk += bytes_read;
    }

    return VS_OK;
}

int vs_chunk_file(const char* path, vs_chunk_fn on_chunk, void* user)
{
    if (!path || !on_chunk)
        return VS_ERR_INVALID;

    wchar_t* wide_path = NULL;
    int rc = vs_utf8_to_wide(path, &wide_path);
    if (rc != VS_OK)
        return rc;
    
    HANDLE h = CreateFileW(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
    {
        free(wide_path);
        return vs_map_win_error(GetLastError());
    }
    free(wide_path);

    uint8_t* buffer = NULL;
    if ((buffer = malloc(VS_CHUNK_SIZE)) == NULL)
    {
        CloseHandle(h);
        return VS_ERR_NOMEM;
    }
    
    while (1)
    {
        DWORD bytes_chunk = 0;
        rc = fill_buffer(h, buffer, &bytes_chunk);
        if (rc != VS_OK || bytes_chunk == 0)
            break;  // read error or EOF

        rc = on_chunk(user, buffer, bytes_chunk);
        if (rc != VS_OK)
            break;

        if (bytes_chunk < VS_CHUNK_SIZE)
            break;  // short chunk can only be the last one
    }

    free(buffer);
    CloseHandle(h);

    return rc;
}
