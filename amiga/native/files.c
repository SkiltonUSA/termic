#include "native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *read_text(const char *path, size_t limit)
{
    FILE *f = fopen(path, "rb");
    char *text;
    size_t n;
    if (!f) return NULL;
    text = malloc(limit + 1);
    if (!text) { fclose(f); return NULL; }
    n = fread(text, 1, limit, f);
    if (ferror(f) || fgetc(f) != EOF || memchr(text, 0, n)) {
        fclose(f); free(text); return NULL;
    }
    fclose(f);
    text[n] = 0;
    return text;
}

int replace_source(const char *text)
{
    FILE *f;
    size_t size = strlen(text);
    int ok;
    if (!size || size > SOURCE_LIMIT) return 0;
    f = fopen(TERMIC_PROJECT "main.c.new", "wb");
    if (!f) return 0;
    ok = fwrite(text, 1, size, f) == size;
    if (fclose(f)) ok = 0;
    if (!ok) { remove(TERMIC_PROJECT "main.c.new"); return 0; }
    /* Keep the previous source until the replacement has been written. */
    remove(TERMIC_PROJECT "main.c.bak");
    if (rename(TERMIC_PROJECT "main.c", TERMIC_PROJECT "main.c.bak")) {
        remove(TERMIC_PROJECT "main.c.new"); return 0;
    }
    if (rename(TERMIC_PROJECT "main.c.new", TERMIC_PROJECT "main.c")) {
        rename(TERMIC_PROJECT "main.c.bak", TERMIC_PROJECT "main.c"); return 0;
    }
    return 1;
}
