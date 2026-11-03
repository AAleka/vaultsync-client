#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vaultsync/vaultsync.h>
#include"util.h"

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
        if (strcmp((a), (b)) != 0) { \
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
        if (strcmp((a), (b)) == 0) { \
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

static void test_list_init(void)
{
    vs_str_list list;
    vs_str_list_init(&list);

    CHECK(list.count == 0);
    CHECK(list.capacity == 0);
    CHECK(list.items == NULL);

    vs_str_list_free(&list);

    vs_str_list_init(NULL);
}

static void test_list_push_basic(void)
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

static void test_list_push_copies(void)
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

static void test_list_growth(void)
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

static void test_list_many(void)
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

static void test_list_invalid(void)
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

static void test_list_free(void)
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

static void test_list_empty_string(void)
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
        TEST(test_list_init),
        TEST(test_list_push_basic),
        TEST(test_list_push_copies),
        TEST(test_list_growth),
        TEST(test_list_many),
        TEST(test_list_invalid),
        TEST(test_list_free),
        TEST(test_list_empty_string),
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