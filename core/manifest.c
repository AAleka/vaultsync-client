#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "vaultsync/vaultsync.h"
#include "manifest.h"
#include "util.h"

/*
* size_t len
* size_t cap
* char* data
*/
typedef struct strbuf {
    size_t len;
    size_t cap;
    char* data;
} strbuf_t;

static int reserve(strbuf_t* sb, size_t extra)
{
    if (!sb)
        return VS_ERR_INVALID;

    size_t need = sb->len + extra + 1;
    size_t temp_cap = sb->cap;
    if (need > sb->cap)
    {
        temp_cap = sb->cap ? sb->cap : 64;
        while (need >= temp_cap)
            temp_cap *= 2;
        
        
        char* temp_data = realloc(sb->data, temp_cap);
        if (!temp_data)
            return VS_ERR_NOMEM;
        
        sb->cap = temp_cap;
        sb->data = temp_data;
    }

    return VS_OK;
}

static int append_fmt(strbuf_t* sb, const char* fmt, ...)
{
    if (!sb)
        return VS_ERR_INVALID;

    int rc = VS_OK;

    va_list args, copy;
    va_start(args, fmt);
    va_copy(copy, args);

    int n = vsnprintf(NULL, 0, fmt, copy);

    va_end(copy);

    if (n < 0)
    {
        rc = VS_ERR_INVALID;
        goto cleanup;
    }

    rc = reserve(sb, n);
    if (rc != VS_OK)
        goto cleanup;
    
    if (vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args) < 0)
    {
        rc = VS_ERR_INVALID;
        goto cleanup;
    }

    sb->len += n;
    
cleanup:
    va_end(args);

    return rc;
}

static int next_line(char** p, char** line)
{
    if (!p || !line)
        return VS_ERR_INVALID;

    *line = NULL;

    if (**p == '\0')  // end of input
        return VS_OK;

    char* nl_char = strchr(*p, '\n');
    if (nl_char == NULL)
        return VS_ERR_CORRUPT;

    *nl_char = '\0';
    *line = *p;
    *p = nl_char + 1;

    if (strchr(*line, '\r') != NULL)
        return VS_ERR_CORRUPT;

    return VS_OK;
}

static int parse_u64(char** p, uint64_t* value)
{
    if (!p || !value)
        return VS_ERR_INVALID;

    *value = 0;

    if (**p < '0' || **p > '9')
        return VS_ERR_CORRUPT;

    if (**p == '0' && (*p)[1] >= '0' && (*p)[1] <= '9')
        return VS_ERR_CORRUPT;

    uint64_t d = 0;
    while (**p >= '0' && **p <= '9')
    {
        d = **p - '0';
        if (*value > (UINT64_MAX - d) / 10)
        {
            *value = 0;
            return VS_ERR_CORRUPT;
        }
        
        *value = *value * 10 + d;
        (*p)++;
    }

    return VS_OK;
}

static int expect_char(char** p, const char c)
{
    if (!p || !*p)
        return VS_ERR_INVALID;

    if (**p != c)
        return VS_ERR_CORRUPT;

    (*p)++;

    return VS_OK;
}

static int read_required_line(char** p, char** line)
{
    int rc = next_line(p, line);
    if (rc != VS_OK)
        return rc;

    if (!*line)
        return VS_ERR_CORRUPT;

    return VS_OK;
}

static int as_corrupt(int rc)
{
    return rc == VS_ERR_INVALID ? VS_ERR_CORRUPT : rc;
}

static uint64_t chunks_for_size(uint64_t size)
{
    return size / VS_CHUNK_SIZE + (size % VS_CHUNK_SIZE != 0);
}

int vs_manifest_create(const char* source, const char* created, vs_manifest_t** out)
{
    if (!out)
        return VS_ERR_INVALID;
    
    *out = NULL;
    
    if (!source || !created || strlen(created) != 16)
        return VS_ERR_INVALID;

    for (int i = 0; i < 16; ++i)
    {
        char c = created[i];
        if ((i == 8 && c != 'T') || (i == 15 && c != 'Z'))
            return VS_ERR_INVALID;
        else if (i != 8 && i != 15 && (c < '0' || c > '9'))
            return VS_ERR_INVALID;
        else
            continue;
    }

    size_t source_len = strlen(source);
    for (size_t i = 0; i < source_len; ++i)
    {
        const char c = source[i];
        if (c == '\n' || c == '\r')
            return VS_ERR_INVALID;
    }

    int rc = VS_OK;
    vs_manifest_t* m = NULL;

    if ((m = calloc(1, sizeof(*m))) == NULL)
        return VS_ERR_NOMEM;

    m->source = vs_strdup(source);
    if (!m->source)
    {
        rc = VS_ERR_NOMEM;
        goto cleanup;
    }

    memcpy(m->created, created, sizeof(m->created));

    *out = m;
    m = NULL;

cleanup:
    if (rc != VS_OK) vs_manifest_free(m);

    return rc;
}

int vs_manifest_add_entry(vs_manifest_t* man, const vs_file_entry_t* info)
{
    if (!man || !info || !info->rel_path)
        return VS_ERR_INVALID;

    size_t rel_path_len = strlen(info->rel_path);

    if (rel_path_len == 0)
        return VS_ERR_INVALID;

    for (size_t i = 0; i < rel_path_len; ++i)
    {
        const char c = info->rel_path[i];
        if (c == '\n' || c == '\r')
            return VS_ERR_INVALID;
    }

    char* rel_path = vs_strdup(info->rel_path);
    if (!rel_path)
        return VS_ERR_NOMEM;

    if (man->count == man->capacity)
    {
        size_t temp_capacity = man->capacity ? man->capacity * 2 : 8;

        vs_manifest_entry_t* temp_items = realloc(man->items, temp_capacity * sizeof(*man->items));
        if (!temp_items)
        {
            free(rel_path);
            return VS_ERR_NOMEM;
        }
    
        man->items = temp_items;
        man->capacity = temp_capacity;
    }

    man->items[man->count].info = *info;
    man->items[man->count].info.rel_path = rel_path;
    man->items[man->count].chunk_capacity = 0;
    man->items[man->count].chunk_count = 0;
    man->items[man->count].ids = NULL;
    man->count++;

    rel_path = NULL;

    return VS_OK;
}

int vs_manifest_add_chunk(vs_manifest_t* man, const char* id)
{
    if (!man || !id)
        return VS_ERR_INVALID;

    if (man->count == 0 || man->items[man->count-1].info.is_dir)
        return VS_ERR_INVALID;
    
    int rc = VS_OK;
    size_t idx = man->count-1;
    vs_manifest_entry_t* e = &man->items[idx];

    rc = validate_object_id(id);
    if (rc != VS_OK)
        return rc;

    if (e->chunk_count == e->chunk_capacity)
    {
        size_t temp_chunk_capacity = e->chunk_capacity ? e->chunk_capacity * 2 : 8;
        char (*temp_ids)[VS_HASH_HEX_LEN + 1] = realloc(e->ids, temp_chunk_capacity * sizeof(*e->ids));
        if (!temp_ids)
            return VS_ERR_NOMEM;
        
        e->ids = temp_ids;
        e->chunk_capacity = temp_chunk_capacity;
    }

    memcpy(e->ids[e->chunk_count], id, VS_HASH_HEX_LEN + 1);
    e->chunk_count++;

    return VS_OK;
}

int vs_manifest_parse(const void* data, const size_t size, vs_manifest_t** out)
{
    if (!data || !out)
        return VS_ERR_INVALID;
    else if (size == 0)
        return VS_ERR_CORRUPT;

    *out = NULL;

    if (memchr(data, '\0', size))
        return VS_ERR_CORRUPT;

    vs_manifest_t* m = NULL;
    char *buf = NULL, *src = NULL, *crt = NULL,
        *cur = NULL, *line = NULL;
    uint64_t chunks_left = 0;
    int seen_end = 0;
    int rc = VS_OK;

    if ((buf = malloc(size + 1)) == NULL)
        return VS_ERR_NOMEM;

    memcpy(buf, data, size);
    buf[size] = '\0';

    cur = buf;

    rc = read_required_line(&cur, &line);
    if (rc != VS_OK)
        goto cleanup;

    if (strcmp(line, "vaultsync-manifest 1") != 0)
    {
        rc = VS_ERR_CORRUPT;
        goto cleanup;
    }

    rc = read_required_line(&cur, &line);
    if (rc != VS_OK)
        goto cleanup;

    if (strncmp(line, "source ", 7) != 0)
    {
        rc = VS_ERR_CORRUPT;
        goto cleanup;
    }
    src = line + 7;

    rc = read_required_line(&cur, &line);
    if (rc != VS_OK)
        goto cleanup;

    if (strncmp(line, "created ", 8) != 0)
    {
        rc = VS_ERR_CORRUPT;
        goto cleanup;
    }
    crt = line + 8;

    rc = as_corrupt(vs_manifest_create(src, crt, &m));
    if (rc != VS_OK)
        goto cleanup;

    while (1)
    {
        rc = next_line(&cur, &line);
        if (rc != VS_OK)
            goto cleanup;
        else if (!line) // end of input
            break;

        if (seen_end)
        {
            rc = VS_ERR_CORRUPT;
            goto cleanup;
        }

        if (chunks_left > 0)
        {
            if (strncmp(line, "chunk ", 6) != 0)
            {
                rc = VS_ERR_CORRUPT;
                goto cleanup;
            }

            rc = as_corrupt(vs_manifest_add_chunk(m, line + 6));
            if (rc != VS_OK)
                goto cleanup;

            chunks_left--;
        }
        else if (strncmp(line, "dir ", 4) == 0)
        {
            char* p = line + 4;
            vs_file_entry_t e = {0};

            e.is_dir = 1;

            rc = parse_u64(&p, &e.size);
            if (rc != VS_OK)
                goto cleanup;

            rc = expect_char(&p, ' ');
            if (rc != VS_OK)
                goto cleanup;

            rc = parse_u64(&p, &e.mtime);
            if (rc != VS_OK)
                goto cleanup;

            rc = expect_char(&p, ' ');
            if (rc != VS_OK)
                goto cleanup;

            e.rel_path = p;

            rc = as_corrupt(vs_manifest_add_entry(m, &e));
            if (rc != VS_OK)
                goto cleanup;
        }
        else if (strncmp(line, "file ", 5) == 0)
        {
            char* p = line + 5;
            vs_file_entry_t e = {0};
            uint64_t chunk_count = 0;

            e.is_dir = 0;

            rc = parse_u64(&p, &e.size);
            if (rc != VS_OK)
                goto cleanup;

            rc = expect_char(&p, ' ');
            if (rc != VS_OK)
                goto cleanup;

            rc = parse_u64(&p, &e.mtime);
            if (rc != VS_OK)
                goto cleanup;

            rc = expect_char(&p, ' ');
            if (rc != VS_OK)
                goto cleanup;

            rc = parse_u64(&p, &chunk_count);
            if (rc != VS_OK)
                goto cleanup;

            rc = expect_char(&p, ' ');
            if (rc != VS_OK)
                goto cleanup;

            if (chunk_count != chunks_for_size(e.size))
            {
                rc = VS_ERR_CORRUPT;
                goto cleanup;
            }

            e.rel_path = p;

            rc = as_corrupt(vs_manifest_add_entry(m, &e));
            if (rc != VS_OK)
                goto cleanup;

            chunks_left = chunk_count;
        }
        else if (strncmp(line, "end ", 4) == 0)
        {
            char* p = line + 4;
            uint64_t count = 0;

            rc = parse_u64(&p, &count);
            if (rc != VS_OK)
                goto cleanup;

            if (*p != '\0' || count != (uint64_t)m->count)
            {
                rc = VS_ERR_CORRUPT;
                goto cleanup;
            }

            seen_end = 1;
        }
        else
        {
            rc = VS_ERR_CORRUPT;
            goto cleanup;
        }
    }

    if (!seen_end || chunks_left != 0)
    {
        rc = VS_ERR_CORRUPT;
        goto cleanup;
    }

    *out = m;

cleanup:
    free(buf);
    if (rc != VS_OK) 
        vs_manifest_free(m);

    return rc;
}

int vs_manifest_serialize(const vs_manifest_t* man, void** data, size_t* size)
{
    if (!man || !data || !size)
        return VS_ERR_INVALID;
    
    *data = NULL;
    *size = 0;

    strbuf_t sb = {0};
    int rc = VS_OK;

    rc = append_fmt(&sb, "vaultsync-manifest 1\n");
    if (rc != VS_OK)
        goto cleanup;

    rc = append_fmt(&sb, "source %s\n", man->source);
    if (rc != VS_OK)
        goto cleanup;

    rc = append_fmt(&sb, "created %s\n", man->created);
    if (rc != VS_OK)
        goto cleanup;

    for (size_t i = 0; i < man->count; ++i)
    {
        const vs_manifest_entry_t* e = &man->items[i];
        if (e->info.is_dir)
        {
            rc = append_fmt(&sb, "dir %" PRIu64 " %" PRIu64 " %s\n", 
                e->info.size, e->info.mtime, e->info.rel_path);
            if (rc != VS_OK)
                goto cleanup;
        }
        else
        {
            rc = append_fmt(&sb, "file %" PRIu64 " %" PRIu64 " %zu %s\n", e->info.size, 
                e->info.mtime, e->chunk_count, e->info.rel_path);
            if (rc != VS_OK)
                goto cleanup;
            
            size_t chunk_count = e->chunk_count;
            for (size_t j = 0; j < chunk_count; ++j)
            {
                rc = append_fmt(&sb, "chunk %s\n", e->ids[j]);
                if (rc != VS_OK)
                    goto cleanup;
            }
        }
    }

    rc = append_fmt(&sb, "end %zu\n", man->count);
    if (rc != VS_OK)
        goto cleanup;

    *data = sb.data;
    *size = sb.len;

cleanup:
    if (rc != VS_OK) free(sb.data);

    return rc;
}

void vs_manifest_free(vs_manifest_t* man)
{
    if (!man)
        return;

    for (size_t i = 0; i < man->count; ++i)
    {
        free(man->items[i].info.rel_path);
        free(man->items[i].ids);
    }

    free(man->source);
    free(man->items);
    free(man);
}