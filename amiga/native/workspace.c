#include "workspace.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int source_name(const char *s)
{
    const char *p = s, *dot;
    unsigned n = 0;
    if (!s || !*s || *s == '.' || *s == '/')
        return 0;
    for (; *p; ++p)
    {
        if (++n > 180)
            return 0;
        if (*p == '/')
        {
            if (p == s || p[-1] == '/' || p[1] == '.' || !p[1])
                return 0;
        }
        else if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-' && *p != '.')
            return 0;
    }
    dot = strrchr(s, '.');
    return dot && (!strcmp(dot, ".c") || !strcmp(dot, ".h") || !strcmp(dot, ".txt") ||
                   !strcmp(dot, ".md"));
}
int save_text(const char *path, const char *text, unsigned long limit)
{
    char tmp[512], bak[512];
    FILE *f;
    size_t n;
    int existed = 0, ok;
    if (!path || strlen(path) > 500 || !text || (n = strlen(text)) > limit)
        return 0;
    snprintf(tmp, sizeof(tmp), "%s.new", path);
    snprintf(bak, sizeof(bak), "%s.bak", path);
    f = fopen(tmp, "wb");
    if (!f)
        return 0;
    ok = fwrite(text, 1, n, f) == n;
    if (fclose(f))
        ok = 0;
    if (!ok)
    {
        remove(tmp);
        return 0;
    }
    f = fopen(path, "rb");
    if (f)
    {
        fclose(f);
        existed = 1;
        remove(bak);
        if (rename(path, bak))
        {
            remove(tmp);
            return 0;
        }
    }
    if (rename(tmp, path))
    {
        if (existed)
            rename(bak, path);
        remove(tmp);
        return 0;
    }
    return 1;
}
