#include <vaultsync/vaultsync.h>
#include <string.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include "util.h"

static char* vs_strdup(const char* s)
{
    size_t len = strlen(s);

    char* copy;
    if ((copy = malloc(len + 1)) == NULL)
    {
        return NULL;
    }

    memcpy(copy, s, len + 1);

    return copy;
}

void vs_str_list_init(vs_str_list* list)
{
    if (!list)
        return;

    memset(list, 0, sizeof(*list));
}

int vs_str_list_push(vs_str_list* list, const char *s)
{
    if (!list || !s)
        return VS_ERR_INVALID;

    if (list->count == list->capacity)
    {
        size_t temp_capacity = list->capacity ? list->capacity * 2 : 8;

        char** temp_items = realloc(list->items, temp_capacity * sizeof(*list->items));
        if (!temp_items)
            return VS_ERR_NOMEM;
        
        list->items = temp_items;
        list->capacity = temp_capacity;
    }

    char* str_copy = vs_strdup(s);
    if (!str_copy)
        return VS_ERR_NOMEM;
    
    list->items[list->count++] = str_copy;

    return VS_OK;
}

void vs_str_list_free(vs_str_list* list)
{
    if (!list)
        return;
    
    for (size_t i = 0; i < list->count; ++i)
        free(list->items[i]);
    
    free(list->items);
    memset(list, 0, sizeof(*list));
}

int vs_utf8_to_wide(const char* s, wchar_t** out)
{
    if (!s || !out)
        return VS_ERR_INVALID;

    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);  // just get len
    if (len == 0)
        return VS_ERR_INVALID;

    wchar_t* buf;
    if ((buf = malloc(len * sizeof(wchar_t))) == NULL)
        return VS_ERR_NOMEM;

    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, buf, len);  // actually convert
    if (written == 0)
    {
        free(buf);
        return VS_ERR_INVALID;
    }

    *out = buf;

    return VS_OK;
}

int vs_wide_to_utf8(const wchar_t* s, char** out)
{
    if (!s || !out)
        return VS_ERR_INVALID;
    
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
    if (len == 0)
        return VS_ERR_INVALID;
    
    char* buf;
    if ((buf = malloc(len)) == NULL)
        return VS_ERR_NOMEM;
    
    int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, buf, len, NULL, NULL);
    if (written == 0)
    {
        free(buf);
        return VS_ERR_INVALID;
    }

    *out = buf;

    return VS_OK;
}

int vs_path_join(const char* a, const char* b, char** out)
{
    if (!a || !b || !out)
        return VS_ERR_INVALID;
    
    size_t len_a = strlen(a), len_b = strlen(b);
    if (len_a == 0 && len_b == 0)
        return VS_ERR_INVALID;
    
    int need_sep = len_a > 0
            && len_b > 0
            && a[len_a - 1] != '/'
            && a[len_a - 1] != '\\';

    char* buf;
    if ((buf = malloc(len_a + need_sep + len_b + 1)) == NULL)
        return VS_ERR_NOMEM;
    
    memcpy(buf + 0, a, len_a);
    if (need_sep)
        buf[len_a] = '/';
    memcpy(buf + len_a + need_sep, b, len_b);
    buf[len_a + need_sep + len_b] = '\0';

    *out = buf;

    return VS_OK;
}

void vs_hex(const uint8_t* bytes, size_t n, char* out)
{
    if (!bytes || !out)
        return;
    
    static const char digits[] = "0123456789abcdef";

    for (size_t i = 0; i < n; ++i)
    {
        out[2*i]     = digits[ bytes[i] >> 4 ];
        out[2*i + 1] = digits[ bytes[i] & 0x0F ];
    }
    out[2*n] = '\0';
}