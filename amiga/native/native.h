#ifndef TERMIC_NATIVE_H
#define TERMIC_NATIVE_H
#include <stddef.h>
#define SOURCE_LIMIT 32768
#define RESPONSE_LIMIT 262144
#ifndef TERMIC_PROJECT
#define TERMIC_PROJECT ""
#endif
#ifndef TERMIC_CONFIG
#define TERMIC_CONFIG "Termic:Config/"
#endif
char *read_text(const char *path, size_t limit);
int replace_source(const char *text);
/* 1 complete, 0 incomplete, -1 malformed/unsupported. Allocates body. */
int decode_http(const char *raw, size_t size, int eof, int *status, char **body);
int cloud_request(const char *key, const char *json, char **reply);
int run_agent(void);
char *list_project_sources(void);
int project_source_allowed(const char *path);
int build_project(void);
int cancel_requested(void);
#endif
