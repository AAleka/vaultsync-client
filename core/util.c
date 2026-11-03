#include <vaultsync/vaultsync.h>
#include <string.h>
#include <stdlib.h>

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