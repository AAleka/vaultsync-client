#ifndef VAULTSYNC_H
#define VAULTSYNC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    VS_OK            = 0,
    VS_ERR_NOMEM     = -1,
    VS_ERR_INVALID   = -2,
    VS_ERR_IO        = -3,
    VS_ERR_NOT_FOUND = -4,
    VS_ERR_CORRUPT   = -5,
    VS_ERR_CANCELLED = -6,
} vs_error;

typedef struct vs_str_list
{
    size_t count;
    size_t capacity;
    char** items;
} vs_str_list;

const char* vs_version(void);
const char* vs_strerr(int err);

void vs_str_list_init(vs_str_list* list);
int vs_str_list_push(vs_str_list* list, const char *s);
void vs_str_list_free(vs_str_list* list);

#ifdef __cplusplus
}
#endif

#endif