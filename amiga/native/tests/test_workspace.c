#include "native.h"
#include "workspace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char *text;
    const char *bad[] = {"",         "../main.c",   "src/../main.c", "SYS:main.c",
                         "/main.c",  "src//main.c", ".git/config",   "main.c\n",
                         "main.c\"", "main.c;echo", "main",          "Program",
                         NULL};
    int i;
    assert(source_name("main.c"));
    assert(source_name("src/util.h"));
    for (i = 0; bad[i]; i++)
        assert(!source_name(bad[i]));
    assert(save_text("fixture.c", "original", 32));
    assert(save_text("fixture.c", "replacement", 32));
    text = read_text("fixture.c.bak", 32);
    assert(text && !strcmp(text, "original"));
    free(text);
    assert(!save_text("fixture.c", "too large", 2));
    text = read_text("fixture.c", 32);
    assert(text && !strcmp(text, "replacement"));
    free(text);
    assert(save_text("fixture.c", "", 32));
    text = read_text("fixture.c", 32);
    assert(text && !*text);
    free(text);
    assert(!save_text("missing/file.c", "source", 32));
    puts("Workspace paths, backup saves, empty files and failure preservation passed.");
    return 0;
}
