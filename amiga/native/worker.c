#include "native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>

const char stack_size[] = "$STACK:262144";

int cancel_requested(void)
{
    return (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) != 0;
}

int build_project(void)
{
    LONG result = SystemTags((STRPTR)"Execute Termic:BuildProject", TAG_DONE);
    printf("Local compiler exit code: %ld\n", (long)result);
    return result == 0;
}

int main(int argc, char **argv)
{
    BPTR lock;
    int result = 20;
    if (argc == 2 && !strcmp(argv[1], "--probe")) {
        char *reply = NULL;
        int status = cloud_request(NULL, NULL, &reply);
        free(reply);
        printf("Verified HTTPS probe: HTTP %d (401 expected without a key).\n", status);
        return status == 401 ? 0 : 20;
    }
    lock = CreateDir((STRPTR)TERMIC_PROJECT ".busy");
    if (!lock) { puts("Project busy, or cannot create its lock directory."); return 20; }
    UnLock(lock);
    if (argc == 2 && !strcmp(argv[1], "--build")) result = build_project() ? 0 : 20;
    else if (argc == 2 && !strcmp(argv[1], "--run")) {
        BPTR program = Lock((STRPTR)TERMIC_PROJECT "Program", ACCESS_READ);
        if (!program) puts("Build the project first.");
        else {
            UnLock(program);
            result = (int)SystemTags((STRPTR)TERMIC_PROJECT "Program", TAG_DONE);
        }
    } else result = run_agent();
    DeleteFile((STRPTR)TERMIC_PROJECT ".busy");
    return result;
}
