#include <stdlib.h>
#include <string.h>

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