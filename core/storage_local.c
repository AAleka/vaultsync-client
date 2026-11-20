#include <stdlib.h>
#include <time.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include "storage.h"
#include "util.h"
#include "hash.h"
#include "scanner.h"

#define TMP_FILENAME_SIZE 64
#define VS_REPO_META "vaultsync-repo 1\n"

typedef struct local_ctx {
    char* objects_dir;
    char* snapshots_dir;
    char* tmp_dir;
    char* repo_path;
} local_ctx;

static int create_dir(const char* path)
{
    if (!path)
        return VS_ERR_INVALID;
    
    wchar_t* path_wide = NULL;
    int rc = VS_OK;

    rc = vs_utf8_to_wide(path, &path_wide);
    if (rc != VS_OK)
        return rc;
    
    if (!CreateDirectoryW(path_wide, NULL))
    {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS)
        {
            rc = vs_map_win_error(err);
            goto cleanup;
        }
    }

cleanup:
    free(path_wide);

    return rc;
}

static int validate_object_id(const char* id)
{
    if (!id || strlen(id) != VS_HASH_HEX_LEN)
        return VS_ERR_INVALID;
    
    while (*id != '\0')
    {
        if (!((*id >= '0' && *id <= '9') ||
            (*id >= 'a' && *id <= 'f')))
            return VS_ERR_INVALID;
        id++;
    }

    return VS_OK;
}

static int validate_snapshot_id(const char* id)
{
    if (!id || strlen(id) > 32 || strlen(id) < 1)
        return VS_ERR_INVALID;
    
    while (*id != '\0')
    {
        if (!((*id >= '0' && *id <= '9') ||
            (*id >= 'a' && *id <= 'z') ||
            (*id >= 'A' && *id <= 'Z')))
            return VS_ERR_INVALID;

        id++;
    }

    return VS_OK;
}

static void rand_str(char* dest, size_t len)
{
    static int seeded = 0;

    if (!seeded)
    {
        srand((unsigned int) time(NULL) ^ (unsigned int) GetCurrentProcessId());
        seeded = 1;
    }

    static const char charset[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    const size_t charset_len = sizeof(charset) - 1;

    while (len-- > 0)
    {
        size_t idx = (size_t)rand() % charset_len;
        *dest++ = charset[idx];
    }

    *dest = '\0';
}

static int read_all(const char* path, void** data, size_t* size)
{
    if (!path || !data || !size)
        return VS_ERR_INVALID;

    *data = NULL;

    int rc = VS_OK;
    uint8_t* buffer = NULL;
    wchar_t* path_wide = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;

    rc = vs_utf8_to_wide(path, &path_wide);
    if (rc != VS_OK)
        return rc;
    
    file = CreateFileW(path_wide, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        rc = vs_map_win_error(GetLastError());
        goto cleanup;
    }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(file, &sz))
    {
        rc = vs_map_win_error(GetLastError());
        goto cleanup;
    }

    buffer = malloc(sz.QuadPart > 0 ? sz.QuadPart : 1);
    if (!buffer)
    {
        rc = VS_ERR_NOMEM;
        goto cleanup;
    }

    size_t read_total = 0;
    while (read_total < (size_t) sz.QuadPart)
    {
        DWORD read = 0;
        DWORD piece = (sz.QuadPart - read_total > MAXDWORD) ? MAXDWORD : (DWORD)(sz.QuadPart - read_total);
        if (!ReadFile(file, buffer + read_total, piece, &read, NULL))
        {
            rc = vs_map_win_error(GetLastError());
            goto cleanup;
        }

        if (read == 0)
        {
            rc = VS_ERR_CORRUPT;
            goto cleanup;
        }

        read_total += read;
    }

    *data = buffer;
    *size = read_total;

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (rc != VS_OK) free(buffer);
    free(path_wide);

    return rc;
}

static int write_atomic(const char* tmp_dir, const char* final_path, const void* data, size_t size, int exists_ok)
{
    if (!tmp_dir || !final_path || (!data && size))
        return VS_ERR_INVALID;

    int rc = VS_OK;

    char random_name[TMP_FILENAME_SIZE];
    rand_str(random_name, sizeof(random_name) - 1);

    char* tmp_path = NULL;
    rc = vs_path_join(tmp_dir, random_name, &tmp_path);
    if (rc != VS_OK)
        return rc;
    
    wchar_t* tmp_path_wide = NULL;
    rc = vs_utf8_to_wide(tmp_path, &tmp_path_wide);
    free(tmp_path);
    if (rc != VS_OK)
        return rc;
    
    wchar_t* final_path_wide = NULL;
    rc = vs_utf8_to_wide(final_path, &final_path_wide);
    if (rc != VS_OK)
    {
        free(tmp_path_wide);
        return rc;
    }

    HANDLE file = CreateFileW(tmp_path_wide, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    int created = 0;
    if (file == INVALID_HANDLE_VALUE)
    {
        rc = vs_map_win_error(GetLastError());
        goto cleanup;
    }
    created = 1;

    const uint8_t* buf = (const uint8_t*) data;
    size_t written_total = 0;
    while (written_total < size)
    {
        DWORD written = 0;
        DWORD piece = (size - written_total > MAXDWORD) ? MAXDWORD : (DWORD)(size - written_total);
        if (!WriteFile(file, buf + written_total, piece, &written, NULL))
        {
            rc = vs_map_win_error(GetLastError());
            goto cleanup;
        }

        if (written == 0)
        {
            rc = VS_ERR_IO;
            goto cleanup;
        }

        written_total += written;
    }

    if (!FlushFileBuffers(file))
    {
        rc = vs_map_win_error(GetLastError());
        goto cleanup;
    }

    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;

    if (MoveFileExW(tmp_path_wide, final_path_wide, MOVEFILE_WRITE_THROUGH))
    {
        created = 0;
    }
    else
    {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS || err == ERROR_FILE_EXISTS)
            rc = exists_ok ? VS_OK : VS_ERR_INVALID;
        else
            rc = vs_map_win_error(err);
    }

cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (created) DeleteFileW(tmp_path_wide);
    free(tmp_path_wide);
    free(final_path_wide);

    return rc;
}

static int directory_exists(const char* path, int* exists)
{
    *exists = 0;
    wchar_t* path_wide = NULL;
    DWORD err = 0;
    int rc = VS_OK;
    
    rc = vs_utf8_to_wide(path, &path_wide);
    if (rc != VS_OK)
        return rc;

    DWORD attrs = GetFileAttributesW(path_wide);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
        {
            rc = VS_OK;
            *exists = 0;
        }
        else
        {
            rc = vs_map_win_error(err);
        }
    }
    else
        *exists = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    free(path_wide);
    
    return rc;
}

int vs_local_init(const char* repo_path)
{
    if (!repo_path)
        return VS_ERR_INVALID;
    
    char *path = NULL, *meta_path = NULL, *repo_tmp_path = NULL;
    int rc = VS_OK;
    const char* repo_subdirs[] = {"objects", "snapshots", "tmp"};
 
    rc = create_dir(repo_path);
    if (rc != VS_OK)
        goto cleanup;

    for (size_t i = 0; i < sizeof(repo_subdirs) / sizeof(repo_subdirs[0]); ++i)
    {
        rc = vs_path_join(repo_path, repo_subdirs[i], &path);
        if (rc != VS_OK)
            goto free;
        
        rc = create_dir(path);
        if (rc != VS_OK)
            goto free;

        if (strcmp(repo_subdirs[i], "tmp") == 0)
        {
            repo_tmp_path = path;
            path = NULL;
        }
        
free:
        free(path);
        path = NULL;

        if (rc != VS_OK)
            goto cleanup;
    }

    rc = vs_path_join(repo_path, "repository.meta", &meta_path);
    if (rc != VS_OK)
        goto cleanup;

    rc = write_atomic(repo_tmp_path, meta_path, VS_REPO_META, sizeof(VS_REPO_META) - 1, 0);
    
cleanup:
    free(repo_tmp_path);
    free(path);
    free(meta_path);

    return rc;
}

static int object_path(const local_ctx* c, const char* id, char** out)
{
    if (!c || !id || !out)
        return VS_ERR_INVALID;
    
    int rc = VS_OK;
    char* tmp = NULL;

    rc = validate_object_id(id);
    if (rc != VS_OK)
        return rc;

    char sub[3] = { id[0], id[1], '\0' };

    rc = vs_path_join(c->objects_dir, sub, &tmp);
    if (rc != VS_OK)
        return rc;
    
    rc = vs_path_join(tmp, id, out);
    free(tmp);

    return rc;
}

static int snapshot_path(const local_ctx* c, const char* id, char** out)
{
    if (!c || !id || !out)
        return VS_ERR_INVALID;
    
    int rc = VS_OK;

    rc = validate_snapshot_id(id);
    if (rc != VS_OK)
        return rc;

    rc = vs_path_join(c->snapshots_dir, id, out);

    return rc;
}

static int vs_local_has_object(void* ctx, const char* id, int* exists)
{
    if (!ctx || !id || !exists)
        return VS_ERR_INVALID;

    int rc = VS_OK;
    local_ctx* c = ctx;
    char* path = NULL;
    wchar_t* path_wide = NULL;
    DWORD attrs = 0;

    *exists = 0;

    rc = object_path(c, id, &path);
    if (rc != VS_OK)
        return rc;
    
    rc = vs_utf8_to_wide(path, &path_wide);
    if (rc != VS_OK)
        goto cleanup;

    attrs = GetFileAttributesW(path_wide);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
        {
            rc = VS_OK;
            *exists = 0;
        }
        else
        {
            rc = vs_map_win_error(err);
        }
    }
    else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        rc = VS_OK;
        *exists = 1;
    }
    else
    {
        rc = VS_ERR_CORRUPT;
    }
    
cleanup:
    free(path);
    free(path_wide);

    return rc;
}

static int vs_local_put_object(void* ctx, const char* id, const void* data, size_t size)
{
    if (!ctx || !id || (!data && size))
        return VS_ERR_INVALID;

    int rc = VS_OK;
    char* path = NULL;
    int exists = 0;
    local_ctx* c = ctx;

    rc = validate_object_id(id);
    if (rc != VS_OK)
        return rc;

    char sub[3] = { id[0], id[1], '\0' };

    rc = vs_local_has_object(ctx, id, &exists);
    if (rc != VS_OK)
        return rc;
    else if (exists)
        return VS_OK;

    rc = vs_path_join(c->objects_dir, sub, &path);
    if (rc != VS_OK)
        return rc;

    rc = create_dir(path);
    if (rc != VS_OK)
        goto cleanup;

    free(path);
    path = NULL;

    rc = object_path(c, id, &path);
    if (rc != VS_OK)
        goto cleanup;

    rc = write_atomic(c->tmp_dir, path, data, size, 1);

cleanup:
    free(path);

    return rc;
}

static int vs_local_get_object(void* ctx, const char* id, void** data, size_t* size)
{
    if (!ctx || !id || !data || !size)
        return VS_ERR_INVALID;

    *data = NULL;
    *size = 0;

    int rc = VS_OK;
    char* path = NULL;
    local_ctx* c = ctx;

    rc = object_path(c, id, &path);
    if (rc != VS_OK)
        return rc;
    
    rc = read_all(path, data, size);
    free(path);

    return rc;
}

static int vs_local_put_snapshot(void* ctx, const char* id, const void* data, size_t size)
{
    if (!ctx || !id || (!data && size))
        return VS_ERR_INVALID;
    
    int rc = VS_OK;
    char* path = NULL;
    local_ctx* c = ctx;

    rc = snapshot_path(c, id, &path);
    if (rc != VS_OK)
        return rc;

    rc = write_atomic(c->tmp_dir, path, data, size, 0);
    free(path);

    return rc;
}

static int vs_local_get_snapshot(void* ctx, const char* id, void** data, size_t* size)
{
    if (!ctx || !id || !data || !size)
        return VS_ERR_INVALID;

    *data = NULL;
    *size = 0;

    int rc = VS_OK;
    char* path = NULL;
    local_ctx* c = ctx;

    rc = snapshot_path(c, id, &path);
    if (rc != VS_OK)
        return rc;

    rc = read_all(path, data, size);
    free(path);

    return rc;
}

static int vs_local_list_snapshots(void* ctx, vs_str_list* out)
{
    if (!ctx || !out)
        return VS_ERR_INVALID;

    int rc = VS_OK;
    local_ctx* c = ctx;
    vs_file_list files = {0};

    vs_str_list_init(out);

    rc = vs_scan(c->snapshots_dir, &files);
    if (rc != VS_OK)
        goto cleanup;

    for (size_t i = 0; i < files.count; ++i)
    {
        vs_file_entry entry = files.items[i];
        if (entry.is_dir || validate_snapshot_id(entry.rel_path) != VS_OK)
            continue;
        
        rc = vs_str_list_push(out, entry.rel_path);
        if (rc != VS_OK)
            break;
    }

cleanup:
    vs_file_list_free(&files);
    if (rc != VS_OK) vs_str_list_free(out);

    return rc;
}

static int vs_local_close(void* ctx)
{
    if (!ctx)
        return VS_OK;
    
    local_ctx* c = ctx;

    free(c->objects_dir);
    free(c->snapshots_dir);
    free(c->tmp_dir);
    free(c->repo_path);
    free(c);

    return VS_OK;
}

int vs_local_open(const char* repo_path, vs_storage* out)
{
    if (!repo_path || out == NULL)
        return VS_ERR_INVALID;

    memset(out, 0, sizeof(*out));

    int rc = VS_OK;
    char* path = NULL;
    void* buffer = NULL;
    size_t size = 0;
    const char* subdir_names[] = {"objects", "snapshots", "tmp"};
    char* subdirs[3] = {NULL};
    local_ctx* c = NULL;

    rc = vs_path_join(repo_path, "repository.meta", &path);
    if (rc != VS_OK)
        goto cleanup;
    
    rc = read_all(path, &buffer, &size);
    if (rc != VS_OK)
        goto cleanup;

    free(path);
    path = NULL;

    if (size != sizeof(VS_REPO_META) - 1 || memcmp(buffer, VS_REPO_META, size) != 0)
    {
        rc = VS_ERR_CORRUPT;
        goto cleanup;
    }

    for (size_t i = 0; i < sizeof(subdir_names) / sizeof(subdir_names[0]); ++i)
    {
        rc = vs_path_join(repo_path, subdir_names[i], &subdirs[i]);
        if (rc != VS_OK)
            goto cleanup;

        int exists = 0;
        rc = directory_exists(subdirs[i], &exists);
        if (rc != VS_OK)
            goto cleanup;
        else if (exists == 0)
        {
            rc = VS_ERR_CORRUPT;
            goto cleanup;
        }
    }

    if ((c = calloc(1, sizeof *c)) == NULL)
    {
        rc = VS_ERR_NOMEM;
        goto cleanup;
    }

    c->repo_path = vs_strdup(repo_path);
    if (c->repo_path == NULL)
    {
        rc = VS_ERR_NOMEM;
        goto cleanup;
    }

    c->objects_dir = subdirs[0];
    subdirs[0] = NULL;

    c->snapshots_dir = subdirs[1];
    subdirs[1] = NULL;

    c->tmp_dir = subdirs[2];
    subdirs[2] = NULL;

    out->ctx = c;
    out->get_object = vs_local_get_object;
    out->get_snapshot = vs_local_get_snapshot;
    out->has_object = vs_local_has_object;
    out->list_snapshots = vs_local_list_snapshots;
    out->put_object = vs_local_put_object;
    out->put_snapshot = vs_local_put_snapshot;
    out->close = vs_local_close;

cleanup:
    free(path);
    free(buffer);
    free(subdirs[0]);
    free(subdirs[1]);
    free(subdirs[2]);

    if (rc != VS_OK)
        vs_local_close(c);

    return rc;
}