/* VBCC bindings for the public MUI 3.8 ABI (muimaster_lib.fd).
 * SDK headers remain in the separately distributed MUI developer archive. */
#ifndef TERMIC_MUI_COMPAT_H
#define TERMIC_MUI_COMPAT_H
#include <clib/alib_protos.h>
#include <libraries/mui.h>
extern struct Library *MUIMasterBase;
Object *__termic_mui_new(__reg("a6") struct Library *, __reg("a0") char *,
                         __reg("a1") struct TagItem *) = "\tjsr\t-30(a6)";
void __termic_mui_dispose(__reg("a6") struct Library *, __reg("a0") Object *) = "\tjsr\t-36(a6)";
LONG __termic_mui_request(__reg("a6") struct Library *, __reg("d0") APTR, __reg("d1") APTR,
                          __reg("d2") LONG, __reg("a0") char *, __reg("a1") char *,
                          __reg("a2") char *, __reg("a3") APTR) = "\tjsr\t-42(a6)";
static Object *MUI_NewObject(char *name, ULONG first, ...)
{
    return __termic_mui_new(MUIMasterBase, name, (struct TagItem *)&first);
}
#define MUI_DisposeObject(obj) __termic_mui_dispose(MUIMasterBase, obj)
#endif
