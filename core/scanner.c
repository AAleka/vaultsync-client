#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <vaultsync/vaultsync.h>
#include "scanner.h"
#include "util.h"

void vs_file_list_init(vs_file_list* list)
{
    if (!list)
        return;
    
    memset(list, 0, sizeof(*list));
}

int vs_file_list_push(vs_file_list* list, const vs_file_entry *f)
{
    if (!list || !f || !f->rel_path)
        return VS_ERR_INVALID;
    
    if (list->count == list->capacity)
    {
        size_t temp_capacity = list->capacity ? list->capacity * 2 : 8;

        vs_file_entry* temp_items = realloc(list->items, temp_capacity * sizeof(*list->items));
        if (!temp_items)
            return VS_ERR_NOMEM;
        
        list->items = temp_items;
        list->capacity = temp_capacity;
    }

    char* path_copy = vs_strdup(f->rel_path);
    if (!path_copy)
        return VS_ERR_NOMEM;

    list->items[list->count] = *f;
    list->items[list->count].rel_path = path_copy;
    list->count++;

    return VS_OK;
}

void vs_file_list_free(vs_file_list* list)
{
    if (!list)
        return;
    
    for (size_t i = 0; i < list->count; ++i)
        free(list->items[i].rel_path);

    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static int compare_entries(const void* a, const void* b)
{
    const vs_file_entry* ea = a;
    const vs_file_entry* eb = b;
    return strcmp(ea->rel_path, eb->rel_path);
}

static int scan_dir(const char* abs, const char* rel, vs_file_list* list);

static int scan_entry(const char* abs, const char* rel, const WIN32_FIND_DATAW* fd, vs_file_list* list)
{
    if (wcscmp(fd->cFileName, L".") == 0 || wcscmp(fd->cFileName, L"..") == 0)
        return VS_OK;
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        return VS_OK;

    char* name = NULL;
    char* child_rel = NULL;
    char* child_abs = NULL;
    int rc;

    rc = vs_wide_to_utf8(fd->cFileName, &name);
    if (rc != VS_OK)
        goto cleanup;

    rc = vs_path_join(rel, name, &child_rel);
    if (rc != VS_OK)
        goto cleanup;

    rc = vs_path_join(abs, name, &child_abs);
    if (rc != VS_OK)
        goto cleanup;

    vs_file_entry e;
    e.rel_path = child_rel;
    e.is_dir   = (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e.size     = e.is_dir ? 0 : (uint64_t)fd->nFileSizeHigh << 32 | fd->nFileSizeLow;
    e.mtime    = (uint64_t)fd->ftLastWriteTime.dwHighDateTime << 32 | fd->ftLastWriteTime.dwLowDateTime;

    rc = vs_file_list_push(list, &e);
    if (rc == VS_OK && e.is_dir)
        rc = scan_dir(child_abs, child_rel, list);

cleanup:
    free(child_abs);
    free(child_rel);
    free(name);

    return rc;
}

static int scan_dir(const char* abs, const char* rel, vs_file_list* list)
{
    char* pattern = NULL;
    wchar_t* pattern_wide = NULL;
    int rc;
    WIN32_FIND_DATAW fd;
    HANDLE h = INVALID_HANDLE_VALUE;

    rc = vs_path_join(abs, "*", &pattern);
    if (rc != VS_OK)
        goto cleanup;

    rc = vs_utf8_to_wide(pattern, &pattern_wide);
    if (rc != VS_OK)
        goto cleanup;
    
    h = FindFirstFileW(pattern_wide, &fd);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            rc = VS_OK;  // empty folder
        else
            rc = vs_map_win_error(GetLastError());
        goto cleanup;
    }

    do
    {
        rc = scan_entry(abs, rel, &fd, list);
        if (rc != VS_OK)
            goto cleanup;
    } while(FindNextFileW(h, &fd));

    if (GetLastError() != ERROR_NO_MORE_FILES)
        rc = vs_map_win_error(GetLastError());

cleanup:
    if (h != INVALID_HANDLE_VALUE)
        FindClose(h);
    
    free(pattern_wide);
    free(pattern);
    
    return rc;
}

int vs_scan(const char* root, vs_file_list* out)
{
    if (!root || !*root || !out)
        return VS_ERR_INVALID;

    vs_file_list_init(out);

    wchar_t* root_wide = NULL;
    int rc = vs_utf8_to_wide(root, &root_wide);
    if (rc != VS_OK)
    {
        vs_file_list_free(out);
        return rc;
    }
    
    unsigned long attrs = GetFileAttributesW(root_wide);
    unsigned long err = GetLastError();
    free(root_wide);
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        vs_file_list_free(out);
        return vs_map_win_error(err);
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        vs_file_list_free(out);
        return VS_ERR_INVALID;  // it is a file
    }
    
    rc = scan_dir(root, "", out);
    if (rc != VS_OK)
    {
        vs_file_list_free(out);
        return rc;
    }

    qsort(out->items, out->count, sizeof(*out->items), compare_entries);

    return VS_OK;
}