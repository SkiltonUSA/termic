#include "syntax.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
static void roundtrip(const char *s)
{
    char *v = syntax_colours(s, 1), *p = v, *out = malloc(strlen(s) + 1), *w = out;
    assert(v && out);
    while (*p)
    {
        if (*p == 27)
        {
            p = strchr(p, ']');
            assert(p);
            p++;
        }
        else
            *w++ = *p++;
    }
    *w = 0;
    assert(!strcmp(s, out));
    free(v);
    free(out);
}
int main(void)
{
    char *s, *v;
    roundtrip("");
    roundtrip("int main(void) { return 42; }\n");
    roundtrip("/* comment\ncontinued */\n#define FOO(x) (x)\nchar *s=\"a\\\"b\"; // end\n");
    roundtrip("char c='\\\'';\n/* unterminated");
    v = syntax_colours("int f(void) {return 42;} // comment\n", 1);
    assert(strstr(v, "\033P[FF7B8B]int"));
    assert(strstr(v, "\033P[DDA0FF]f"));
    assert(strstr(v, "\033P[FFB86C]42"));
    assert(strstr(v, "\033P[A6AFBD]// comment"));
    free(v);
    s = malloc(32770);
    memset(s, ';', 32768);
    s[32768] = 0;
    roundtrip(s);
    s[32768] = 'x';
    s[32769] = 0;
    assert(!syntax_colours(s, 1));
    free(s);
    v = syntax_colours("return text\n\n", 0);
    assert(v && strstr(v, "E6EDF3") && !strstr(v, "FF7B8B"));
    free(v);
    assert(!syntax_colours("\033p[1]text", 1));
    return 0;
}
