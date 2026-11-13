#include "syntax.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
static int word(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}
static int keyword(const char *s, size_t n)
{
    static const char *words[] = {
        "auto",     "break",  "case",   "char",     "const",    "continue", "default",  "do",
        "double",   "else",   "enum",   "extern",   "float",    "for",      "goto",     "if",
        "inline",   "int",    "long",   "register", "restrict", "return",   "short",    "signed",
        "sizeof",   "static", "struct", "switch",   "typedef",  "union",    "unsigned", "void",
        "volatile", "while",  "_Bool",  NULL};
    int i;
    for (i = 0; words[i]; i++)
        if (strlen(words[i]) == n && !strncmp(s, words[i], n))
            return 1;
    return 0;
}
char *syntax_colours(const char *text, int csyntax)
{
    /* Bright accents on a black editor surface.
     * RGB escapes let TextEditor own screen pens across show/hide cycles. */
    static const char *colour[] = {"E6EDF3", "FF7B8B", "DDA0FF", "FFB86C",
                                   "8FE3A4", "A6AFBD", "79C8FF"};
    const char *p = text, *end, *q;
    char *out, *w;
    size_t n;
    int kind, first = 1;
    if (!text || (n = strlen(text)) > 32768 || strchr(text, 27))
        return NULL;
    out = malloc((n + 1) * 32);
    if (!out)
        return NULL;
    w = out;
    memcpy(w, "\033P[E6EDF3]", 10);
    w += 10;
    while (*p)
    {
        end = p + 1;
        kind = 0;
        if (*p == '\n')
        {
            *w++ = *p++;
            memcpy(w, "\033P[E6EDF3]", 10);
            w += 10;
            first = 1;
            continue;
        }
        if (*p == ' ' || *p == '\t' || *p == '\r')
        {
            *w++ = *p++;
            continue;
        }
        if (!csyntax)
        {
            while (*end && *end != '\n')
                end++;
        }
        else if (first && *p == '#')
        {
            kind = 6;
            while (*end && *end != '\n')
                end++;
        }
        else if (p[0] == '/' && p[1] == '/')
        {
            kind = 5;
            while (*end && *end != '\n')
                end++;
        }
        else if (p[0] == '/' && p[1] == '*')
        {
            kind = 5;
            end = p + 2;
            while (*end && !(end[0] == '*' && end[1] == '/'))
                end++;
            if (*end)
                end += 2;
        }
        else if (*p == '"' || *p == '\'')
        {
            kind = 4;
            while (*end)
            {
                if (*end == '\\' && end[1])
                    end += 2;
                else if (*end++ == *p)
                    break;
            }
        }
        else if (isalpha((unsigned char)*p) || *p == '_')
        {
            while (word(*end))
                end++;
            if (keyword(p, end - p))
                kind = 1;
            else
            {
                q = end;
                while (isspace((unsigned char)*q))
                    q++;
                if (*q == '(')
                    kind = 2;
            }
        }
        else if (isdigit((unsigned char)*p))
        {
            kind = 3;
            while (word(*end) || *end == '.')
                end++;
        }
        first = 0;
        /* Colour is reset at each newline by TextEditor, including in comments. */
        do
        {
            memcpy(w, "\033P[", 3);
            w += 3;
            memcpy(w, colour[kind], 6);
            w += 6;
            *w++ = ']';
            while (p < end && *p != '\n')
                *w++ = *p++;
            memcpy(w, "\033P[E6EDF3]", 10);
            w += 10;
            if (p < end && *p == '\n')
            {
                *w++ = *p++;
                first = 1;
            }
        } while (p < end);
    }
    *w = 0;
    return out;
}
