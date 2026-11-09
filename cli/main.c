#include <stdio.h>
#include <string.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <vaultsync/vaultsync.h>
#include "scanner.h"
#include "hash.h"

static void print_help(void);
static int cmd_scan(const char* dir);

static int print_chunk(void* user, const void* data, size_t n);
static int cmd_hash(const char* path);

int main(int argc, char** argv)
{
    SetConsoleCP(CP_UTF8);

    if (argc > 1)
    {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
        {
            print_help();
            return 0;
        }
        else if(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)
        {
            printf("vaultsync version %s\n", vs_version());
            return 0;
        }

        if (strcmp(argv[1], "scan") == 0)
        {
            if (argc == 2)
            {
                fprintf(stderr, "missing argument <dir> for 'scan'\n");
                print_help();
                return 1;
            }

            return cmd_scan(argv[2]);
        }
        else if (strcmp(argv[1], "hash") == 0)
        {
            if (argc == 2)
            {
                fprintf(stderr, "missing argument <file> for 'hash'\n");
                print_help();
                return 1;
            }

            return cmd_hash(argv[2]);
        }
        else
        {
            fprintf(stderr, "invalid argument '%s'\n", argv[1]);
            print_help();
            return 1;
        }
    }

    return 0;
}

static void print_help(void)
{
    const char* help_msg =
        "This is a backup application.\n"
        "Usage: vaultsync_cli <args>\n"
        "<args>:\n" \
        "    --help    / -h - prints this help message\n"
        "    --version / -v - prints the application version\n"
        "      scan <dir>   - scans contents of a given directory\n"
        "      hash <file>  - prints index, size and hash of each 1 MiB chunk\n";
    
    printf("%s\n", help_msg);
}

static void get_datetime(const uint64_t mtime, char* buf, size_t buf_size)
{
    struct tm tm;
    time_t timestamp = (time_t)((mtime - 116444736000000000ULL) / 10000000ULL);
    if (localtime_s(&tm, &timestamp) != 0)
    {
        snprintf(buf, buf_size, "?");
        return;
    }
    strftime(buf, buf_size, "%Y-%m-%d %H:%M:%S", &tm);
}

static void get_size(const uint64_t size, char* buf, size_t buf_size)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = (double)size;
    size_t unit = 0;

    while (value >= 1024.0 && unit < 5)
    {
        value /= 1024.0;
        unit++;
    }

    if (value == 0.0)
        snprintf(buf, buf_size, "-");
    else if (unit == 0)
        snprintf(buf, buf_size, "%.0f %s", value, units[unit]);
    else
        snprintf(buf, buf_size, "%.2f %s", value, units[unit]);
}

static int cmd_scan(const char* dir)
{
    vs_file_list list;
    int rc = vs_scan(dir, &list);
    if (rc != VS_OK)
    {
        fprintf(stderr, "scan '%s': %s\n", dir, vs_strerr(rc));
        return 1;
    }

    for (int i = 0; i < 142; ++i)
        printf("-");
    printf("\n");
    printf("|%-12s %-20s %-5s %-100s|\n", "SIZE", "MTIME", "DIR", "RELATIVE PATH");
    for (int i = 0; i < 142; ++i)
        printf("-");
    printf("\n");
    for (size_t i = 0; i < list.count; ++i)
    {
        char date_buf[20], size_buf[12];
        get_datetime(list.items[i].mtime, date_buf, 20);
        get_size(list.items[i].size, size_buf, 12);

        printf("|%-12s %-20s %-5d %-100s|\n", size_buf, date_buf, list.items[i].is_dir ? 1 : 0, list.items[i].rel_path);
    }
    
    for (int i = 0; i < 140; ++i)
        printf("-");
    printf("\n");

    vs_file_list_free(&list);
    
    return VS_OK;
}

static int print_chunk(void* user, const void* data, size_t n)
{
    if (!user || (!data && n))
        return VS_ERR_INVALID;

    size_t* counter = user;

    char out_hex[VS_HASH_HEX_LEN + 1];
    int rc = VS_OK;

    rc = vs_hash(data, n, out_hex);
    if (rc != VS_OK)
        return rc;
    
    printf("%zu %zu %s\n", *counter, n, out_hex);
    (*counter)++;

    return rc;
}

static int cmd_hash(const char* path)
{
    size_t chunk_counter = 0;
    int rc = VS_OK;

    rc = vs_chunk_file(path, print_chunk, &chunk_counter);
    if (rc != VS_OK)
    {
        fprintf(stderr, "hash '%s': %s\n", path, vs_strerr(rc));
        return 1;
    }

    if (chunk_counter == 0)
        printf("(empty file, 0 chunks)\n");

    return 0;
}