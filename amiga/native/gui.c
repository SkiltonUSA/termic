/* Integrated AmigaOS workspace. All MUI calls stay on the UI task; long
 * operations run in TermicAgent processes with per-workspace locking. */
#include "mui_compat.h"
#include "native.h"
#include "workspace.h"
#include <ctype.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <libraries/asl.h>
#include <mui/TextEditor_mcc.h>
#include <proto/asl.h>
#include <proto/diskfont.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <workbench/workbench.h>

struct Library *MUIMasterBase, *IconBase, *DiskfontBase, *AslBase;
struct GfxBase *GfxBase;
static struct TextFont *bodyfont, *codefont;
struct IntuitionBase *IntuitionBase;
const char stack_size[] = "$STACK:262144";
#define MAX_ITEMS 128
#define PATH_SIZE 384
static Object *app, *win, *projects, *tasks, *files, *pages, *source, *prompt, *output, *review,
    *transcript;
static Object *status, *heading, *filename, *newname, *projectname, *taskname, *model, *keyfield,
    *command, *commit, *findtext, *buildcommand, *projectdrawer;
static char project_paths[MAX_ITEMS][PATH_SIZE], task_paths[MAX_ITEMS][PATH_SIZE],
    file_names[MAX_ITEMS][184];
static char current[PATH_SIZE], opened[184], project[PATH_SIZE];
static char project_root[PATH_SIZE] = "Termic:Projects";
static int project_count, task_count, file_count, loading, source_loaded = 1, prompt_loaded = 1;
static const char *tabs[] = {"Agent", "Editor", "Build / Shell", "Changes", "Settings", NULL};
enum
{
    QUIT = 1,
    PROJECT,
    TASK,
    FILESEL,
    NEWPROJECT,
    NEWTASK,
    SAVE,
    BUILD,
    RUN,
    AGENT,
    CANCEL,
    NEWFILE,
    REFRESH,
    PROBE,
    SETTINGS,
    SAVECONFIG,
    SHELL,
    DIFF,
    GITSTATUS,
    GITINIT,
    GITCOMMIT,
    UNDO,
    REDO,
    CUT,
    COPY,
    PASTE,
    SAVETASK,
    RENAMEPROJECT,
    RENAMETASK,
    MARKDONE,
    PAGE_AGENT,
    PAGE_EDITOR,
    PAGE_OUTPUT,
    PAGE_CHANGES,
    PAGE_SETTINGS,
    FIND,
    FINDNEXT,
    SAVEBUILD,
    NEWSESSION,
    IMPORTPROJECT,
    FOCUSFILE,
    FOCUSEDITOR,
    FOCUSCOMMIT,
    FOCUSCOMMAND
};
struct Job
{
    char path[PATH_SIZE];
    int active, polls;
    long bytes;
} jobs[16];
static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static int timer_open, timer_pending;
static ULONG value(Object *o, ULONG a)
{
    ULONG v = 0;
    get(o, a, &v);
    return v;
}
static void notify(Object *o, ULONG a, ULONG v, ULONG id)
{
    DoMethod(o, MUIM_Notify, a, v, app, 2, MUIM_Application_ReturnID, id);
}
static Object *label(const char *s)
{
    return TextObject, MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_Normal, MUIA_Text_Contents,
           s, End;
}
static Object *button(const char *s, int id)
{
    Object *o = TextObject, ButtonFrame, MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_Button,
           MUIA_InnerTop, 4, MUIA_InnerBottom, 4, MUIA_Background, MUII_ButtonBack, MUIA_InputMode,
           MUIV_InputMode_RelVerify, MUIA_Text_Contents, s, MUIA_Text_PreParse, "\33c",
           MUIA_CycleChain, 1, End;
    notify(o, MUIA_Pressed, FALSE, id);
    return o;
}
static Object *entry(unsigned max)
{
    return StringObject, StringFrame, MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_Normal,
           MUIA_String_MaxLen, max, MUIA_CycleChain, 1, End;
}
static Object *list(Object **o)
{
    return ListviewObject, MUIA_Listview_List,
           *o = ListObject, InputListFrame, MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_List,
           MUIA_List_ConstructHook, MUIV_List_ConstructHook_String, MUIA_List_DestructHook,
           MUIV_List_DestructHook_String, End, End;
}
static Object *editor(Object **o, int readonly)
{
    Object *slider;
    Object *g = HGroup, Child, *o = TextEditorObject, InputListFrame, MUIA_Font,
           codefont ? (ULONG)codefont : MUIV_Font_Fixed, MUIA_TextEditor_ReadOnly, readonly,
           MUIA_TextEditor_ImportHook, MUIV_TextEditor_ImportHook_Plain, MUIA_TextEditor_ExportHook,
           MUIV_TextEditor_ExportHook_Plain, MUIA_TextEditor_WrapMode,
           MUIV_TextEditor_WrapMode_NoWrap, MUIA_CycleChain, 1, End, Child,
           slider = ScrollbarObject, End, End;
    set(*o, MUIA_TextEditor_Slider, slider);
    return g;
}
static void message(const char *s)
{
    set(status, MUIA_Text_Contents, s);
}
static int exists(const char *p)
{
    BPTR l = Lock((STRPTR)p, ACCESS_READ);
    if (!l)
        return 0;
    UnLock(l);
    return 1;
}
static int mkdir_safe(const char *p)
{
    BPTR l = CreateDir((STRPTR)p);
    if (l)
    {
        UnLock(l);
        return 1;
    }
    return exists(p);
}
static void join(char *out, const char *dir, const char *name)
{
    snprintf(out, PATH_SIZE, "%s/%s", dir, name);
}
static int busy(const char *p)
{
    char path[PATH_SIZE];
    int i;
    for (i = 0; i < 16; i++)
        if (jobs[i].active && !strcmp(jobs[i].path, p))
            return 1;
    join(path, p, ".busy");
    return exists(path);
}
static void content(Object *o, const char *p, const char *fallback)
{
    char *s = read_text(p, o == source ? SOURCE_LIMIT : o == prompt ? 8192 : RESPONSE_LIMIT);
    int valid = s != NULL || !exists(p);
    if (o == source)
        source_loaded = valid;
    if (o == prompt)
        prompt_loaded = valid;
    if (!valid && (o == source || o == prompt))
    {
        fallback = "File could not be loaded (binary, unreadable or above the editor size limit).";
        set(o, MUIA_TextEditor_ReadOnly, TRUE);
    }
    set(o, MUIA_TextEditor_Contents, s ? s : fallback);
    free(s);
    set(o, MUIA_TextEditor_HasChanged, FALSE);
}
static int save_editor(Object *o, const char *name, unsigned long limit)
{
    char path[PATH_SIZE], *s;
    int ok;
    if (!*current || busy(current) || (o == source && !source_loaded) ||
        (o == prompt && !prompt_loaded))
    {
        message("Workspace is busy. Wait or cancel before saving.");
        return 0;
    }
    join(path, current, name);
    s = (char *)DoMethod(o, MUIM_TextEditor_ExportText);
    if (!s)
        return 0;
    ok = save_text(path, s, limit);
    FreeVec(s);
    if (ok)
        set(o, MUIA_TextEditor_HasChanged, FALSE);
    else
        message("Save failed. Previous file is preserved; check size and free disk space.");
    return ok;
}
static int save_current(void)
{
    if (*opened && value(source, MUIA_TextEditor_HasChanged) &&
        !save_editor(source, opened, SOURCE_LIMIT))
        return 0;
    if (value(prompt, MUIA_TextEditor_HasChanged) && !save_editor(prompt, "task.txt", 8192))
        return 0;
    return 1;
}
static void arm_timer(void);
static int worker_registered(const char *path)
{
    unsigned long hash = 5381;
    char portname[40];
    const char *p;
    struct MsgPort *port;
    for (p = path; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    sprintf(portname, "TermicJob.%08lx", hash);
    Forbid();
    port = FindPort((STRPTR)portname);
    Permit();
    return port != NULL;
}
static void attach_job(const char *path)
{
    int i, free_slot = -1;
    if (!worker_registered(path))
        return;
    for (i = 0; i < 16; i++)
    {
        if (jobs[i].active && !strcmp(jobs[i].path, path))
            return;
        if (!jobs[i].active)
            free_slot = i;
    }
    if (free_slot >= 0)
    {
        strcpy(jobs[free_slot].path, path);
        jobs[free_slot].active = 1;
        jobs[free_slot].polls = 0;
        jobs[free_slot].bytes = -1;
        arm_timer();
    }
}
static int internal_name(const char *n)
{
    return !strcmp(n, "name.txt") || !strcmp(n, "state.txt") || !strcmp(n, "session.txt") ||
           !strcmp(n, "commit.txt") || !strcmp(n, "command.txt") || !strcmp(n, "transcript.txt");
}
static int managed_path(const char *p)
{
    const char *s = p;
    if ((strncmp(p, project_root, strlen(project_root)) || p[strlen(project_root)] != '/') &&
        strncmp(p, "Termic:Project", 14))
        return 0;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
              strchr(":/_-", *s)))
            return 0;
    return strlen(p) < PATH_SIZE - 184;
}
static char *read_value(const char *path, unsigned limit)
{
    char *s = read_text(path, limit);
    unsigned i;
    if (s)
    {
        s[strcspn(s, "\r\n")] = 0;
        for (i = 0; s[i]; i++)
            if ((unsigned char)s[i] < 32 || (unsigned char)s[i] == 155)
                s[i] = ' ';
    }
    return s;
}
static void task_row(const char *p)
{
    char path[PATH_SIZE], labeltext[180], *name, *state;
    int i, n = value(tasks, MUIA_List_Active);
    for (i = 0; i < task_count; i++)
        if (!strcmp(task_paths[i], p))
            break;
    if (i == task_count)
        return;
    join(path, p, "name.txt");
    name = read_value(path, 120);
    join(path, p, "state.txt");
    state = read_value(path, 32);
    snprintf(labeltext, sizeof(labeltext), "%s [%s]", name ? name : (i ? "Task" : "Main workspace"),
             state ? state : "Ready");
    DoMethod(tasks, MUIM_KillNotify, MUIA_List_Active);
    DoMethod(tasks, MUIM_List_Remove, i);
    DoMethod(tasks, MUIM_List_InsertSingle, labeltext, i);
    set(tasks, MUIA_List_Active, n);
    notify(tasks, MUIA_List_Active, MUIV_EveryTime, TASK);
    free(name);
    free(state);
}
static void scan_files(const char *dir, const char *prefix, int depth)
{
    BPTR l;
    struct FileInfoBlock *f;
    char path[PATH_SIZE], rel[184];
    if (depth > 6 || file_count >= MAX_ITEMS)
        return;
    l = Lock((STRPTR)dir, ACCESS_READ);
    if (!l)
        return;
    f = AllocDosObject(DOS_FIB, NULL);
    if (f && Examine(l, f))
        while (ExNext(l, f) && file_count < MAX_ITEMS)
        {
            if (f->fib_FileName[0] == '.' || internal_name(f->fib_FileName) ||
                !strcmp(f->fib_FileName, "task.txt") ||
                !strcmp(f->fib_FileName, "build-command.txt") || !strcmp(f->fib_FileName, "Tasks"))
                continue;
            if (strlen(prefix) + strlen(f->fib_FileName) + 2 >= sizeof(rel))
                continue;
            sprintf(rel, "%s%s", prefix, f->fib_FileName);
            if (f->fib_DirEntryType == ST_USERDIR)
            {
                char sub[184];
                join(path, dir, f->fib_FileName);
                if (strlen(rel) > 180)
                    continue;
                sprintf(sub, "%s/", rel);
                scan_files(path, sub, depth + 1);
            }
            else if (f->fib_DirEntryType == ST_FILE && source_name(rel))
            {
                strcpy(file_names[file_count++], rel);
                DoMethod(files, MUIM_List_InsertSingle, rel, MUIV_List_Insert_Bottom);
            }
        }
    if (f)
        FreeDosObject(DOS_FIB, f);
    UnLock(l);
}
static void refresh_files(void)
{
    loading = 1;
    DoMethod(files, MUIM_KillNotify, MUIA_List_Active);
    DoMethod(files, MUIM_List_Clear);
    file_count = 0;
    scan_files(current, "", 0);
    notify(files, MUIA_List_Active, MUIV_EveryTime, FILESEL);
    loading = 0;
}
static void open_file(const char *name)
{
    char path[PATH_SIZE];
    if (!save_current())
        return;
    strcpy(opened, name);
    join(path, current, name);
    content(source, path, "");
    set(source, MUIA_TextEditor_ReadOnly, busy(current) || !source_loaded);
    set(filename, MUIA_Text_Contents, name);
}
static void load_workspace(const char *p)
{
    char path[PATH_SIZE], title[420];
    strcpy(current, p);
    attach_job(p);
    save_text("Termic:Config/last-workspace", p, PATH_SIZE);
    *opened = 0;
    refresh_files();
    open_file("main.c");
    join(path, current, "task.txt");
    content(prompt, path, "");
    join(path, current, "transcript.txt");
    content(
        transcript, path,
        "Describe a task below, then choose Start agent.\nThe agent can read source, edit it, and "
        "invoke the local compiler.\nConfigure your API key in Settings before the first request.");
    join(path, current, "build-command.txt");
    {
        char *s = read_text(path, 512);
        set(buildcommand, MUIA_String_Contents,
            s ? s : "vc +aos68k -cpu=68020 -O1 main.c -o Program.new -lamiga");
        free(s);
    }
    join(path, current, "job.log");
    content(output, path, "Build output and explicit shell commands appear here.");
    join(path, current, "name.txt");
    {
        char *s = read_value(path, 120);
        set(taskname, MUIA_String_Contents, s ? s : "Main workspace");
        free(s);
    }
    task_row(current);
    sprintf(title, "\33b%s", current);
    set(heading, MUIA_Text_Contents, title);
    set(source, MUIA_TextEditor_ReadOnly, busy(current) || !source_loaded);
    set(prompt, MUIA_TextEditor_ReadOnly, busy(current) || !prompt_loaded);
    message(busy(current) ? "Running in this workspace. Other workspaces remain available."
                          : "Ready. Changes are saved when switching files or workspaces.");
}
static void scan_drawers(Object *listobj, const char *base, char paths[][PATH_SIZE], int *count)
{
    BPTR l;
    struct FileInfoBlock *f;
    char path[PATH_SIZE], namepath[PATH_SIZE], *name;
    *count = 0;
    DoMethod(listobj, MUIM_List_Clear);
    l = Lock((STRPTR)base, ACCESS_READ);
    if (!l)
        return;
    f = AllocDosObject(DOS_FIB, NULL);
    if (f && Examine(l, f))
        while (ExNext(l, f) && *count < MAX_ITEMS)
        {
            if (f->fib_DirEntryType != ST_USERDIR || f->fib_FileName[0] == '.')
                continue;
            join(path, base, f->fib_FileName);
            if (!managed_path(path))
                continue;
            strcpy(paths[*count], path);
            ++*count;
            join(namepath, path, "name.txt");
            name = read_value(namepath, 120);
            DoMethod(listobj, MUIM_List_InsertSingle, name && *name ? name : f->fib_FileName,
                     MUIV_List_Insert_Bottom);
            free(name);
        }
    if (f)
        FreeDosObject(DOS_FIB, f);
    UnLock(l);
}
static void load_tasks(void)
{
    char path[PATH_SIZE], *project_label;
    join(path, project, "name.txt");
    project_label = read_value(path, 120);
    set(projectname, MUIA_String_Contents, project_label ? project_label : "Starter project");
    free(project_label);
    loading = 1;
    DoMethod(tasks, MUIM_KillNotify, MUIA_List_Active);
    join(path, project, "Tasks");
    scan_drawers(tasks, path, task_paths, &task_count);
    if (task_count < MAX_ITEMS)
    {
        memmove(task_paths + 1, task_paths, task_count * PATH_SIZE);
        strcpy(task_paths[0], project);
        task_count++;
        DoMethod(tasks, MUIM_List_InsertSingle, "Main workspace", MUIV_List_Insert_Top);
    }
    set(tasks, MUIA_List_Active, 0);
    notify(tasks, MUIA_List_Active, MUIV_EveryTime, TASK);
    loading = 0;
    load_workspace(project);
}
static void load_projects(void)
{
    char path[PATH_SIZE];
    char *name;
    loading = 1;
    DoMethod(projects, MUIM_KillNotify, MUIA_List_Active);
    scan_drawers(projects, project_root, project_paths, &project_count);
    if (project_count < MAX_ITEMS)
    {
        memmove(project_paths + 1, project_paths, project_count * PATH_SIZE);
        strcpy(project_paths[0], "Termic:Project");
        project_count++;
        DoMethod(projects, MUIM_List_InsertSingle, "Starter project", MUIV_List_Insert_Top);
    }
    set(projects, MUIA_List_Active, 0);
    notify(projects, MUIA_List_Active, MUIV_EveryTime, PROJECT);
    loading = 0;
    strcpy(project, project_paths[0]);
    join(path, project, "name.txt");
    name = read_value(path, 120);
    set(projectname, MUIA_String_Contents, name ? name : "Starter project");
    free(name);
    load_tasks();
}
/* Clone only regular editable files. Never follow links or copy credentials,
 * build products, Git internals or active job state into a new task. */
static int clone_sources(const char *from, const char *to, int depth)
{
    BPTR l;
    struct FileInfoBlock *f;
    char src[PATH_SIZE], dst[PATH_SIZE];
    int ok = 1;
    char *text;
    if (depth > 6)
        return 0;
    l = Lock((STRPTR)from, ACCESS_READ);
    if (!l)
        return 0;
    f = AllocDosObject(DOS_FIB, NULL);
    if (!f || !Examine(l, f))
        ok = 0;
    else
        while (ok && ExNext(l, f))
        {
            if (f->fib_FileName[0] == '.' || internal_name(f->fib_FileName) ||
                !strcmp(f->fib_FileName, "Tasks") || !strcmp(f->fib_FileName, "name.txt") ||
                !strcmp(f->fib_FileName, "transcript.txt") ||
                !strcmp(f->fib_FileName, "command.txt"))
                continue;
            if (strlen(from) + strlen(f->fib_FileName) + 2 >= PATH_SIZE ||
                strlen(to) + strlen(f->fib_FileName) + 2 >= PATH_SIZE)
            {
                ok = 0;
                break;
            }
            join(src, from, f->fib_FileName);
            join(dst, to, f->fib_FileName);
            if (f->fib_DirEntryType == ST_USERDIR)
                ok = mkdir_safe(dst) && clone_sources(src, dst, depth + 1);
            else if (f->fib_DirEntryType == ST_FILE && source_name(f->fib_FileName))
            {
                text = read_text(src, SOURCE_LIMIT);
                ok = text && save_text(dst, text, SOURCE_LIMIT);
                free(text);
            }
        }
    if (f)
        FreeDosObject(DOS_FIB, f);
    UnLock(l);
    return ok;
}
static void create_workspace(int task)
{
    char base[PATH_SIZE], path[PATH_SIZE], namepath[PATH_SIZE], name[80];
    int n;
    if (task && busy(current))
    {
        message("Wait for this workspace job before copying its source into a new task.");
        return;
    }
    if (!save_current())
        return;
    if (task)
    {
        join(base, project, "Tasks");
        mkdir_safe(base);
    }
    else
        strcpy(base, project_root);
    for (n = 1; n < 10000; n++)
    {
        snprintf(path, sizeof(path), "%s/%s-%04d", base, task ? "task" : "project", n);
        if (!exists(path))
            break;
    }
    if (n == 10000 || !mkdir_safe(path))
    {
        message("Cannot create workspace.");
        return;
    }
    if (!clone_sources(task ? current : "Termic:Template", path, 0))
    {
        message("Copy failed. Partial drawer retained for recovery.");
        return;
    }
    sprintf(name, "%s %d", task ? "Task" : "Project", n);
    join(namepath, path, "name.txt");
    save_text(namepath, name, 120);
    if (task)
    {
        load_tasks();
        {
            int i;
            for (i = 0; i < task_count; i++)
                if (!strcmp(task_paths[i], path))
                    SetAttrs(tasks, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
        }
        load_workspace(path);
    }
    else
    {
        load_projects();
        {
            int i;
            for (i = 0; i < project_count; i++)
                if (!strcmp(project_paths[i], path))
                    SetAttrs(projects, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
        }
        strcpy(project, path);
        load_tasks();
    }
    message("Workspace created. Rename it below the list. Task files are independent snapshots.");
}
static void arm_timer(void)
{
    if (!timer_io || timer_pending)
        return;
    timer_io->tr_node.io_Command = TR_ADDREQUEST;
    timer_io->tr_time.tv_secs = 0;
    timer_io->tr_time.tv_micro = 500000;
    SendIO((struct IORequest *)timer_io);
    timer_pending = 1;
}
static int any_jobs(void)
{
    int i;
    for (i = 0; i < 16; i++)
        if (jobs[i].active)
            return 1;
    return 0;
}
static void launch(const char *action, int page)
{
    char path[PATH_SIZE], line[512];
    BPTR in, out;
    int i;
    if (!save_current())
        return;
    if (busy(current))
    {
        message("A job is already running in this workspace.");
        return;
    }
    for (i = 0; i < 16 && jobs[i].active; i++)
        ;
    if (i == 16)
    {
        message("All 16 worker slots are in use.");
        return;
    }
    join(path, current, ".result");
    DeleteFile((STRPTR)path);
    join(path, current, "job.log");
    out = Open((STRPTR)path, MODE_READWRITE);
    in = Open((STRPTR) "NIL:", MODE_OLDFILE);
    if (!out || !in)
    {
        if (out)
            Close(out);
        if (in)
            Close(in);
        message("Cannot open job log.");
        return;
    }
    SetFileSize(out, 0, OFFSET_BEGINNING);
    sprintf(line, "Stack 262144\nTermic:TermicAgent %s --project \"%s\"\n", action, current);
    join(path, current, ".job");
    if (!save_text(path, line, 1024))
    {
        Close(in);
        Close(out);
        message("Cannot write worker script.");
        return;
    }
    sprintf(line, "Execute \"%s\"", path);
    if (SystemTags((STRPTR)line, SYS_Input, in, SYS_Output, out, SYS_Asynch, TRUE, TAG_DONE) == -1)
    {
        Close(in);
        Close(out);
        message("Cannot launch worker.");
        return;
    }
    strcpy(jobs[i].path, current);
    jobs[i].active = 1;
    jobs[i].polls = 0;
    jobs[i].bytes = -1;
    join(path, current, "state.txt");
    save_text(path, "Running", 32);
    task_row(current);
    set(source, MUIA_TextEditor_ReadOnly, TRUE);
    set(prompt, MUIA_TextEditor_ReadOnly, TRUE);
    set(pages, MUIA_Group_ActivePage, page);
    set(output, MUIA_TextEditor_Contents, "Starting native worker...\n");
    message("Running. You can switch tasks while this worker continues.");
    arm_timer();
}
static void poll_jobs(void)
{
    int i;
    char path[PATH_SIZE], *s;
    for (i = 0; i < 16; i++)
        if (jobs[i].active)
        {
            join(path, jobs[i].path, "job.log");
            if (!strcmp(current, jobs[i].path))
            {
                s = read_text(path, RESPONSE_LIMIT);
                if (s)
                {
                    long n = strlen(s);
                    if (n != jobs[i].bytes)
                    {
                        set(output, MUIA_TextEditor_Contents, s);
                        if (value(pages, MUIA_Group_ActivePage) == 3)
                            set(review, MUIA_TextEditor_Contents, s);
                        jobs[i].bytes = n;
                    }
                    free(s);
                }
            }
            join(path, jobs[i].path, ".result");
            s = read_text(path, 32);
            if (!s && ++jobs[i].polls > 20 && !worker_registered(jobs[i].path))
            {
                jobs[i].active = 0;
                if (!strcmp(current, jobs[i].path))
                    message("Worker exited without a result. Check its log and any stale .busy "
                            "drawer.");
            }
            if (s)
            {
                jobs[i].active = 0;
                join(path, jobs[i].path, "state.txt");
                save_text(path, atoi(s) == 0 ? "Review" : "Needs attention", 32);
                task_row(jobs[i].path);
                if (!strcmp(current, jobs[i].path))
                {
                    set(source, MUIA_TextEditor_ReadOnly, !source_loaded);
                    set(prompt, MUIA_TextEditor_ReadOnly, !prompt_loaded);
                    message(atoi(s) == 0
                                ? "Job completed successfully. Review changes before running."
                                : "Job failed or was cancelled. See Build / Shell for details.");
                    join(path, current, "transcript.txt");
                    content(transcript, path, "No agent transcript for this task.");
                    refresh_files();
                    if (*opened && !value(source, MUIA_TextEditor_HasChanged))
                    {
                        join(path, current, opened);
                        content(source, path, "");
                    }
                }
                free(s);
            }
        }
    if (any_jobs())
        arm_timer();
}
static void cancel(void)
{
    unsigned long hash = 5381;
    const char *p;
    char name[40];
    struct MsgPort *port;
    for (p = current; *p; p++)
        hash = hash * 33 + (unsigned char)*p;
    sprintf(name, "TermicJob.%08lx", hash);
    Forbid();
    port = FindPort((STRPTR)name);
    if (port)
        Signal(port->mp_SigTask, SIGBREAKF_CTRL_C);
    Permit();
    message(port ? "Cancellation requested. Compiler or network operation will finish unwinding."
                 : "No worker is currently registered for this workspace.");
}
static void configure(void)
{
    char *s = (char *)value(model, MUIA_String_Contents);
    char *k = (char *)value(keyfield, MUIA_String_Contents);
    char *drawer = (char *)value(projectdrawer, MUIA_String_Contents);
    unsigned i;
    int ok = save_text("Termic:Config/model", s, 128), valid = *drawer != 0;
    for (i = 0; drawer[i]; i++)
        if (!isalnum((unsigned char)drawer[i]) && !strchr(":/_-", drawer[i]))
            valid = 0;
    if (!valid || !save_text("Termic:Config/projects-drawer", drawer, 180))
        ok = 0;
    if (*k)
    {
        FILE *f = fopen("Termic:Config/api-key", "wb");
        if (!f)
            ok = 0;
        else
        {
            if (fwrite(k, 1, strlen(k), f) != strlen(k))
                ok = 0;
            if (fclose(f))
                ok = 0;
        }
        set(keyfield, MUIA_String_Contents, "");
    }
    message(ok ? "Settings saved. Existing key is retained when the key field is blank."
               : "Settings could not be saved.");
}
static int icon(const char *p)
{
    struct DiskObject *d;
    int ok = 0;
    IconBase = OpenLibrary((STRPTR) "icon.library", 37);
    if (!IconBase)
        return 20;
    d = GetDefDiskObject(WBTOOL);
    if (d)
    {
        d->do_StackSize = 262144;
        d->do_CurrentX = d->do_CurrentY = NO_ICON_POSITION;
        ok = PutDiskObject((STRPTR)p, d);
        FreeDiskObject(d);
    }
    CloseLibrary(IconBase);
    return ok ? 0 : 20;
}
static int build_ui(void)
{
    Object *body, *left, *right, *center;
    char *s;
    app = ApplicationObject, MUIA_Application_Title, "Termic", MUIA_Application_Base, "TERMIC",
    MUIA_Application_SingleTask, TRUE, MUIA_Application_Description,
    "Native Amiga development workspace", End;
    if (!app)
        return 0;
    left = VGroup, MUIA_Group_Spacing, 6, MUIA_Weight, 22, Child, label("\33bPROJECTS"), Child,
    list(&projects), Child, projectname = entry(120), Child, HGroup, Child,
    button("New project", NEWPROJECT), Child, button("Rename", RENAMEPROJECT), End, Child,
    button("Import source drawer", IMPORTPROJECT), Child, label("\33bTASKS / WORKSPACES"), Child,
    list(&tasks), Child, taskname = entry(120), Child, HGroup, Child, button("New task", NEWTASK),
    Child, button("Rename", RENAMETASK), End, Child, button("Mark done", MARKDONE), End;
    center = VGroup, MUIA_Weight, 65, Child, heading = label("Termic workspace"), Child,
    pages = RegisterGroup(tabs), MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_Normal, Child,
    VGroup, Child, editor(&transcript, 1), Child, label("\33bTask instructions"), Child,
    editor(&prompt, 0), Child, HGroup, Child, button("Save task", SAVETASK), Child,
    button("Start agent", AGENT), Child, button("Cancel", CANCEL), Child,
    button("New conversation", NEWSESSION), End, End, Child, VGroup, Child,
    filename = label("main.c"), Child, HGroup, Child, button("Save", SAVE), Child,
    button("Undo", UNDO), Child, button("Redo", REDO), Child, button("Cut", CUT), Child,
    button("Copy", COPY), Child, button("Paste", PASTE), End, Child, editor(&source, 0), Child,
    HGroup, Child, findtext = entry(120), Child, button("Find", FIND), Child,
    button("Next", FINDNEXT), End, End, Child, VGroup, Child, editor(&output, 1), Child,
    label("AmigaDOS command (runs in this workspace)"), Child, command = entry(1024), Child, HGroup,
    Child, button("Execute", SHELL), Child, button("Cancel job", CANCEL), End, End, Child, VGroup,
    Child, HGroup, Child, button("Status", GITSTATUS), Child, button("Diff", DIFF), Child,
    button("Init repository", GITINIT), End, Child, editor(&review, 1), Child,
    label("Commit message (configure Git identity in the shell first)"), Child, commit = entry(256),
    Child, button("Stage source and commit", GITCOMMIT), End, Child, VGroup, Child,
    label("\33bCLOUD AGENT"), Child, label("Model"), Child, model = entry(128), Child,
    label("API key (blank preserves existing key; never displayed)"), Child,
    keyfield = StringObject, StringFrame, MUIA_String_Secret, TRUE, MUIA_String_MaxLen, 512,
    MUIA_CycleChain, 1, End, Child, button("Save settings", SAVECONFIG), Child,
    button("Test verified HTTPS", PROBE), Child, label("\33bNATIVE DEVELOPMENT"), Child,
    label("Project drawer (restart required after changing)"), Child, projectdrawer = entry(180),
    Child, label("Build command for this workspace (must produce Program.new)"), Child,
    buildcommand = entry(512), Child, button("Save build command", SAVEBUILD), Child,
    label("VBCC / NDK 3.2. Build and Run execute on the Amiga.\nTasks use independent source "
          "snapshots.\nReadable disk fonts are used when installed; MUI supplies colours and split "
          "panes.\nZZ9000 "
          "provides the RTG display. ARM jobs require an exclusive core lease.\nCloud inference "
          "needs an API key and an internet connection."),
    Child, HVSpace, End, End, End;
    right = VGroup, MUIA_Group_Spacing, 6, MUIA_Weight, 23, Child, label("\33bFILES"), Child,
    list(&files), Child, newname = entry(180), Child, button("Create file", NEWFILE), Child,
    button("Refresh files", REFRESH), Child,
    label("C, H, text and Markdown\nSelect to edit.\nSaves keep .bak copies."), End;
    body = VGroup, MUIA_Font, bodyfont ? (ULONG)bodyfont : MUIV_Font_Normal, MUIA_Group_Spacing, 7,
    InnerSpacing(8, 6), Child, HGroup, Child, label("\33bTERMIC  /  AMIGA"), Child, HVSpace, Child,
    button("Build", BUILD), Child, button("Run", RUN), Child, button("Settings", SETTINGS), Child,
    button("Quit", QUIT), End, Child, HGroup, Child, left, Child, BalanceObject, End, Child, center,
    Child, BalanceObject, End, Child, right, End, Child, status = label("Ready"), End;
    win = WindowObject, MUIA_Window_Title, "Termic Amiga Workspace", MUIA_Window_ID,
    MAKE_ID('T', 'R', 'M', 'C'), MUIA_Window_Width, 1400, MUIA_Window_Height, 650, WindowContents,
    body, End;
    if (!win)
        return 0;
    DoMethod(app, OM_ADDMEMBER, win);
    notify(win, MUIA_Window_CloseRequest, TRUE, QUIT);
    notify(projects, MUIA_List_Active, MUIV_EveryTime, PROJECT);
    notify(tasks, MUIA_List_Active, MUIV_EveryTime, TASK);
    notify(files, MUIA_List_Active, MUIV_EveryTime, FILESEL);
    {
        const char *keys[] = {"-repeat f1", "-repeat f2", "-repeat f3", "-repeat f4",
                              "-repeat f5", "-repeat f6", "-repeat f7", "-repeat f8",
                              "-repeat f9", "-repeat f10"};
        int ids[] = {PAGE_AGENT, PAGE_EDITOR, PAGE_OUTPUT, PAGE_CHANGES, PAGE_SETTINGS,
                     BUILD,      RUN,         SAVE,        NEWTASK,      QUIT};
        int i;
        for (i = 0; i < 10; i++)
            notify(win, MUIA_Window_InputEvent, (ULONG)keys[i], ids[i]);
    }
    {
        const char *keys[] = {"shift f1", "shift f2", "shift f3", "shift f4", "shift f5",
                              "shift f6", "shift f7", "shift f8", "shift f9", "shift f10"};
        int ids[] = {AGENT,     FOCUSFILE, FOCUSCOMMAND, FOCUSCOMMIT, GITINIT,
                     GITSTATUS, DIFF,      MARKDONE,     NEWPROJECT,  CANCEL};
        int i;
        for (i = 0; i < 10; i++)
            notify(win, MUIA_Window_InputEvent, (ULONG)keys[i], ids[i]);
    }
    notify(newname, MUIA_String_Acknowledge, MUIV_EveryTime, NEWFILE);
    notify(commit, MUIA_String_Acknowledge, MUIV_EveryTime, GITCOMMIT);
    notify(command, MUIA_String_Acknowledge, MUIV_EveryTime, SHELL);
    s = read_text("Termic:Config/model", 128);
    if (s)
    {
        s[strcspn(s, "\r\n")] = 0;
        set(model, MUIA_String_Contents, s);
        free(s);
    }
    set(projectdrawer, MUIA_String_Contents, project_root);
    set(transcript, MUIA_Weight, 75);
    set(prompt, MUIA_Weight, 25);
    return 1;
}
int main(int argc, char **argv)
{
    ULONG id, signals, received;
    int done = 0, result = 20, storage_failed = 0;
    BPTR root;
    char path[PATH_SIZE];
    if (argc == 3 && !strcmp(argv[1], "--icon"))
        return icon(argv[2]);
    root = DupLock(GetProgramDir());
    if (!root)
        return 20;
    if (!AssignLock((STRPTR) "Termic", root))
    {
        UnLock(root);
        return 20;
    }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR) "intuition.library", 37);
    MUIMasterBase = OpenLibrary((STRPTR) "muimaster.library", 19);
    if (!IntuitionBase || !MUIMasterBase)
        goto cleanup;
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR) "graphics.library", 37);
    AslBase = OpenLibrary((STRPTR) "asl.library", 37);
    DiskfontBase = OpenLibrary((STRPTR) "diskfont.library", 37);
    if (GfxBase && DiskfontBase)
    {
        struct TextAttr a = {(STRPTR) "helvetica.font", 13, 0, FPF_DISKFONT},
                        b = {(STRPTR) "courier.font", 13, 0, FPF_DISKFONT};
        bodyfont = OpenDiskFont(&a);
        codefont = OpenDiskFont(&b);
    }
    {
        char *configured = read_value("Termic:Config/projects-drawer", 180);
        if (configured && *configured)
        {
            unsigned i;
            int valid = 1;
            for (i = 0; configured[i]; i++)
                if (!isalnum((unsigned char)configured[i]) && !strchr(":/_-", configured[i]))
                    valid = 0;
            if (valid)
                strcpy(project_root, configured);
        }
        free(configured);
        if (!strncmp(project_root, "TMGIT:", 6) &&
            SystemTags((STRPTR) "Termic:TermicStorage", TAG_DONE))
        {
            strcpy(project_root, "Termic:Projects");
            storage_failed = 1;
        }
    }
    if (!build_ui())
    {
        puts("MUI 3.8 and TextEditor.mcc 15.56 are required. Run InstallUI.");
        goto cleanup;
    }
    mkdir_safe(project_root);
    mkdir_safe("Termic:Config");
    {
        char *last = argc == 3 && !strcmp(argv[1], "--workspace")
                         ? NULL
                         : read_text("Termic:Config/last-workspace", PATH_SIZE);
        char desired[PATH_SIZE];
        const char *p = argc == 3 && !strcmp(argv[1], "--workspace") ? argv[2] : last;
        *desired = 0;
        if (p && managed_path(p) && exists(p))
            strcpy(desired, p);
        free(last);
        load_projects();
        if (*desired)
        {
            char *sub;
            int i;
            strcpy(project, desired);
            sub = strstr(project, "/Tasks/");
            if (sub)
                *sub = 0;
            for (i = 0; i < project_count; i++)
                if (!strcmp(project_paths[i], project))
                    SetAttrs(projects, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
            load_tasks();
            for (i = 0; i < task_count; i++)
                if (!strcmp(task_paths[i], desired))
                    SetAttrs(tasks, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
            load_workspace(desired);
        }
    }
    timer_port = CreateMsgPort();
    if (timer_port)
        timer_io = (struct timerequest *)CreateIORequest(timer_port, sizeof(*timer_io));
    if (!timer_io || OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)timer_io, 0))
    {
        message("Cannot open timer.device.");
        goto cleanup;
    }
    timer_open = 1;
    if (any_jobs())
        arm_timer();
    if (storage_failed)
        message(
            "Project volume could not mount. Using Termic:Projects; check TermicStorage in Shell.");
    set(win, MUIA_Window_Open, TRUE);
    if (!value(win, MUIA_Window_Open))
        goto cleanup;
    while (!done)
    {
        signals = 0;
        while ((id = DoMethod(app, MUIM_Application_NewInput, &signals)) != 0)
        {
            int n;
            char *name;
            switch (id)
            {
            case FOCUSFILE:
                set(win, MUIA_Window_ActiveObject, newname);
                break;
            case FOCUSEDITOR:
                set(pages, MUIA_Group_ActivePage, 1);
                set(win, MUIA_Window_ActiveObject, source);
                break;
            case FOCUSCOMMIT:
                set(pages, MUIA_Group_ActivePage, 3);
                set(win, MUIA_Window_ActiveObject, commit);
                break;
            case FOCUSCOMMAND:
                set(pages, MUIA_Group_ActivePage, 2);
                set(win, MUIA_Window_ActiveObject, command);
                break;
            case PAGE_AGENT:
            case PAGE_EDITOR:
            case PAGE_OUTPUT:
            case PAGE_CHANGES:
            case PAGE_SETTINGS:
                set(win, MUIA_Window_ActiveObject, NULL);
                set(pages, MUIA_Group_ActivePage, id - PAGE_AGENT);
                if (id == PAGE_EDITOR)
                    set(win, MUIA_Window_ActiveObject, source);
                break;
            case QUIT:
            case MUIV_Application_ReturnID_Quit:
                if (save_current())
                {
                    done = 1;
                }
                break;
            case PROJECT:
                if (!loading && !save_current())
                {
                    int i;
                    for (i = 0; i < project_count; i++)
                        if (!strcmp(project_paths[i], project))
                            SetAttrs(projects, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
                    break;
                }
                if (!loading)
                {
                    n = value(projects, MUIA_List_Active);
                    if (n >= 0 && n < project_count)
                    {
                        strcpy(project, project_paths[n]);
                        join(path, project, "name.txt");
                        name = read_value(path, 120);
                        set(projectname, MUIA_String_Contents, name ? name : "Starter project");
                        free(name);
                        load_tasks();
                    }
                }
                break;
            case TASK:
                if (!loading && !save_current())
                {
                    int i;
                    for (i = 0; i < task_count; i++)
                        if (!strcmp(task_paths[i], current))
                            SetAttrs(tasks, MUIA_NoNotify, TRUE, MUIA_List_Active, i, TAG_DONE);
                    break;
                }
                if (!loading)
                {
                    n = value(tasks, MUIA_List_Active);
                    if (n >= 0 && n < task_count)
                        load_workspace(task_paths[n]);
                }
                break;
            case FILESEL:
                if (loading)
                    break;
                n = value(files, MUIA_List_Active);
                if (n >= 0 && n < file_count)
                {
                    open_file(file_names[n]);
                    set(pages, MUIA_Group_ActivePage, 1);
                    set(win, MUIA_Window_ActiveObject, source);
                }
                break;
            case IMPORTPROJECT:
                if (save_current() && AslBase)
                {
                    struct FileRequester *r = AllocAslRequestTags(
                        ASL_FileRequest, ASLFR_TitleText, "Import source drawer", ASLFR_DrawersOnly,
                        TRUE, TAG_DONE);
                    if (r && AslRequestTags(r, TAG_DONE))
                    {
                        char dest[PATH_SIZE], np[PATH_SIZE];
                        int i;
                        for (i = 1; i < 10000; i++)
                        {
                            snprintf(dest, sizeof(dest), "%s/import-%04d", project_root, i);
                            if (!exists(dest))
                                break;
                        }
                        if (i < 10000 && mkdir_safe(dest) && clone_sources(r->fr_Drawer, dest, 0))
                        {
                            join(np, dest, "name.txt");
                            save_text(np, "Imported project", 120);
                            load_projects();
                            strcpy(project, dest);
                            load_tasks();
                            message("Source drawer imported into an independent project.");
                        }
                        else
                            message("Import failed. Check the selected drawer and disk space.");
                    }
                    if (r)
                        FreeAslRequest(r);
                }
                break;
            case NEWPROJECT:
                create_workspace(0);
                break;
            case NEWTASK:
                create_workspace(1);
                break;
            case SAVE:
                if (save_current())
                    message("Saved. Previous contents are retained in .bak files.");
                break;
            case SAVETASK:
                if (save_editor(prompt, "task.txt", 8192))
                    message("Task instructions saved.");
                break;
            case NEWFILE:
                name = (char *)value(newname, MUIA_String_Contents);
                if (!source_name(name) || strchr(name, '/'))
                {
                    message("Use a simple .c, .h, .txt or .md filename.");
                    break;
                }
                join(path, current, name);
                if (busy(current) || exists(path))
                {
                    message("Workspace busy or file already exists.");
                    break;
                }
                if (save_text(path, "", SOURCE_LIMIT))
                {
                    refresh_files();
                    open_file(name);
                    set(pages, MUIA_Group_ActivePage, 1);
                    set(win, MUIA_Window_ActiveObject, source);
                    set(newname, MUIA_String_Contents, "");
                }
                break;
            case BUILD:
                launch("--build", 2);
                break;
            case RUN:
                launch("--run", 2);
                break;
            case AGENT:
                if (save_editor(prompt, "task.txt", 8192))
                    launch("--agent", 2);
                break;
            case CANCEL:
                cancel();
                break;
            case FIND:
            case FINDNEXT:
                if (!DoMethod(source, MUIM_TextEditor_Search,
                              (STRPTR)value(findtext, MUIA_String_Contents),
                              id == FIND ? MUIF_TextEditor_Search_FromTop
                                         : MUIF_TextEditor_Search_Next))
                    message("Text not found.");
                break;
            case NEWSESSION:
                join(path, current, "session.txt");
                if (!busy(current))
                {
                    DeleteFile((STRPTR)path);
                    message("Next agent request starts a new conversation. Transcript retained.");
                }
                break;
            case SAVEBUILD:
                join(path, current, "build-command.txt");
                if (!busy(current) &&
                    save_text(path, (char *)value(buildcommand, MUIA_String_Contents), 512))
                    message("Build command saved for this workspace.");
                break;
            case REFRESH:
                refresh_files();
                break;
            case SETTINGS:
                set(pages, MUIA_Group_ActivePage, 4);
                break;
            case SAVECONFIG:
                configure();
                break;
            case PROBE:
                launch("--probe", 2);
                break;
            case SHELL:
                name = (char *)value(command, MUIA_String_Contents);
                join(path, current, "command.txt");
                if (!busy(current) && save_text(path, name, 1024))
                    launch("--shell", 2);
                break;
            case GITSTATUS:
                launch("--status", 3);
                break;
            case DIFF:
                launch("--diff", 3);
                break;
            case GITINIT:
                launch("--init", 3);
                break;
            case GITCOMMIT:
                name = (char *)value(commit, MUIA_String_Contents);
                join(path, current, "commit.txt");
                if (*name && !busy(current) && save_text(path, name, 256))
                    launch("--commit", 3);
                else
                    message("Enter a commit message and wait for running jobs.");
                break;
            case UNDO:
            case REDO:
            case CUT:
            case COPY:
            case PASTE:
            {
                const char *cmd = id == UNDO   ? "UNDO"
                                  : id == REDO ? "REDO"
                                  : id == CUT  ? "CUT"
                                  : id == COPY ? "COPY"
                                               : "PASTE";
                DoMethod(source, MUIM_TextEditor_ARexxCmd, cmd);
                break;
            }
            case RENAMEPROJECT:
            case RENAMETASK:
                name = (char *)value(id == RENAMEPROJECT ? projectname : taskname,
                                     MUIA_String_Contents);
                join(path, id == RENAMEPROJECT ? project : current, "name.txt");
                if (!busy(current) && *name && save_text(path, name, 120))
                {
                    task_row(current);
                    if (id == RENAMEPROJECT)
                    {
                        int i = value(projects, MUIA_List_Active);
                        DoMethod(projects, MUIM_KillNotify, MUIA_List_Active);
                        DoMethod(projects, MUIM_List_Remove, i);
                        DoMethod(projects, MUIM_List_InsertSingle, name, i);
                        set(projects, MUIA_List_Active, i);
                        notify(projects, MUIA_List_Active, MUIV_EveryTime, PROJECT);
                    }
                    message("Name saved.");
                }
                break;
            case MARKDONE:
                join(path, current, "state.txt");
                if (!busy(current) && save_text(path, "Done", 32))
                {
                    task_row(current);
                    message("Task marked done. Files and history are retained.");
                }
                break;
            }
            if (done)
                break;
        }
        if (done)
            break;
        if (signals)
        {
            received = Wait(signals | SIGBREAKF_CTRL_C | (1UL << timer_port->mp_SigBit));
            if (received & SIGBREAKF_CTRL_C)
            {
                if (save_current())
                    done = 1;
            }
            if (timer_pending && CheckIO((struct IORequest *)timer_io))
            {
                WaitIO((struct IORequest *)timer_io);
                timer_pending = 0;
                poll_jobs();
            }
        }
    }
    result = 0;
cleanup:
    if (timer_pending)
    {
        AbortIO((struct IORequest *)timer_io);
        WaitIO((struct IORequest *)timer_io);
    }
    if (timer_open)
        CloseDevice((struct IORequest *)timer_io);
    if (timer_io)
        DeleteIORequest((struct IORequest *)timer_io);
    if (timer_port)
        DeleteMsgPort(timer_port);
    if (app)
        MUI_DisposeObject(app);
    if (codefont)
        CloseFont(codefont);
    if (bodyfont)
        CloseFont(bodyfont);
    if (AslBase)
        CloseLibrary(AslBase);
    if (DiskfontBase)
        CloseLibrary(DiskfontBase);
    if (GfxBase)
        CloseLibrary((struct Library *)GfxBase);
    if (MUIMasterBase)
        CloseLibrary(MUIMasterBase);
    if (IntuitionBase)
        CloseLibrary((struct Library *)IntuitionBase);
    return result;
}
