/* Mount our optional project image without replacing another application image.
 * --create formats only the new file created by this invocation. */
#include "native.h"
#include "version.h"
#include "workspace.h"
#include <ctype.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/filehandler.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
const char version_tag[] = TERMIC_VERSION_TAG("TermicStorage");
const char stack_size[] = "$STACK:32768";
static int equal(const char *a, const char *b)
{
    while (*a && *b)
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return 0;
    return !*a && !*b;
}
static int ownership(int *mounted)
{
    struct DosList *list = LockDosList(LDF_DEVICES | LDF_READ), *entry = list;
    int ok = 1;
    *mounted = 0;
    if (!list)
        return 0;
    while ((entry = NextDosEntry(entry, LDF_DEVICES)))
    {
        struct FileSysStartupMsg *s;
        struct DosEnvec *env;
        unsigned char *b;
        char device[256], name[256];
        char *part;
        int ours;
        b = (unsigned char *)BADDR(entry->dol_Name);
        memcpy(name, b + 1, b[0]);
        name[b[0]] = 0;
        ours = equal(name, "TMGIT");
        if ((ULONG)entry->dol_misc.dol_handler.dol_Startup < 4096)
        {
            if (ours)
                ok = 0;
            continue;
        }
        s = (struct FileSysStartupMsg *)BADDR(entry->dol_misc.dol_handler.dol_Startup);
        b = (unsigned char *)BADDR(s->fssm_Device);
        if (!b)
        {
            if (ours)
                ok = 0;
            continue;
        }
        memcpy(device, b + 1, b[0]);
        device[b[0]] = 0;
        part = strrchr(device, ':');
        part = part ? part + 1 : device;
        if (equal(part, "diskimage.device") && s->fssm_Unit == 7)
        {
            if (!ours)
                ok = 0;
            else
            {
                env = (struct DosEnvec *)BADDR(s->fssm_Environ);
                if (!env || env->de_TableSize < DE_DOSTYPE || env->de_DosType != 0x444f5307)
                    ok = 0;
                else
                    *mounted = 1;
            }
        }
        else if (ours)
            ok = 0;
    }
    UnLockDosList(LDF_DEVICES | LDF_READ);
    return ok;
}
static int create_image(void)
{
    BPTR lock;
    FILE *f;
    char *zero;
    int i, ok = 1, mounted;
    if (!ownership(&mounted) || mounted)
    {
        puts("Create requires an unmounted, unclaimed project device.");
        return 0;
    }
    lock = Lock((STRPTR) "Termic:Projects.hdf", ACCESS_READ);
    if (lock)
    {
        UnLock(lock);
        puts("Project image already exists; it was not changed.");
        return 0;
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND)
        return 0;
    lock = Lock((STRPTR) "Termic:Projects.hdf.new", ACCESS_READ);
    if (lock)
    {
        UnLock(lock);
        puts("A previous image creation needs recovery.");
        return 0;
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND)
        return 0;
    zero = calloc(1, 65536);
    if (!zero)
        return 0;
    f = fopen("Termic:Projects.hdf.new", "wb");
    if (!f)
    {
        free(zero);
        return 0;
    }
    for (i = 0; i < 1024 && ok; i++)
    {
        if (fwrite(zero, 1, 65536, f) != 65536)
            ok = 0;
        if ((i % 128) == 0)
        {
            printf("Creating project image: %d MiB of 64\n", i / 16);
            fflush(stdout);
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            ok = 0;
    }
    if (fclose(f))
        ok = 0;
    free(zero);
    if (!ok)
    {
        remove("Termic:Projects.hdf.new");
        return 0;
    }
    if (!Rename((STRPTR) "Termic:Projects.hdf.new", (STRPTR) "Termic:Projects.hdf"))
        return 0;
    return 1;
}
int main(int argc, char **argv)
{
    int mounted, state = -1, result = 20, creating = argc == 2 && !strcmp(argv[1], "--create");
    BPTR lock;
    char canonical[384], temp[80], command[512], *text, *line, *end;
    FILE *f;
    if (argc == 2 && !strcmp(argv[1], "--version"))
    {
        puts(version_tag + 7);
        return 0;
    }
    if (creating && !create_image())
        return 20;
    lock = Lock((STRPTR) "Termic:Projects.hdf", ACCESS_READ);
    if (!lock)
    {
        puts("Project image is missing. Run TermicStorage --create to create a new file-backed "
             "volume.");
        return 20;
    }
    if (!NameFromLock(lock, (STRPTR)canonical, sizeof(canonical)))
    {
        UnLock(lock);
        return 20;
    }
    UnLock(lock);
    if (!ownership(&mounted))
    {
        puts("Disk-image unit 7 belongs to another DOS device. Project volume was not changed.");
        return 20;
    }
    sprintf(temp, "T:tmimg-%08lx", (unsigned long)FindTask(NULL));
    sprintf(command, "Termic:Tools/DiskImageCtrl >%s", temp);
    if (SystemTags((STRPTR)command, TAG_DONE))
    {
        DeleteFile((STRPTR)temp);
        return 20;
    }
    text = read_text(temp, 8192);
    DeleteFile((STRPTR)temp);
    if (!text)
        return 20;
    line = strstr(text, "unit 7:");
    if (line)
    {
        line += 7;
        while (*line == ' ' || *line == '\t')
            line++;
        end = strpbrk(line, "\r\n");
        if (end)
            *end = 0;
        if (strstr(line, "<no image>"))
            state = 0;
        else
        {
            char *quote = strchr(line, '\"');
            if (quote)
            {
                line = quote + 1;
                end = strchr(line, '\"');
                if (end)
                    *end = 0;
                if (equal(line, canonical))
                    state = 1;
            }
        }
    }
    free(text);
    if (state < 0)
    {
        puts("Disk-image unit 7 already contains another image. It was left untouched.");
        return 20;
    }
    if (argc == 2 && !strcmp(argv[1], "--eject"))
    {
        if (!state)
            return 0;
        if (mounted && !Inhibit((STRPTR) "TMGIT:", DOSTRUE))
            return 20;
        result = SystemTags((STRPTR) "Termic:Tools/DiskImageCtrl UNIT 7 EJECT", TAG_DONE);
        if (mounted && !Inhibit((STRPTR) "TMGIT:", DOSFALSE))
            return 20;
        return result;
    }
    if (argc > 1 && !creating)
        return 20;
    if (!mounted)
    {
        f = fopen("Termic:Tools/TMGIT", "w");
        if (!f)
            return 20;
        fputs("FileSystem = L:FastFileSystem\nDevice = diskimage.device\nUnit = 7\nFlags = "
              "0\nSurfaces = 1\nBlocksPerTrack = 32\nBlockSize = 512\nReserved = 2\nInterleave = "
              "0\nLowCyl = 0\nHighCyl = 4095\nBuffers = 128\nBufMemType = 1\nMaxTransfer = "
              "0x1fe00\nMask = 0x7ffffffe\nStackSize = 16384\nPriority = 5\nGlobVec = -1\nDosType "
              "= 0x444f5307\nActivate = 0\n",
              f);
        if (fclose(f))
            return 20;
        if (SystemTags((STRPTR) "Mount Termic:Tools/TMGIT", TAG_DONE))
            return 20;
    }
    if (!state &&
        SystemTags((STRPTR) "Termic:Tools/DiskImageCtrl UNIT 7 INSERT Termic:Projects.hdf",
                   TAG_DONE))
        return 20;
    if (creating)
    {
        if (state != 0 || !Inhibit((STRPTR) "TMGIT:", DOSTRUE))
            return 20;
        result = Format((STRPTR) "TMGIT:", (STRPTR) "TermicProjects", 0x444f5307) ? 0 : 20;
        if (!Inhibit((STRPTR) "TMGIT:", DOSFALSE))
            return 20;
        if (result)
            return result;
        lock = CreateDir((STRPTR) "TMGIT:Projects");
        if (lock)
            UnLock(lock);
        else
            return 20;
        if (!save_text("Termic:Config/projects-drawer", "TMGIT:Projects", 180))
            return 20;
    }
    lock = Lock((STRPTR) "TMGIT:", ACCESS_READ);
    if (lock)
    {
        UnLock(lock);
        result = 0;
        puts("Termic project volume mounted.");
    }
    return result;
}
