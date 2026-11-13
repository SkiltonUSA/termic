#include "native.h"
#include "workspace.h"
#include <ctype.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/ports.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char stack_size[] = "$STACK:262144";
int cancel_requested(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}
/* Shell V47 merges diagnostics when stdout is explicitly redirected.
 * SYS_Output alone does not provide that shell-level redirection. */
static LONG run_command(const char *command)
{
    char redirected[1200], *errors;
    LONG rc;
    BPTR capture;
    if (strlen(command) + 20 >= sizeof(redirected))
        return 20;
    capture = Open((STRPTR) ".command.log", MODE_NEWFILE);
    if (!capture)
        return 20;
    Close(capture);
    snprintf(redirected, sizeof(redirected), "%s > .command.log\n", command);
    fflush(stdout);
    rc = SystemTags((STRPTR)redirected, TAG_DONE);
    errors = read_text(".command.log", RESPONSE_LIMIT);
    if (errors && *errors)
        printf("%s", errors);
    free(errors);
    return rc;
}
int build_project(void)
{
    LONG r = run_command("Execute Termic:BuildProject");
    printf("Local compiler exit code: %ld\n", (long)r);
    return r == 0;
}
/* Resolve existing files (or a new file's parent) and reject links outside
 * the current workspace before handing content to the cloud agent. */
static int same_prefix(const char *a, const char *b, unsigned n)
{
    while (n--)
    {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    }
    return 1;
}
int project_source_allowed(const char *path)
{
    char root[512], resolved[512], parent[184];
    BPTR l;
    const char *ext;
    char *slash;
    int ok;
    if (!source_name(path))
        return 0;
    ext = strrchr(path, '.');
    if (strcmp(ext, ".c") && strcmp(ext, ".h"))
        return 0;
    l = Lock((STRPTR) "", ACCESS_READ);
    if (!l)
        return 0;
    ok = NameFromLock(l, (STRPTR)root, sizeof(root));
    UnLock(l);
    if (!ok)
        return 0;
    l = Lock((STRPTR)path, ACCESS_READ);
    if (!l)
    {
        strcpy(parent, path);
        slash = strrchr(parent, '/');
        if (slash)
            *slash = 0;
        else
            *parent = 0;
        l = Lock((STRPTR)parent, ACCESS_READ);
    }
    if (!l)
        return 0;
    ok = NameFromLock(l, (STRPTR)resolved, sizeof(resolved));
    UnLock(l);
    if (!ok)
        return 0;
    if (strlen(root) == strlen(resolved) && same_prefix(root, resolved, strlen(root)))
        return 1;
    strcat(root, "/");
    return strlen(resolved) >= strlen(root) && same_prefix(root, resolved, strlen(root));
}
static void collect_sources(const char *dir, const char *prefix, char *out, unsigned *used,
                            int depth)
{
    BPTR l;
    struct FileInfoBlock *f;
    char rel[184], sub[184];
    unsigned n;
    if (depth > 6)
        return;
    l = Lock((STRPTR)dir, ACCESS_READ);
    if (!l)
        return;
    f = AllocDosObject(DOS_FIB, NULL);
    if (f && Examine(l, f))
        while (ExNext(l, f))
        {
            if (f->fib_FileName[0] == '.' || !strcmp(f->fib_FileName, "Tasks") ||
                strlen(prefix) + strlen(f->fib_FileName) > 178)
                continue;
            sprintf(rel, "%s%s", prefix, f->fib_FileName);
            if (f->fib_DirEntryType == ST_USERDIR)
            {
                sprintf(sub, "%s/", rel);
                collect_sources(rel, sub, out, used, depth + 1);
            }
            else if (f->fib_DirEntryType == ST_FILE && project_source_allowed(rel))
            {
                n = strlen(rel);
                if (*used + n + 2 < 16384)
                {
                    strcpy(out + *used, rel);
                    *used += n;
                    out[(*used)++] = '\n';
                    out[*used] = 0;
                }
            }
        }
    if (f)
        FreeDosObject(DOS_FIB, f);
    UnLock(l);
}
char *list_project_sources(void)
{
    char *out = calloc(1, 16384);
    unsigned n = 0;
    if (out)
        collect_sources("", "", out, &n, 0);
    return out;
}
int main(int argc, char **argv)
{
    BPTR lock, old;
    int result = 20;
    FILE *f;
    struct MsgPort port;
    char portname[40];
    unsigned long hash = 5381;
    const char *p;
    const char *path = "Termic:Project", *action = argc > 1 ? argv[1] : "--agent";
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 4 && !strcmp(argv[2], "--project"))
        path = argv[3];
    else if (argc > 2)
    {
        puts("Usage: TermicAgent "
             "--agent|--build|--run|--probe|--status|--diff|--init|--commit|--shell [--project "
             "drawer]");
        return 20;
    }
    if (strcmp(action, "--agent") && strcmp(action, "--build") && strcmp(action, "--run") &&
        strcmp(action, "--probe") && strcmp(action, "--status") && strcmp(action, "--diff") &&
        strcmp(action, "--init") && strcmp(action, "--commit") && strcmp(action, "--shell"))
        return 20;
    lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock)
        return 20;
    old = CurrentDir(lock);
    lock = CreateDir((STRPTR) ".busy");
    if (!lock)
    {
        puts("Workspace busy or not writable.");
        UnLock(CurrentDir(old));
        return 20;
    }
    UnLock(lock);
    for (p = path; *p; ++p)
        hash = hash * 33 + (unsigned char)*p;
    sprintf(portname, "TermicJob.%08lx", hash);
    memset(&port, 0, sizeof(port));
    port.mp_Node.ln_Name = portname;
    port.mp_Node.ln_Type = NT_MSGPORT;
    port.mp_Flags = PA_IGNORE;
    port.mp_SigTask = FindTask(NULL);
    AddPort(&port);
    if (!strcmp(action, "--probe"))
    {
        char *reply = NULL;
        int status = cloud_request(NULL, NULL, &reply);
        free(reply);
        printf("Verified HTTPS probe: HTTP %d (401 expected without a key).\n", status);
        result = status == 401 ? 0 : 20;
    }
    else if (!strcmp(action, "--build"))
        result = build_project() ? 0 : 20;
    else if (!strcmp(action, "--run"))
        result = run_command("Program");
    else if (!strcmp(action, "--agent"))
        result = run_agent();
    else if (!strcmp(action, "--shell"))
    {
        char *command = read_text("command.txt", 1024);
        if (command && *command)
        {
            size_t n = strlen(command);
            while (n && isspace((unsigned char)command[n - 1]))
                command[--n] = 0;
            if (n)
                result = run_command(command);
        }
        free(command);
    }
    else if (!strcmp(action, "--status"))
        result = run_command("Termic:Tools/amiga-git/git status");
    else if (!strcmp(action, "--diff"))
        result = run_command("Termic:Tools/amiga-git/git diff");
    else if (!strcmp(action, "--init"))
    {
        char root[384], cmd[512];
        BPTR here = Lock((STRPTR) "", ACCESS_READ);
        if (here)
        {
            if (NameFromLock(here, (STRPTR)root, sizeof(root)))
            {
                snprintf(cmd, sizeof(cmd), "Termic:Tools/amiga-git/git init \"%s\"", root);
                result = run_command(cmd);
            }
            UnLock(here);
        }
        if (!result)
        {
            FILE *existing = fopen(".gitignore", "rb");
            if (existing)
                fclose(existing);
            else if (!save_text(".gitignore",
                                "Program\nProgram.new\n*.o\n*.asm\n*.bak\n*.new\n.busy/"
                                "\n.job*\n.result*\njob.log\n.stderr\n.command.log\nbuild."
                                "log\ntranscript.txt\nsession."
                                "txt*\nname.txt\nstate.txt\ncommand.txt\ncommit.txt\nTasks/\n",
                                1024))
                result = 20;
        }
    }
    else if (!strcmp(action, "--commit"))
    {
        char *names = list_project_sources(), *name, *next, *msg = read_text("commit.txt", 256);
        char cmd[512];
        int valid = msg && *msg;
        if (valid)
            for (name = msg; *name; ++name)
                if (!isalnum((unsigned char)*name) && !strchr(" .,:/()_-", *name))
                {
                    valid = 0;
                    break;
                }
        if (!valid)
            puts("Use letters, numbers, spaces and . , : / ( ) _ - in the commit message.");
        else if (names)
        {
            result = 0;
            for (name = names; *name && !result; name = next)
            {
                next = strchr(name, '\n');
                if (!next)
                    break;
                *next++ = 0;
                sprintf(cmd, "Termic:Tools/amiga-git/git add \"%s\"", name);
                result = run_command(cmd);
                if (cancel_requested())
                {
                    result = 20;
                    break;
                }
            }
            if (!result)
            {
                FILE *ignore = fopen(".gitignore", "rb");
                if (ignore)
                {
                    fclose(ignore);
                    result = run_command("Termic:Tools/amiga-git/git add .gitignore");
                }
            }
            if (!result)
            {
                sprintf(cmd, "Termic:Tools/amiga-git/git commit -m \"%s\"", msg);
                result = run_command(cmd);
            }
        }
        free(names);
        free(msg);
    }
    printf("\nFinished with exit code %d.\n", result);
    RemPort(&port);
    DeleteFile((STRPTR) ".busy");
    f = fopen(".result.new", "w");
    if (f)
    {
        fprintf(f, "%d\n", result);
        fclose(f);
        remove(".result");
        rename(".result.new", ".result");
    }
    UnLock(CurrentDir(old));
    return result;
}
