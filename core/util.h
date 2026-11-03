#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

char* vs_strdup(const char* s);
int vs_utf8_to_wide(const char* s, wchar_t** out);
int vs_wide_to_utf8(const wchar_t* s, char** out);
int vs_path_join(const char* a, const char* b, char** out);
void vs_hex(const uint8_t* bytes, size_t n, char* out);

#ifdef __cplusplus
}
#endif

#endif