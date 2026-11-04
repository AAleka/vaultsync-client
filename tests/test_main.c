#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

#include <vaultsync/vaultsync.h>
#include "util.h"
#include "scanner.h"

/* MSVC Debug builds only: the CRT debug heap tracks every allocation. */
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#define VS_LEAK_CHECK 1
#endif

static int g_failures = 0;

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
    vs_str_list list;
    vs_str_list_init(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_str_list_free(&list);

    vs_str_list_init(NULL);
}

static void test_vs_str_list_push_basic(void)
{
    vs_str_list list;
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
    vs_str_list list;
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
    vs_str_list list;
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
    vs_str_list list;
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
    vs_str_list list;
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
    vs_str_list list;
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
    vs_str_list list;
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
    vs_file_list list;
    vs_file_list_init(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_file_list_free(&list);

    vs_file_list_init(NULL);
}

static void test_vs_file_list_push(void)
{
    vs_file_list list;
    vs_file_list_init(&list);

    const int num_entries = 100000;
    
    for (int i = 0; i < num_entries; ++i)
    {
        vs_file_entry f;
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
    vs_file_list list;
    vs_file_list_init(&list);

    vs_file_entry f = {"/test/test.txt", 5435345, 3123123213, 0};
    vs_file_entry d = {"/test/tests/", 432423445, 3331221313, 1};

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
    vs_file_list list;
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

    remove_tree(root);  /* leftovers from a crashed run */
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

    const vs_file_entry expected[] = {
        { "a.txt",                  2, 0, 0 },
        { "b.txt",                  1, 0, 0 },
        { "empty",                  0, 0, 1 },
        { "nested",                 0, 0, 1 },
        { "nested/a",               0, 0, 1 },
        { "nested/a/b",             0, 0, 1 },
        { "nested/a/b/c.txt",       3, 0, 0 },
        { "zero.txt",               0, 0, 0 },
        { UNI_DIR,                  0, 0, 1 },
        { UNI_DIR "/" UNI_FILE,     5, 0, 0 },
    };
    const size_t n = sizeof(expected) / sizeof(expected[0]);

    vs_file_list list;
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

    /* empty dir */
    char* empty = NULL;
    CHECK(vs_path_join(root, "empty", &empty) == VS_OK);
    CHECK(vs_scan(empty, &list) == VS_OK);
    CHECK(list.count == 0);
    vs_file_list_free(&list);
    free(empty);

    /* errors */
    char* file = NULL;
    CHECK(vs_path_join(root, "a.txt", &file) == VS_OK);
    CHECK(vs_scan(file, &list) == VS_ERR_INVALID);
    free(file);
    CHECK(vs_scan(VS_TEST_DIR "/no_such_dir", &list) == VS_ERR_NOT_FOUND);
    CHECK(vs_scan("", &list) == VS_ERR_INVALID);
    CHECK(vs_scan(NULL, &list) == VS_ERR_INVALID);
    CHECK(vs_scan(root, NULL) == VS_ERR_INVALID);

    // remove_tree(root);
}

int main(void)
{
#ifdef VS_LEAK_CHECK
    /* Send CRT reports (leaks, heap corruption, abort) to stderr instead of
       a message box, so ctest never hangs waiting for someone to click OK. */
    _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    /* Snapshot of the heap before any test runs. */
    _CrtMemState mem_before, mem_after, mem_diff;
    _CrtMemCheckpoint(&mem_before);
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
    };
    size_t len = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < len; ++i)
    {
        printf("TEST %s\n", tests[i].name);
        tests[i].func();
    }

#ifdef VS_LEAK_CHECK
    /* Anything still allocated that wasn't before is a leak. */
    _CrtMemCheckpoint(&mem_after);
    if (_CrtMemDifference(&mem_diff, &mem_before, &mem_after))
    {
        fprintf(stderr, "MEMORY LEAK detected:\n");
        _CrtMemDumpStatistics(&mem_diff);
        g_failures++;
    }
#endif

    return g_failures ? 1 : 0;
}