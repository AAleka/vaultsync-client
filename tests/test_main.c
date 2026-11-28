#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <vaultsync/vaultsync.h>
#include "util.h"
#include "scanner.h"
#include "hash.h"
#include "storage.h"
#include "manifest.h"

/* MSVC Debug builds only: the CRT debug heap tracks every allocation. */
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define VS_CRT_DEBUG 1
/* ASan replaces the allocator, so the debug heap sees nothing: configure
   with -DVS_ASAN=OFF to get leak checks. */
#ifndef __SANITIZE_ADDRESS__
#define VS_LEAK_CHECK 1
#endif
#endif

static int g_failures = 0;

#ifndef VS_TEST_DIR
#define VS_TEST_DIR "build"
#endif

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
                __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while(0)

#define CHECK_STR_EQ(a, b) \
    do { \
        if (!(a) || !(b)) { \
            fprintf(stderr, "%s:%d: CHECK_STR_EQ failed: passed NULL\n", \
                __FILE__, __LINE__); \
            g_failures++; \
        } \
        else if (strcmp((a), (b)) != 0) { \
            fprintf(stderr, "%s:%d: CHECK_STR_EQ failed: %s != %s\n", \
                __FILE__, __LINE__, (a), (b)); \
            g_failures++; \
        } \
    } while(0)

#define CHECK_STR_NOTEQ(a, b) \
    do { \
        if (!(a) || !(b)) { \
            fprintf(stderr, "%s:%d: CHECK_STR_NOTEQ failed: passed NULL\n", \
                __FILE__, __LINE__); \
            g_failures++; \
        } \
        else if (strcmp((a), (b)) == 0) { \
            fprintf(stderr, "%s:%d: CHECK_STR_NOTEQ failed: %s != %s\n", \
                __FILE__, __LINE__, (a), (b)); \
            g_failures++; \
        } \
    } while(0)

typedef struct {
    const char* name;
    void (*func)(void);
} Test;

#define TEST(fn) { #fn, fn }

static void test_strerr(void)
{
    CHECK_STR_EQ(vs_strerr(VS_OK), "Success");
    CHECK(vs_strerr(VS_ERR_NOMEM) != NULL);
    CHECK(vs_strerr(VS_ERR_INVALID) != NULL);
    CHECK(vs_strerr(VS_ERR_IO) != NULL);
    CHECK(vs_strerr(VS_ERR_NOT_FOUND) != NULL);
    CHECK(vs_strerr(VS_ERR_CORRUPT) != NULL);
    CHECK(vs_strerr(VS_ERR_CANCELLED) != NULL);
    CHECK(vs_strerr(VS_ERR_NOMEM)[0] != '\0');
    CHECK(vs_strerr(VS_ERR_INVALID)[0] != '\0');
    CHECK(vs_strerr(VS_ERR_IO)[0] != '\0');
    CHECK(vs_strerr(VS_ERR_NOT_FOUND)[0] != '\0');
    CHECK(vs_strerr(VS_ERR_CORRUPT)[0] != '\0');
    CHECK(vs_strerr(VS_ERR_CANCELLED)[0] != '\0');
    CHECK(vs_strerr(42) != NULL);
    CHECK_STR_EQ(vs_strerr(42), "unknown error code");
} 

static void test_vs_str_list_init(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_str_list_free(&list);

    vs_str_list_init(NULL);
}

static void test_vs_str_list_push_basic(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    const char* a = "aa";
    const char* b = "bb";
    const char* c = "cc";

    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(vs_str_list_push(&list, c) == VS_OK);

    CHECK(list.count == 3);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0], a);
    CHECK_STR_EQ(list.items[1], b);
    CHECK_STR_EQ(list.items[2], c);

    vs_str_list_free(&list);
}

static void test_vs_str_list_push_copies(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    char buf[] = "abc";
    CHECK(vs_str_list_push(&list, buf) == VS_OK);
    CHECK(list.items[0] != buf);

    buf[0] = 'd';
    CHECK_STR_NOTEQ(list.items[0], buf);

    vs_str_list_free(&list);
}

static void test_vs_str_list_growth(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    const char* a = "aa";
    const char* b = "bb";
    const char* c = "cc";

    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(vs_str_list_push(&list, c) == VS_OK);
    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(vs_str_list_push(&list, c) == VS_OK);
    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(list.capacity == 8);
    CHECK(vs_str_list_push(&list, c) == VS_OK);

    CHECK(list.count == 9);
    CHECK(list.capacity == 16);
    for (size_t i = 0; i < list.count; ++i)
    {
        if (i % 3 == 0)
            CHECK_STR_EQ(list.items[i], a);
        else if (i % 3 == 1)
            CHECK_STR_EQ(list.items[i], b);
        else if (i % 3 == 2)
            CHECK_STR_EQ(list.items[i], c);
    }

    vs_str_list_free(&list);
}

static void test_vs_str_list_many(void)
{    
    vs_str_list_t list;
    vs_str_list_init(&list);

    const int num_files = 100000;
    
    for (int i = 0; i < num_files; ++i)
    {
        char file_name[64];
        snprintf(file_name, 64, "file_%d", i);
        CHECK(vs_str_list_push(&list, file_name) == VS_OK);
    }

    CHECK(list.count == num_files);
    CHECK(list.capacity == 131072);
    CHECK_STR_EQ(list.items[0], "file_0");
    CHECK_STR_EQ(list.items[50000], "file_50000");
    CHECK_STR_EQ(list.items[99999], "file_99999");

    vs_str_list_free(&list);
}

static void test_vs_str_list_invalid(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    CHECK(vs_str_list_push(NULL, "x") == VS_ERR_INVALID);
    CHECK(vs_str_list_push(&list, NULL) == VS_ERR_INVALID);
    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_str_list_free(&list);
}

static void test_vs_str_list_free(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    const char* a = "aa";
    const char* b = "bb";
    const char* c = "cc";

    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(vs_str_list_push(&list, c) == VS_OK);

    CHECK(list.count == 3);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0], a);
    CHECK_STR_EQ(list.items[1], b);
    CHECK_STR_EQ(list.items[2], c);

    vs_str_list_free(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_str_list_free(&list);

    CHECK(vs_str_list_push(&list, a) == VS_OK);
    CHECK(vs_str_list_push(&list, b) == VS_OK);
    CHECK(vs_str_list_push(&list, c) == VS_OK);

    CHECK(list.count == 3);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0], a);
    CHECK_STR_EQ(list.items[1], b);
    CHECK_STR_EQ(list.items[2], c);

    vs_str_list_free(&list);
}

static void test_vs_str_list_empty_string(void)
{
    vs_str_list_t list;
    vs_str_list_init(&list);

    const char* a = "";

    CHECK(vs_str_list_push(&list, a) == VS_OK);

    CHECK(list.count == 1);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0], a);

    vs_str_list_free(&list);
}

static void test_vs_utf8_to_wide(void)
{
    wchar_t* w = NULL;

    CHECK(vs_utf8_to_wide("\xC3\x9C\xE6\x97\xA5", &w) == VS_OK);
    CHECK(w != NULL);
    CHECK(wcslen(w) == 2);
    CHECK(w[0] == 0x00DC);
    CHECK(w[1] == 0x65E5);
    free(w);
    w = NULL;

    CHECK(vs_utf8_to_wide("\xF0\x9F\x98\x80", &w) == VS_OK);   /* 😀 */
    CHECK(w != NULL);
    CHECK(wcslen(w) == 2);
    CHECK(w[0] == 0xD83D);
    CHECK(w[1] == 0xDE00);
    free(w);
    w = NULL;

    CHECK(vs_utf8_to_wide("", &w) == VS_OK);
    CHECK(w != NULL);
    CHECK(wcslen(w) == 0);
    free(w);
    w = NULL;

    CHECK(vs_utf8_to_wide("\xFF", &w) == VS_ERR_INVALID);
    CHECK(vs_utf8_to_wide(NULL, &w) == VS_ERR_INVALID);
    CHECK(vs_utf8_to_wide("a", NULL) == VS_ERR_INVALID);
    CHECK(w == NULL);
}

static void test_vs_wide_to_utf8(void)
{
    const wchar_t in[] = { 0x00DC, 0x65E5, 0 };   /* "Ü日" */
    char* out = NULL;

    CHECK(vs_wide_to_utf8(in, &out) == VS_OK);
    CHECK(out != NULL);
    CHECK(strlen(out) == 5);
    CHECK_STR_EQ(out, "\xC3\x9C\xE6\x97\xA5");
    free(out);
    out = NULL;

    const wchar_t lone[] = { 0xD800, 0 };
    CHECK(vs_wide_to_utf8(lone, &out) == VS_ERR_INVALID);
    CHECK(vs_wide_to_utf8(NULL, &out) == VS_ERR_INVALID);
    CHECK(vs_wide_to_utf8(in, NULL) == VS_ERR_INVALID);
    CHECK(out == NULL);
}

static void test_vs_utf8_to_wide_to_utf8(void)
{
    const char* in[] = {
        "Fotos/\xC3\x9Cn\xC3\xAF" "c\xC3\xB8" "d\xC3\xA9/\xE6\x97\xA5\xE6\x9C\xAC.txt",   /* "Fotos/Ünïcødé/日本.txt" */
        "",
    };

    for (size_t i = 0; i < sizeof(in) / sizeof(in[0]); ++i)
    {
        wchar_t* w = NULL;
        char* out = NULL;

        CHECK(vs_utf8_to_wide(in[i], &w) == VS_OK);
        CHECK(vs_wide_to_utf8(w, &out) == VS_OK);
        CHECK_STR_EQ(out, in[i]);

        free(w);
        free(out);
    }
}

static void test_vs_path_join(void)
{
    char* result = NULL;

    CHECK(vs_path_join("/root/", "home/test.txt", &result) == VS_OK);
    CHECK_STR_EQ(result, "/root/home/test.txt");
    free(result);
    result = NULL;

    CHECK(vs_path_join("/root", "home/test.txt", &result) == VS_OK);
    CHECK_STR_EQ(result, "/root/home/test.txt");
    free(result);
    result = NULL;

    CHECK(vs_path_join("/root/home/test.txt", "", &result) == VS_OK);
    CHECK_STR_EQ(result, "/root/home/test.txt");
    free(result);
    result = NULL;

    CHECK(vs_path_join("/root/home/test.txt", NULL, &result) == VS_ERR_INVALID);
    CHECK(vs_path_join("/root/home/test.txt", "", NULL) == VS_ERR_INVALID);
    CHECK(vs_path_join(NULL, "/root/home/test.txt", &result) == VS_ERR_INVALID);

    CHECK(vs_path_join("", "/root/home/test.txt", &result) == VS_OK);
    CHECK_STR_EQ(result, "/root/home/test.txt");
    free(result);
    result = NULL;

    CHECK(vs_path_join("C:/Backups", "objects", &result) == VS_OK);
    CHECK_STR_EQ(result, "C:/Backups/objects");
    free(result);
    result = NULL;

    CHECK(vs_path_join("C:\\Backups\\", "x", &result) == VS_OK);
    CHECK_STR_EQ(result, "C:\\Backups\\x");
    free(result);
    result = NULL;

    CHECK(vs_path_join("C:", "x", &result) == VS_OK);
    CHECK_STR_EQ(result, "C:/x");
    free(result);
    result = NULL;

    CHECK(vs_path_join("", "", &result) == VS_ERR_INVALID);
    CHECK(result == NULL);
}

static void test_vs_hex(void)
{
    uint8_t b[] = { 0x00, 0xAB, 0xFF };
    char out[7];
    vs_hex(b, 3, out);
    CHECK_STR_EQ(out, "00abff");

    char empty[1];
    vs_hex(b, 0, empty);
    CHECK_STR_EQ(empty, "");

    uint8_t all[] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    char out2[17];
    vs_hex(all, 8, out2);
    CHECK_STR_EQ(out2, "0123456789abcdef");
}

static void test_vs_file_list_init(void)
{
    vs_file_list_t list;
    vs_file_list_init(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_file_list_free(&list);

    vs_file_list_init(NULL);
}

static void test_vs_file_list_push(void)
{
    vs_file_list_t list;
    vs_file_list_init(&list);

    const int num_entries = 100000;
    
    for (int i = 0; i < num_entries; ++i)
    {
        vs_file_entry_t f;
        char rel_path[64];
        if (i % 3 == 0)
        {
            snprintf(rel_path, 64, "/test/dir_%d", i);
            f.is_dir = 1;
            f.rel_path = rel_path;
            f.size = 4324235;
            f.mtime = 54356546;
        }
        else
        {
            snprintf(rel_path, 64, "/test/file_%d", i);
            f.is_dir = 0;
            f.rel_path = rel_path;
            f.size = 3424234;
            f.mtime = 6457568758;
        }

        CHECK(vs_file_list_push(&list, &f) == VS_OK);
    }

    CHECK(list.capacity == 131072);
    CHECK_STR_EQ(list.items[1].rel_path, "/test/file_1");
    CHECK(list.items[1].is_dir == 0);
    CHECK(list.items[1].size == 3424234);
    CHECK(list.items[1].mtime == 6457568758);
    CHECK_STR_EQ(list.items[99999].rel_path, "/test/dir_99999");
    CHECK(list.items[99999].is_dir == 1);
    CHECK(list.items[99999].size == 4324235);
    CHECK(list.items[99999].mtime == 54356546);

    vs_file_list_free(&list);
}

static void test_vs_file_list_free(void)
{
    vs_file_list_t list;
    vs_file_list_init(&list);

    vs_file_entry_t f = {5435345, 3123123213, 0, "/test/test.txt"};
    vs_file_entry_t d = {432423445, 3331221313, 1, "/test/tests/"};

    CHECK(vs_file_list_push(&list, &f) == VS_OK);
    CHECK(vs_file_list_push(&list, &d) == VS_OK);

    CHECK(list.count == 2);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0].rel_path, "/test/test.txt");
    CHECK_STR_EQ(list.items[1].rel_path, "/test/tests/");

    vs_file_list_free(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_file_list_free(&list);

    CHECK(vs_file_list_push(&list, &f) == VS_OK);
    CHECK(vs_file_list_push(&list, &d) == VS_OK);

    CHECK(list.count == 2);
    CHECK(list.capacity == 8);
    CHECK_STR_EQ(list.items[0].rel_path, "/test/test.txt");
    CHECK_STR_EQ(list.items[1].rel_path, "/test/tests/");

    vs_file_list_free(&list);
}

static int make_dir(const char* root, const char* rel)
{
    char* path = NULL;
    wchar_t* w = NULL;
    int ok = vs_path_join(root, rel, &path) == VS_OK
          && vs_utf8_to_wide(path, &w) == VS_OK
          && (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    free(w);
    free(path);
    return ok;
}

static int make_file(const char* root, const char* rel, unsigned long size)
{
    char* path = NULL;
    wchar_t* w = NULL;
    int ok = vs_path_join(root, rel, &path) == VS_OK
          && vs_utf8_to_wide(path, &w) == VS_OK;
    free(path);
    if (!ok)
        return 0;

    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    char buf[64];
    memset(buf, 'x', sizeof(buf));
    unsigned long written = 0;
    ok = size <= sizeof(buf) && WriteFile(h, buf, size, &written, NULL) && written == size;
    CloseHandle(h);
    return ok;
}

/* Sorted scan walked backwards gives children before parents. */
static void remove_tree(const char* root)
{
    vs_file_list_t list;
    if (vs_scan(root, &list) != VS_OK)
        return;

    for (size_t i = list.count; i-- > 0; )
    {
        char* path = NULL;
        wchar_t* w = NULL;
        if (vs_path_join(root, list.items[i].rel_path, &path) == VS_OK
            && vs_utf8_to_wide(path, &w) == VS_OK)
        {
            if (list.items[i].is_dir)
                RemoveDirectoryW(w);
            else
                DeleteFileW(w);
        }
        free(w);
        free(path);
    }
    vs_file_list_free(&list);

    wchar_t* w = NULL;
    if (vs_utf8_to_wide(root, &w) == VS_OK)
        RemoveDirectoryW(w);
    free(w);
}

#define UNI_DIR  "\xC3\x9Cn\xC3\xAF" "c\xC3\xB8" "d\xC3\xA9"     /* "Ünïcødé" */
#define UNI_FILE "\xE6\x97\xA5\xE6\x9C\xAC.txt"                 /* "日本.txt" */

static void test_vs_scan(void)
{
    const char* root = VS_TEST_DIR "/test_vs_scan";

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(make_dir(root, ""));
    CHECK(make_dir(root, "empty"));
    CHECK(make_dir(root, "nested"));
    CHECK(make_dir(root, "nested/a"));
    CHECK(make_dir(root, "nested/a/b"));
    CHECK(make_dir(root, UNI_DIR));
    CHECK(make_file(root, "nested/a/b/c.txt", 3));
    CHECK(make_file(root, "zero.txt", 0));
    CHECK(make_file(root, "b.txt", 1));
    CHECK(make_file(root, "a.txt", 2));
    CHECK(make_file(root, UNI_DIR "/" UNI_FILE, 5));

    const vs_file_entry_t expected[] = {
        { 2, 0, 0, "a.txt" },
        { 1, 0, 0, "b.txt" },
        { 0, 0, 1, "empty" },
        { 0, 0, 1, "nested" },
        { 0, 0, 1, "nested/a" },
        { 0, 0, 1, "nested/a/b" },
        { 3, 0, 0, "nested/a/b/c.txt" },
        { 0, 0, 0, "zero.txt" },
        { 0, 0, 1, UNI_DIR },
        { 5, 0, 0, UNI_DIR "/" UNI_FILE },
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);

    vs_file_list_t list;
    CHECK(vs_scan(root, &list) == VS_OK);
    CHECK(list.count == n);
    for (size_t i = 0; i < n && i < list.count; ++i)
    {
        CHECK_STR_EQ(list.items[i].rel_path, expected[i].rel_path);
        CHECK(list.items[i].size == expected[i].size);
        CHECK(list.items[i].is_dir == expected[i].is_dir);
        CHECK(list.items[i].mtime != 0);
        CHECK(strchr(list.items[i].rel_path, '\\') == NULL);
    }
    vs_file_list_free(&list);

    char* empty = NULL;
    CHECK(vs_path_join(root, "empty", &empty) == VS_OK);
    CHECK(vs_scan(empty, &list) == VS_OK);
    CHECK(list.count == 0);
    vs_file_list_free(&list);
    free(empty);

    char* file = NULL;
    CHECK(vs_path_join(root, "a.txt", &file) == VS_OK);
    CHECK(vs_scan(file, &list) == VS_ERR_INVALID);
    free(file);
    CHECK(vs_scan(VS_TEST_DIR "/no_such_dir", &list) == VS_ERR_NOT_FOUND);
    CHECK(vs_scan("", &list) == VS_ERR_INVALID);
    CHECK(vs_scan(NULL, &list) == VS_ERR_INVALID);
    CHECK(vs_scan(root, NULL) == VS_ERR_INVALID);

    remove_tree(root);
}

static void test_vs_hash(void)
{
    static const struct {
        size_t len;
        const char* expected;
    } cases[] = {
        {
            0,
            "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"
        },
        {
            1,
            "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213"
        },
        {
            63,
            "e9bc37a594daad83be9470df7f7b3798297c3d834ce80ba85d6e207627b7db7b"
        },
        {
            64,
            "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98"
        },
        {
            65,
            "de1e5fa0be70df6d2be8fffd0e99ceaa8eb6e8c93a63f2d8d1c30ecb6b263dee"
        },
    };

    uint8_t buf[65];
    char out_hex[VS_HASH_HEX_LEN + 1];

    for (size_t i = 0; i < sizeof(buf); ++i)
        buf[i] = (uint8_t)(i % 251);

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        memset(out_hex, 0xAA, sizeof(out_hex));

        CHECK(vs_hash(buf, cases[i].len, out_hex) == VS_OK);
        CHECK_STR_EQ(out_hex, cases[i].expected);
        CHECK(strlen(out_hex) == VS_HASH_HEX_LEN);
    }

    CHECK(vs_hash(NULL, 1, out_hex) == VS_ERR_INVALID);
    CHECK(vs_hash(buf, sizeof(buf), NULL) == VS_ERR_INVALID);

    CHECK(vs_hash(NULL, 0, out_hex) == VS_OK);
    CHECK_STR_EQ(out_hex, cases[0].expected);
}

#define MAX_REC_CHUNKS 8

/* What the test callback saw; passed to vs_chunk_file as `user`. */
typedef struct {
    size_t count;                                       /* callbacks received */
    size_t sizes[MAX_REC_CHUNKS];
    char   hashes[MAX_REC_CHUNKS][VS_HASH_HEX_LEN + 1];
    size_t stop_after;                                  /* 0 = never; else cancel on that call */
} chunk_rec;

static int on_chunk_cb(void* user, const void* data, size_t n)
{
    chunk_rec* rec = user;
    if (rec->count < MAX_REC_CHUNKS)
    {
        rec->sizes[rec->count] = n;
        vs_hash(data, n, rec->hashes[rec->count]);
    }
    rec->count++;

    if (rec->stop_after && rec->count == rec->stop_after)
        return VS_ERR_CANCELLED;
    return VS_OK;
}

static int on_chunk_ignore_user(void* user, const void* data, size_t n)
{
    (void)user;
    (void)data;
    (void)n;
    return VS_OK;
}

/* Byte at file offset i is i % 251, so every 1 MiB chunk has different content
   and a chunk read from the wrong offset gets the wrong hash. */
static void fill_pattern(uint8_t* buf, size_t n, size_t offset)
{
    for (size_t i = 0; i < n; ++i)
        buf[i] = (uint8_t)((offset + i) % 251);
}

static int make_pattern_file(const char* path, size_t size)
{
    wchar_t* w = NULL;
    if (vs_utf8_to_wide(path, &w) != VS_OK)
        return 0;

    HANDLE h = CreateFileW(w, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE)
        return 0;

    static uint8_t block[64 * 1024];
    size_t done = 0;
    int ok = 1;
    while (ok && done < size)
    {
        size_t want = size - done < sizeof(block) ? size - done : sizeof(block);
        unsigned long written = 0;
        fill_pattern(block, want, done);
        ok = WriteFile(h, block, (DWORD)want, &written, NULL) && written == want;
        done += want;
    }
    CloseHandle(h);
    return ok;
}

/* Writes a pattern file of `size` bytes, chunks it, and checks the number of
   chunks, each chunk's size, and each chunk's hash. */
static void check_chunks(const char* root, const char* name, size_t size)
{
    char* path = NULL;
    CHECK(vs_path_join(root, name, &path) == VS_OK);
    CHECK(make_pattern_file(path, size));

    chunk_rec rec;
    memset(&rec, 0, sizeof(rec));
    CHECK(vs_chunk_file(path, on_chunk_cb, &rec) == VS_OK);

    size_t expected = (size + VS_CHUNK_SIZE - 1) / VS_CHUNK_SIZE;
    CHECK(rec.count == expected);
    if (rec.count != expected)
        fprintf(stderr, "  %s (%zu bytes): got %zu chunks, expected %zu\n",
            name, size, rec.count, expected);

    uint8_t* want = malloc(VS_CHUNK_SIZE);
    CHECK(want != NULL);
    for (size_t i = 0; want && i < expected && i < MAX_REC_CHUNKS; ++i)
    {
        size_t off = i * VS_CHUNK_SIZE;
        size_t len = size - off < VS_CHUNK_SIZE ? size - off : VS_CHUNK_SIZE;
        char hex[VS_HASH_HEX_LEN + 1];

        CHECK(rec.sizes[i] == len);
        fill_pattern(want, len, off);
        CHECK(vs_hash(want, len, hex) == VS_OK);
        CHECK_STR_EQ(rec.hashes[i], hex);
    }
    free(want);
    free(path);
}

static void test_vs_chunk_file(void)
{
    const char* root = VS_TEST_DIR "/test_vs_chunk_file";

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(make_dir(root, ""));

    /* chunk counts and boundaries */
    check_chunks(root, "empty.bin", 0);
    check_chunks(root, "small.bin", 1000);
    check_chunks(root, "under.bin", VS_CHUNK_SIZE - 1);
    check_chunks(root, "exact.bin", VS_CHUNK_SIZE);
    check_chunks(root, "over.bin",  VS_CHUNK_SIZE + 1);
    check_chunks(root, "3_5.bin",   3 * VS_CHUNK_SIZE + VS_CHUNK_SIZE / 2);
    check_chunks(root, UNI_FILE,    10);

    char* path = NULL;
    CHECK(vs_path_join(root, "3_5.bin", &path) == VS_OK);

    /* callback error stops reading and is returned as-is */
    chunk_rec rec;
    memset(&rec, 0, sizeof(rec));
    rec.stop_after = 1;
    CHECK(vs_chunk_file(path, on_chunk_cb, &rec) == VS_ERR_CANCELLED);
    CHECK(rec.count == 1);

    /* user is the caller's business; NULL is allowed */
    CHECK(vs_chunk_file(path, on_chunk_ignore_user, NULL) == VS_OK);

    /* errors: callback must never be called */
    char* missing = NULL;
    CHECK(vs_path_join(root, "missing.bin", &missing) == VS_OK);
    memset(&rec, 0, sizeof(rec));
    CHECK(vs_chunk_file(missing, on_chunk_cb, &rec) == VS_ERR_NOT_FOUND);
    CHECK(vs_chunk_file(root, on_chunk_cb, &rec) != VS_OK);    /* a directory */
    CHECK(vs_chunk_file(NULL, on_chunk_cb, &rec) == VS_ERR_INVALID);
    CHECK(vs_chunk_file(path, NULL, &rec) == VS_ERR_INVALID);
    CHECK(rec.count == 0);

    free(missing);
    free(path);
    remove_tree(root);
}

#define REPO_META "vaultsync-repo 1\n"

/* 1 if the file at root/rel holds exactly n bytes equal to data. */
static int file_equals(const char* root, const char* rel, const void* data, size_t n)
{
    char* path = NULL;
    if (vs_path_join(root, rel, &path) != VS_OK)
        return 0;

    FILE* f = NULL;
    fopen_s(&f, path, "rb");
    free(path);
    if (!f)
        return 0;

    char buf[256];
    size_t got = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return got == n && memcmp(buf, data, n) == 0;
}

/* Number of entries below root/rel, or (size_t)-1 if it can't be scanned. */
static size_t count_entries(const char* root, const char* rel)
{
    char* path = NULL;
    vs_file_list_t list;
    size_t count = (size_t)-1;

    if (vs_path_join(root, rel, &path) == VS_OK && vs_scan(path, &list) == VS_OK)
    {
        count = list.count;
        vs_file_list_free(&list);
    }
    free(path);
    return count;
}

static int remove_dir(const char* root, const char* rel)
{
    char* path = NULL;
    wchar_t* w = NULL;
    int ok = vs_path_join(root, rel, &path) == VS_OK
          && vs_utf8_to_wide(path, &w) == VS_OK
          && RemoveDirectoryW(w);
    free(w);
    free(path);
    return ok;
}

static void test_vs_local_init(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_init";

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));

    /* fresh repo: folders, meta file with exact bytes, nothing left in tmp */
    CHECK(vs_local_init(root) == VS_OK);
    CHECK(count_entries(root, "objects") == 0);
    CHECK(count_entries(root, "snapshots") == 0);
    CHECK(count_entries(root, "tmp") == 0);
    CHECK(file_equals(root, "repository.meta", REPO_META, sizeof(REPO_META) - 1));

    /* second init refuses and leaves the repo untouched */
    CHECK(vs_local_init(root) != VS_OK);
    CHECK(file_equals(root, "repository.meta", REPO_META, sizeof(REPO_META) - 1));
    CHECK(count_entries(root, "tmp") == 0);

    /* parent folder doesn't exist: CreateDirectoryW creates one level only */
    CHECK(vs_local_init(VS_TEST_DIR "/no_such_parent/repo") == VS_ERR_NOT_FOUND);

    CHECK(vs_local_init(NULL) == VS_ERR_INVALID);

    remove_tree(root);
}

static void test_vs_local_open(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_open";
    vs_storage_t s;

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(make_dir(root, ""));

    /* good repo: every function pointer filled, close succeeds */
    CHECK(vs_local_init(VS_TEST_DIR "/test_vs_local_open/good") == VS_OK);
    int rc = vs_local_open(VS_TEST_DIR "/test_vs_local_open/good", &s);
    CHECK(rc == VS_OK);
    if (rc == VS_OK)    /* on failure s may not be usable; don't crash the runner */
    {
        CHECK(s.ctx != NULL);
        CHECK(s.has_object != NULL);
        CHECK(s.put_object != NULL);
        CHECK(s.get_object != NULL);
        CHECK(s.put_snapshot != NULL);
        CHECK(s.get_snapshot != NULL);
        CHECK(s.list_snapshots != NULL);
        CHECK(s.close != NULL);
        CHECK(s.close(s.ctx) == VS_OK);
    }

    /* not a repo: no meta file -> NOT_FOUND and out is left all NULL */
    CHECK(make_dir(root, "empty"));
    memset(&s, 0xAB, sizeof(s));
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/empty", &s) == VS_ERR_NOT_FOUND);
    CHECK(s.ctx == NULL && s.close == NULL && s.has_object == NULL);
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/missing", &s) == VS_ERR_NOT_FOUND);

    /* meta with wrong length, and with right length but wrong bytes */
    CHECK(vs_local_init(VS_TEST_DIR "/test_vs_local_open/short") == VS_OK);
    CHECK(make_file(root, "short/repository.meta", 5));
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/short", &s) == VS_ERR_CORRUPT);

    CHECK(vs_local_init(VS_TEST_DIR "/test_vs_local_open/samelen") == VS_OK);
    CHECK(make_file(root, "samelen/repository.meta", sizeof(REPO_META) - 1));
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/samelen", &s) == VS_ERR_CORRUPT);

    /* a required folder is missing, or is a file instead of a folder */
    CHECK(vs_local_init(VS_TEST_DIR "/test_vs_local_open/nosnap") == VS_OK);
    CHECK(remove_dir(root, "nosnap/snapshots"));
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/nosnap", &s) == VS_ERR_CORRUPT);
    CHECK(make_file(root, "nosnap/snapshots", 1));
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/nosnap", &s) == VS_ERR_CORRUPT);

    CHECK(vs_local_open(NULL, &s) == VS_ERR_INVALID);
    CHECK(vs_local_open(VS_TEST_DIR "/test_vs_local_open/good", NULL) == VS_ERR_INVALID);

    remove_tree(root);
}

static void test_vs_local_has_object(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_has_object";
    vs_storage_t s;
    int exists = -1;

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(vs_local_init(root) == VS_OK);
    int rc = vs_local_open(root, &s);
    CHECK(rc == VS_OK);
    if (rc != VS_OK)
    {
        remove_tree(root);
        return;
    }

    char id[VS_HASH_HEX_LEN + 1];
    CHECK(vs_hash("hello", 5, id) == VS_OK);

    /* not stored yet: a successful "no", not an error */
    CHECK(s.has_object(s.ctx, id, &exists) == VS_OK);
    CHECK(exists == 0);

    /* put a file exactly where the layout says objects/<id[0..1]>/<id> goes */
    char sub[3] = { id[0], id[1], '\0' };
    char* objects = NULL;
    CHECK(vs_path_join(root, "objects", &objects) == VS_OK);
    CHECK(make_dir(objects, sub));
    char* rel = NULL;
    CHECK(vs_path_join(sub, id, &rel) == VS_OK);
    CHECK(make_file(objects, rel, 5));

    exists = -1;
    CHECK(s.has_object(s.ctx, id, &exists) == VS_OK);
    CHECK(exists == 1);

    /* a folder where an object should be means the repo is damaged */
    char id2[VS_HASH_HEX_LEN + 1];
    CHECK(vs_hash("world", 5, id2) == VS_OK);
    char sub2[3] = { id2[0], id2[1], '\0' };
    char* rel2 = NULL;
    CHECK(make_dir(objects, sub2));
    CHECK(vs_path_join(sub2, id2, &rel2) == VS_OK);
    CHECK(make_dir(objects, rel2));
    CHECK(s.has_object(s.ctx, id2, &exists) == VS_ERR_CORRUPT);

    /* ids that must never become paths */
    char upper[VS_HASH_HEX_LEN + 1];
    char short_id[VS_HASH_HEX_LEN];
    memset(upper, 'A', VS_HASH_HEX_LEN);
    upper[VS_HASH_HEX_LEN] = '\0';
    memset(short_id, 'a', VS_HASH_HEX_LEN - 1);
    short_id[VS_HASH_HEX_LEN - 1] = '\0';

    CHECK(s.has_object(s.ctx, "../x", &exists) == VS_ERR_INVALID);
    CHECK(s.has_object(s.ctx, "", &exists) == VS_ERR_INVALID);
    CHECK(s.has_object(s.ctx, NULL, &exists) == VS_ERR_INVALID);
    CHECK(s.has_object(s.ctx, upper, &exists) == VS_ERR_INVALID);
    CHECK(s.has_object(s.ctx, short_id, &exists) == VS_ERR_INVALID);
    CHECK(s.has_object(s.ctx, id, NULL) == VS_ERR_INVALID);
    CHECK(s.has_object(NULL, id, &exists) == VS_ERR_INVALID);

    free(rel2);
    free(rel);
    free(objects);
    CHECK(s.close(s.ctx) == VS_OK);
    remove_tree(root);
}

static void test_vs_local_put_object(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_put_object";
    vs_storage_t s;
    int exists = -1;

    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(vs_local_init(root) == VS_OK);
    int rc = vs_local_open(root, &s);
    CHECK(rc == VS_OK);
    if (rc != VS_OK)
    {
        remove_tree(root);
        return;
    }

    char id[VS_HASH_HEX_LEN + 1];
    CHECK(vs_hash("hello", 5, id) == VS_OK);

    /* not stored yet: a successful "no", not an error */
    CHECK(s.has_object(s.ctx, id, &exists) == VS_OK);
    CHECK(exists == 0);

    CHECK(s.put_object(s.ctx, id, "hello", 5) == VS_OK);
    
    CHECK(s.has_object(s.ctx, id, &exists) == VS_OK);
    CHECK(exists == 1);

    /* check data correctness */
    void* data = NULL;
    size_t size = 0;
    CHECK(s.get_object(s.ctx, id, &data, &size) == VS_OK);
    CHECK(size == 5);
    CHECK(data && memcmp(data, "hello", 5) == 0);
    free(data);
    data = NULL;
    size = 0;

    CHECK(vs_hash(NULL, 0, id) == VS_OK);
    CHECK(s.put_object(s.ctx, id, NULL, 1) == VS_ERR_INVALID);
    CHECK(s.put_object(s.ctx, id, NULL, 0) == VS_OK);

    CHECK(s.get_object(s.ctx, id, &data, &size) == VS_OK);
    CHECK(size == 0);
    free(data);

    CHECK(s.put_object(NULL, id, "hello", 5) == VS_ERR_INVALID);
    CHECK(s.put_object(s.ctx, NULL, "hello", 5) == VS_ERR_INVALID);
    CHECK(s.put_object(s.ctx, id, NULL, 5) == VS_ERR_INVALID);

    CHECK(s.close(s.ctx) == VS_OK);
    remove_tree(root);
}

/* Fresh repo at root, opened into s. 0 if anything failed (already CHECKed). */
static int open_fresh_repo(const char* root, vs_storage_t* s)
{
    remove_tree(root);
    CHECK(make_dir(VS_TEST_DIR, ""));
    CHECK(vs_local_init(root) == VS_OK);
    int rc = vs_local_open(root, s);
    CHECK(rc == VS_OK);
    if (rc != VS_OK)
    {
        remove_tree(root);
        return 0;
    }
    return 1;
}

/* A valid object id: two given chars, then 62 x fill. */
static void make_id(char out[VS_HASH_HEX_LEN + 1], const char* prefix, char fill)
{
    memset(out, fill, VS_HASH_HEX_LEN);
    out[0] = prefix[0];
    out[1] = prefix[1];
    out[VS_HASH_HEX_LEN] = '\0';
}

static void test_vs_local_get_object(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_get_object";
    vs_storage_t s;
    if (!open_fresh_repo(root, &s))
        return;

    char id[VS_HASH_HEX_LEN + 1];
    CHECK(vs_hash("hello", 5, id) == VS_OK);

    /* missing: NOT_FOUND, and the outputs are reset, not left as junk */
    void* data = (void*)1;
    size_t size = 123;
    CHECK(s.get_object(s.ctx, id, &data, &size) == VS_ERR_NOT_FOUND);
    CHECK(data == NULL);
    CHECK(size == 0);

    /* second put of the same id is a no-op: first contents stay, no tmp leftovers */
    CHECK(s.put_object(s.ctx, id, "hello", 5) == VS_OK);
    CHECK(s.put_object(s.ctx, id, "world", 5) == VS_OK);
    CHECK(count_entries(root, "tmp") == 0);
    CHECK(s.get_object(s.ctx, id, &data, &size) == VS_OK);
    CHECK(size == 5);
    CHECK(data && memcmp(data, "hello", 5) == 0);
    free(data);

    /* two ids with the same prefix share one objects/<xx>/ folder */
    const char* root2 = VS_TEST_DIR "/test_vs_local_get_object_prefix";
    vs_storage_t s2;
    char id_a[VS_HASH_HEX_LEN + 1];
    char id_b[VS_HASH_HEX_LEN + 1];
    make_id(id_a, "ab", '0');
    make_id(id_b, "ab", '1');
    if (open_fresh_repo(root2, &s2))
    {
        CHECK(s2.put_object(s2.ctx, id_a, "a", 1) == VS_OK);
        CHECK(s2.put_object(s2.ctx, id_b, "b", 1) == VS_OK);
        CHECK(count_entries(root2, "objects") == 3);    /* ab/, ab/<id_a>, ab/<id_b> */
        CHECK(count_entries(root2, "objects/ab") == 2);
        CHECK(s2.close(s2.ctx) == VS_OK);
        remove_tree(root2);
    }

    /* bad arguments */
    CHECK(s.get_object(NULL, id, &data, &size) == VS_ERR_INVALID);
    CHECK(s.get_object(s.ctx, NULL, &data, &size) == VS_ERR_INVALID);
    CHECK(s.get_object(s.ctx, "../x", &data, &size) == VS_ERR_INVALID);
    CHECK(s.get_object(s.ctx, id, NULL, &size) == VS_ERR_INVALID);
    CHECK(s.get_object(s.ctx, id, &data, NULL) == VS_ERR_INVALID);
    CHECK(s.put_object(s.ctx, "../x", "hello", 5) == VS_ERR_INVALID);

    CHECK(s.close(s.ctx) == VS_OK);
    remove_tree(root);
}

static void test_vs_local_snapshots(void)
{
    const char* root = VS_TEST_DIR "/test_vs_local_snapshots";
    vs_storage_t s;
    if (!open_fresh_repo(root, &s))
        return;

    vs_str_list_t list;
    void* data = NULL;
    size_t size = 0;

    /* empty repo lists nothing */
    CHECK(s.list_snapshots(s.ctx, &list) == VS_OK);
    CHECK(list.count == 0);
    vs_str_list_free(&list);

    /* put -> get round trip, file lands at snapshots/<id> */
    CHECK(s.put_snapshot(s.ctx, "b", "snap-b", 6) == VS_OK);
    CHECK(file_equals(root, "snapshots/b", "snap-b", 6));
    CHECK(s.get_snapshot(s.ctx, "b", &data, &size) == VS_OK);
    CHECK(size == 6);
    CHECK(data && memcmp(data, "snap-b", 6) == 0);
    free(data);

    /* same id again is refused and the first contents survive */
    CHECK(s.put_snapshot(s.ctx, "b", "other!", 6) == VS_ERR_INVALID);
    CHECK(file_equals(root, "snapshots/b", "snap-b", 6));
    CHECK(count_entries(root, "tmp") == 0);

    /* empty snapshot is allowed */
    CHECK(s.put_snapshot(s.ctx, "empty", NULL, 0) == VS_OK);
    CHECK(s.get_snapshot(s.ctx, "empty", &data, &size) == VS_OK);
    CHECK(size == 0);
    free(data);

    /* missing: NOT_FOUND with reset outputs */
    data = (void*)1;
    size = 123;
    CHECK(s.get_snapshot(s.ctx, "nope", &data, &size) == VS_ERR_NOT_FOUND);
    CHECK(data == NULL);
    CHECK(size == 0);

    /* list: sorted, files only, junk names skipped */
    CHECK(s.put_snapshot(s.ctx, "c", "c", 1) == VS_OK);
    CHECK(s.put_snapshot(s.ctx, "a", "a", 1) == VS_OK);
    CHECK(make_file(root, "snapshots/junk.txt", 3));
    CHECK(make_dir(root, "snapshots/sub"));
    CHECK(make_file(root, "snapshots/sub/x", 1));
    CHECK(s.list_snapshots(s.ctx, &list) == VS_OK);
    CHECK(list.count == 4);
    if (list.count == 4)
    {
        CHECK_STR_EQ(list.items[0], "a");
        CHECK_STR_EQ(list.items[1], "b");
        CHECK_STR_EQ(list.items[2], "c");
        CHECK_STR_EQ(list.items[3], "empty");
    }
    vs_str_list_free(&list);

    /* bad ids and arguments */
    char long_id[34];
    memset(long_id, 'a', 33);
    long_id[33] = '\0';
    CHECK(s.put_snapshot(s.ctx, "../x", "x", 1) == VS_ERR_INVALID);
    CHECK(s.put_snapshot(s.ctx, "", "x", 1) == VS_ERR_INVALID);
    CHECK(s.put_snapshot(s.ctx, long_id, "x", 1) == VS_ERR_INVALID);
    CHECK(s.put_snapshot(s.ctx, "a2", NULL, 1) == VS_ERR_INVALID);
    CHECK(s.put_snapshot(NULL, "a2", "x", 1) == VS_ERR_INVALID);
    CHECK(s.get_snapshot(s.ctx, "../x", &data, &size) == VS_ERR_INVALID);
    CHECK(s.get_snapshot(s.ctx, NULL, &data, &size) == VS_ERR_INVALID);
    CHECK(s.list_snapshots(NULL, &list) == VS_ERR_INVALID);
    CHECK(s.list_snapshots(s.ctx, NULL) == VS_ERR_INVALID);

    CHECK(s.close(s.ctx) == VS_OK);
    remove_tree(root);
}

#define MAN_SOURCE  "C:/Users/me/Photos"
#define MAN_CREATED "20261121T200945Z"

static void test_vs_manifest_create(void)
{
    vs_manifest_t* man = NULL;

    int rc = vs_manifest_create(MAN_SOURCE, MAN_CREATED, &man);
    CHECK(rc == VS_OK);
    if (rc != VS_OK)
        return;

    CHECK_STR_EQ(man->source, MAN_SOURCE);
    CHECK_STR_EQ(man->created, MAN_CREATED);
    CHECK(man->count == 0);
    CHECK(man->capacity == 0);
    CHECK(man->items == NULL);

    vs_manifest_free(man);
    vs_manifest_free(NULL);

    /* invalid input: error, *out stays NULL */
    man = NULL;
    CHECK(vs_manifest_create(MAN_SOURCE, "2026", &man) == VS_ERR_INVALID);
    CHECK(man == NULL);
    CHECK(vs_manifest_create(MAN_SOURCE, MAN_CREATED "0", &man) == VS_ERR_INVALID);
    CHECK(man == NULL);
    CHECK(vs_manifest_create(NULL, MAN_CREATED, &man) == VS_ERR_INVALID);
    CHECK(vs_manifest_create(MAN_SOURCE, NULL, &man) == VS_ERR_INVALID);
    CHECK(vs_manifest_create(MAN_SOURCE, MAN_CREATED, NULL) == VS_ERR_INVALID);
}

static void test_vs_manifest_add_entry(void)
{
    vs_manifest_t* man = NULL;
    if (vs_manifest_create(MAN_SOURCE, MAN_CREATED, &man) != VS_OK)
    {
        CHECK(0);
        return;
    }

    /* manifest must keep its own copy of rel_path */
    char path[64] = "Trips";
    vs_file_entry_t d = {0, 111, 1, path};
    CHECK(vs_manifest_add_entry(man, &d) == VS_OK);
    snprintf(path, sizeof(path), "XXXXX");
    CHECK_STR_EQ(man->items[0].info.rel_path, "Trips");

    vs_file_entry_t f = {2097152, 222, 0, "Trips/a b.jpg"};
    CHECK(vs_manifest_add_entry(man, &f) == VS_OK);

    CHECK(man->count == 2);
    CHECK(man->items[0].info.is_dir == 1);
    CHECK(man->items[0].info.mtime == 111);
    CHECK(man->items[1].info.is_dir == 0);
    CHECK(man->items[1].info.size == 2097152);
    CHECK(man->items[1].info.mtime == 222);
    CHECK_STR_EQ(man->items[1].info.rel_path, "Trips/a b.jpg");
    CHECK(man->items[1].chunk_count == 0);
    CHECK(man->items[1].ids == NULL);

    /* invalid paths */
    vs_file_entry_t bad = {0, 0, 0, "a\nb"};
    CHECK(vs_manifest_add_entry(man, &bad) == VS_ERR_INVALID);
    bad.rel_path = "a\rb";
    CHECK(vs_manifest_add_entry(man, &bad) == VS_ERR_INVALID);
    bad.rel_path = "";
    CHECK(vs_manifest_add_entry(man, &bad) == VS_ERR_INVALID);
    bad.rel_path = NULL;
    CHECK(vs_manifest_add_entry(man, &bad) == VS_ERR_INVALID);
    CHECK(vs_manifest_add_entry(man, NULL) == VS_ERR_INVALID);
    CHECK(vs_manifest_add_entry(NULL, &f) == VS_ERR_INVALID);
    CHECK(man->count == 2);

    /* growth */
    for (int i = 0; i < 10000; ++i)
    {
        char p[32];
        snprintf(p, sizeof(p), "file_%d", i);
        vs_file_entry_t e = {(uint64_t)i, 0, 0, p};
        CHECK(vs_manifest_add_entry(man, &e) == VS_OK);
    }
    CHECK(man->count == 10002);
    CHECK_STR_EQ(man->items[10001].info.rel_path, "file_9999");
    CHECK(man->items[10001].info.size == 9999);

    vs_manifest_free(man);
}

static void test_vs_manifest_add_chunk(void)
{
    vs_manifest_t* man = NULL;
    if (vs_manifest_create(MAN_SOURCE, MAN_CREATED, &man) != VS_OK)
    {
        CHECK(0);
        return;
    }

    char id_a[VS_HASH_HEX_LEN + 1], id_b[VS_HASH_HEX_LEN + 1];
    make_id(id_a, "9f", '1');
    make_id(id_b, "41", '2');

    /* no entry yet */
    CHECK(vs_manifest_add_chunk(man, id_a) == VS_ERR_INVALID);

    /* dirs have no chunks */
    vs_file_entry_t d = {0, 0, 1, "Trips"};
    CHECK(vs_manifest_add_entry(man, &d) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, id_a) == VS_ERR_INVALID);
    CHECK(man->items[0].chunk_count == 0);

    /* chunks go to the last entry, in order */
    vs_file_entry_t f = {3 * VS_CHUNK_SIZE, 0, 0, "Trips/a.jpg"};
    CHECK(vs_manifest_add_entry(man, &f) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, id_a) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, id_b) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, id_a) == VS_OK);
    CHECK(man->items[1].chunk_count == 3);
    CHECK_STR_EQ(man->items[1].ids[0], id_a);
    CHECK_STR_EQ(man->items[1].ids[1], id_b);
    CHECK_STR_EQ(man->items[1].ids[2], id_a);

    /* invalid ids */
    char bad[VS_HASH_HEX_LEN + 1];
    make_id(bad, "AB", 'c');    /* uppercase */
    CHECK(vs_manifest_add_chunk(man, bad) == VS_ERR_INVALID);
    make_id(bad, "zz", 'c');    /* not hex */
    CHECK(vs_manifest_add_chunk(man, bad) == VS_ERR_INVALID);
    CHECK(vs_manifest_add_chunk(man, "abc") == VS_ERR_INVALID);
    CHECK(vs_manifest_add_chunk(man, NULL) == VS_ERR_INVALID);
    CHECK(vs_manifest_add_chunk(NULL, id_a) == VS_ERR_INVALID);
    CHECK(man->items[1].chunk_count == 3);

    /* growth */
    vs_file_entry_t big = {100 * VS_CHUNK_SIZE, 0, 0, "big.bin"};
    CHECK(vs_manifest_add_entry(man, &big) == VS_OK);
    for (int i = 0; i < 100; ++i)
        CHECK(vs_manifest_add_chunk(man, i % 2 ? id_a : id_b) == VS_OK);
    CHECK(man->items[2].chunk_count == 100);
    CHECK_STR_EQ(man->items[2].ids[0], id_b);
    CHECK_STR_EQ(man->items[2].ids[99], id_a);

    vs_manifest_free(man);
}

static void test_vs_manifest_serialize(void)
{
    const char* expected = 
        "vaultsync-manifest 1\n"
        "source C:/Users/me/Photos\n"
        "created 20261126T205617Z\n"
        "dir 5435345 342345 Holiday\n"
        "file 1500000 534532 2 me.jpg\n"
        "chunk af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\n"
        "chunk 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213\n"
        "end 2\n";
    
    vs_manifest_t* man = NULL;
    vs_file_entry_t dir = {5435345, 342345, 1, "Holiday"};
    vs_file_entry_t file = {1500000, 534532, 0, "me.jpg"};

    CHECK(vs_manifest_create("C:/Users/me/Photos", "20261126T205617Z", &man) == VS_OK);
    if (!man)
        return;

    CHECK(vs_manifest_add_entry(man, &dir) == VS_OK);
    CHECK(vs_manifest_add_entry(man, &file) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262") == VS_OK);
    CHECK(vs_manifest_add_chunk(man, "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213") == VS_OK);

    void* data = NULL;
    size_t size = 0;
    CHECK(vs_manifest_serialize(man, &data, &size) == VS_OK);
    CHECK(size == strlen(expected));
    CHECK(memcmp(data, expected, size) == 0);

    CHECK(vs_manifest_serialize(NULL, &data, &size) == VS_ERR_INVALID);
    CHECK(vs_manifest_serialize(man, NULL, &size) == VS_ERR_INVALID);
    CHECK(vs_manifest_serialize(man, &data, NULL) == VS_ERR_INVALID);

    free(data);
    vs_manifest_free(man);
}

static void test_manifest_parse(void)
{
    const char* serialized = 
        "vaultsync-manifest 1\n"
        "source C:/Users/me/Photos\n"
        "created 20261126T205617Z\n"
        "dir 5435345 342345 Holiday\n"
        "file 1500000 534532 2 me.jpg\n"
        "chunk af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\n"
        "chunk 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213\n"
        "end 2\n";

    vs_manifest_t* man = NULL;
    int rc = vs_manifest_parse(serialized, strlen(serialized), &man);
    CHECK(rc == VS_OK);
    if (rc != VS_OK)
        return;
    
    CHECK(man->count == 2);
    CHECK_STR_EQ(man->created, "20261126T205617Z");
    CHECK_STR_EQ(man->source, "C:/Users/me/Photos");
    CHECK(man->items[0].info.is_dir == 1);
    CHECK(man->items[0].info.size == 5435345);
    CHECK(man->items[0].info.mtime == 342345);
    CHECK_STR_EQ(man->items[0].info.rel_path, "Holiday");
    CHECK(man->items[1].info.is_dir == 0);
    CHECK(man->items[1].info.size == 1500000);
    CHECK(man->items[1].info.mtime == 534532);
    CHECK_STR_EQ(man->items[1].info.rel_path, "me.jpg");
    CHECK(man->items[1].chunk_count == 2);
    CHECK_STR_EQ(man->items[1].ids[0], 
        "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
    CHECK_STR_EQ(man->items[1].ids[1], 
        "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213");

    vs_manifest_free(man);
    man = NULL;

    CHECK(vs_manifest_parse(NULL, 1, &man) == VS_ERR_INVALID);
    CHECK(vs_manifest_parse(serialized, 0, &man) == VS_ERR_CORRUPT);
    CHECK(vs_manifest_parse(serialized, strlen(serialized), NULL) == VS_ERR_INVALID);
}

static void test_vs_manifest_roundtrip(void)
{
    const char* expected =
        "vaultsync-manifest 1\n"
        "source C:/Users/me/Photos\n"
        "created 20261126T205617Z\n"
        "dir 0 0 Holiday\n"
        "file 1500000 534532 2 Holiday/me and you.jpg\n"
        "chunk af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\n"
        "chunk 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213\n"
        "file 0 534533 0 empty.txt\n"
        "end 3\n";

    vs_manifest_t* man = NULL;
    vs_manifest_t* out = NULL;
    void* data = NULL;
    void* new_data = NULL;
    size_t size = 0, new_size = 0;
    vs_file_entry_t dir = {0, 0, 1, "Holiday"};
    vs_file_entry_t file = {1500000, 534532, 0, "Holiday/me and you.jpg"};
    vs_file_entry_t empty = {0, 534533, 0, "empty.txt"};

    CHECK(vs_manifest_create("C:/Users/me/Photos", "20261126T205617Z", &man) == VS_OK);
    if (!man)
        return;

    CHECK(vs_manifest_add_entry(man, &dir) == VS_OK);
    CHECK(vs_manifest_add_entry(man, &file) == VS_OK);
    CHECK(vs_manifest_add_chunk(man, "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262") == VS_OK);
    CHECK(vs_manifest_add_chunk(man, "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213") == VS_OK);
    CHECK(vs_manifest_add_entry(man, &empty) == VS_OK);

    CHECK(vs_manifest_serialize(man, &data, &size) == VS_OK);
    CHECK(size == strlen(expected));
    CHECK(data && memcmp(data, expected, size) == 0);

    /* serialize -> parse -> serialize gives identical bytes */
    CHECK(vs_manifest_parse(data, size, &out) == VS_OK);
    if (out)
    {
        CHECK(vs_manifest_serialize(out, &new_data, &new_size) == VS_OK);
        CHECK(new_size == size);
        CHECK(new_data && memcmp(new_data, data, new_size) == 0);
    }

    free(new_data);
    free(data);
    vs_manifest_free(out);
    vs_manifest_free(man);
}

static void test_vs_manifest_parse_corrupt(void)
{
    const char* valid =
        "vaultsync-manifest 1\n"
        "source C:/src\n"
        "created 20261126T205617Z\n"
        "dir 0 0 d\n"
        "file 1500000 1 2 d/a.jpg\n"
        "chunk af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\n"
        "chunk 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213\n"
        "end 2\n";

#define MAN_HEAD "vaultsync-manifest 1\nsource C:/src\ncreated 20261126T205617Z\n"
#define MAN_ID   "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262"

    const char* bad[] = {
        "vaultsync-manifest 2\nsource C:/src\ncreated 20261126T205617Z\nend 0\n",   /* version */
        "vaultsync-manifest 1\r\nsource C:/src\r\ncreated 20261126T205617Z\r\nend 0\r\n", /* CRLF */
        "vaultsync-manifest 1\ncreated 20261126T205617Z\nsource C:/src\nend 0\n",   /* header order */
        MAN_HEAD "dir 5x0 0 d\nend 1\n",                        /* garbage in number */
        MAN_HEAD "dir 0  0 d\nend 1\n",                         /* double space */
        MAN_HEAD "dir -1 0 d\nend 1\n",                         /* negative */
        MAN_HEAD "dir 007 0 d\nend 1\n",                        /* leading zero */
        MAN_HEAD "dir 18446744073709551616 0 d\nend 1\n",       /* overflow */
        MAN_HEAD "dir 0 0 \nend 1\n",                           /* empty path */
        MAN_HEAD "file 1500000 1 1 a\nchunk " MAN_ID "\nend 1\n",               /* count != size */
        MAN_HEAD "file 1500000 1 2 a\nchunk " MAN_ID "\nend 1\n",               /* missing chunk */
        MAN_HEAD "file 1 1 1 a\nchunk " MAN_ID "\nchunk " MAN_ID "\nend 1\n",   /* extra chunk */
        MAN_HEAD "dir 0 0 d\nchunk " MAN_ID "\nend 1\n",        /* chunk after dir */
        MAN_HEAD "file 1 1 1 a\nchunk AF1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\nend 1\n", /* uppercase id */
        MAN_HEAD "foo 1\nend 0\n",                              /* unknown keyword */
        MAN_HEAD "\nend 0\n",                                   /* empty line */
        MAN_HEAD "end 1\n",                                     /* wrong count */
        MAN_HEAD "end 0x\n",                                    /* trailing garbage */
        MAN_HEAD "end 0\ndir 0 0 d\n",                          /* line after end */
        MAN_HEAD "dir 0 0 d\n",                                 /* no end */
        MAN_HEAD "end 0",                                       /* no final newline */
    };

#undef MAN_HEAD
#undef MAN_ID

    vs_manifest_t* man = NULL;

    /* sanity: the valid one parses */
    CHECK(vs_manifest_parse(valid, strlen(valid), &man) == VS_OK);
    vs_manifest_free(man);

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        man = NULL;
        int rc = vs_manifest_parse(bad[i], strlen(bad[i]), &man);
        if (rc != VS_ERR_CORRUPT)
            fprintf(stderr, "  bad[%zu] gave %d\n", i, rc);
        CHECK(rc == VS_ERR_CORRUPT);
        CHECK(man == NULL);
        vs_manifest_free(man);
    }

    /* NUL byte inside the data */
    char with_nul[64];
    memcpy(with_nul, valid, sizeof(with_nul));
    with_nul[30] = '\0';
    CHECK(vs_manifest_parse(with_nul, sizeof(with_nul), &man) == VS_ERR_CORRUPT);

    /* every truncation of a valid manifest must fail */
    size_t len = strlen(valid);
    for (size_t n = 0; n < len; ++n)
    {
        man = NULL;
        int rc = vs_manifest_parse(valid, n, &man);
        CHECK(rc != VS_OK);
        CHECK(man == NULL);
        vs_manifest_free(man);
    }
}

/* Damage one byte at a time. Any result is fine (a flipped letter in a path
   is still valid), but parse must never crash, leak, or hand back a manifest
   on error. Run under ASan to catch out-of-bounds reads. */
static void test_vs_manifest_parse_flip(void)
{
    const char* valid =
        "vaultsync-manifest 1\n"
        "source C:/src\n"
        "created 20261126T205617Z\n"
        "dir 0 0 d\n"
        "file 1500000 1 2 d/a b.jpg\n"
        "chunk af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262\n"
        "chunk 2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213\n"
        "file 0 2 0 empty.txt\n"
        "end 3\n";

    const unsigned char masks[] = { 0x01, 0x20, 0x80 };
    size_t len = strlen(valid);
    char buf[512];

    CHECK(len <= sizeof(buf));
    if (len > sizeof(buf))
        return;

    size_t ok = 0, corrupt = 0;
    for (size_t m = 0; m < sizeof(masks); ++m)
    {
        for (size_t i = 0; i < len; ++i)
        {
            memcpy(buf, valid, len);
            buf[i] ^= masks[m];

            vs_manifest_t* man = NULL;
            int rc = vs_manifest_parse(buf, len, &man);
            if (rc == VS_OK)
            {
                CHECK(man != NULL);
                ok++;
            }
            else
            {
                CHECK(rc == VS_ERR_CORRUPT);
                CHECK(man == NULL);
                corrupt++;
            }
            vs_manifest_free(man);
        }
    }

    /* most single-byte damage must be detected */
    CHECK(corrupt > ok);
}

int main(int argc, char** argv)
{
#ifdef VS_CRT_DEBUG
    /* Send CRT reports (leaks, heap corruption, abort) to stderr instead of
       a message box, so ctest never hangs waiting for someone to click OK. */
    _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    /* To find where block {N} from a leak report was allocated, uncomment
       and run under the debugger: it breaks on that malloc. */
    /* _CrtSetBreakAlloc(N); */
#endif
#ifndef VS_LEAK_CHECK
    printf("note: leak check disabled (ASan build or non-MSVC/Debug)\n");
#endif

    Test tests[] = {
        TEST(test_strerr),
        TEST(test_vs_str_list_init),
        TEST(test_vs_str_list_push_basic),
        TEST(test_vs_str_list_push_copies),
        TEST(test_vs_str_list_growth),
        TEST(test_vs_str_list_many),
        TEST(test_vs_str_list_invalid),
        TEST(test_vs_str_list_free),
        TEST(test_vs_str_list_empty_string),
        TEST(test_vs_utf8_to_wide),
        TEST(test_vs_wide_to_utf8),
        TEST(test_vs_utf8_to_wide_to_utf8),
        TEST(test_vs_path_join),
        TEST(test_vs_hex),
        TEST(test_vs_file_list_init),
        TEST(test_vs_file_list_push),
        TEST(test_vs_file_list_free),
        TEST(test_vs_scan),
        TEST(test_vs_hash),
        TEST(test_vs_chunk_file),
        TEST(test_vs_local_init),
        TEST(test_vs_local_open),
        TEST(test_vs_local_has_object),
        TEST(test_vs_local_put_object),
        TEST(test_vs_local_get_object),
        TEST(test_vs_local_snapshots),
        TEST(test_vs_manifest_create),
        TEST(test_vs_manifest_add_entry),
        TEST(test_vs_manifest_add_chunk),
        TEST(test_vs_manifest_serialize),
        TEST(test_manifest_parse),
        TEST(test_vs_manifest_roundtrip),
        TEST(test_vs_manifest_parse_corrupt),
        TEST(test_vs_manifest_parse_flip),
    };
    /* No argument: run every test. One argument: run only the test with that
       name (ctest registers each test this way, see CMakeLists.txt). */
    const char* only = argc > 1 ? argv[1] : NULL;
    int ran = 0;
    size_t len = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < len; ++i)
    {
        if (only && strcmp(only, tests[i].name) != 0)
            continue;
        printf("TEST %s\n", tests[i].name);
        ran++;
#ifdef VS_LEAK_CHECK
        _CrtMemState mem_before, mem_after, mem_diff;
        _CrtMemCheckpoint(&mem_before);
#endif

        tests[i].func();

#ifdef VS_LEAK_CHECK
        /* Anything still allocated that wasn't before this test is a leak. */
        _CrtMemCheckpoint(&mem_after);
        if (_CrtMemDifference(&mem_diff, &mem_before, &mem_after))
        {
            fprintf(stderr, "MEMORY LEAK in %s:\n", tests[i].name);
            _CrtMemDumpAllObjectsSince(&mem_before);
            g_failures++;
        }
#endif
    }

    return g_failures ? 1 : 0;
}