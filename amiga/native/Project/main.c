/* Native AmigaOS 2+ GadTools example; no bridge or GUI toolkit dependency. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/icon.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase;
struct Library *IconBase;
static struct TextAttr font = { (STRPTR)"topaz.font", 8, 0, 0 };
static char counter_text[64];

enum { INCREMENT = 1, RESET, QUIT };

static int make_icon(const char *path)
{
    struct DiskObject *icon;
    int result = 20;
    IconBase = OpenLibrary("icon.library", 37);
    if (!IconBase) return result;
    icon = GetDefDiskObject(WBTOOL);
    if (icon) {
        icon->do_StackSize = 32768;
        icon->do_CurrentX = NO_ICON_POSITION;
        icon->do_CurrentY = NO_ICON_POSITION;
        if (PutDiskObject((STRPTR)path, icon)) result = 0;
        FreeDiskObject(icon);
    }
    CloseLibrary(IconBase);
    return result;
}

static void update_counter(struct Gadget *counter, struct Window *window, ULONG count)
{
    sprintf(counter_text, "Button count: %lu", (unsigned long)count);
    GT_SetGadgetAttrs(counter, window, NULL, GTTX_Text, counter_text, TAG_DONE);
}

int main(int argc, char **argv)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    struct Gadget *gadgets = NULL, *previous, *counter = NULL;
    struct NewGadget ng;
    APTR visual = NULL;
    ULONG count = 0;
    BOOL running = TRUE;
    int result = 20, i;
    WORD top, left;
    static const char *labels[] = { "Increment (I)", "Reset (R)", "Quit (Q)" };

    if (argc == 3 && !strcmp(argv[1], "--icon")) return make_icon(argv[2]);
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
    GadToolsBase = OpenLibrary("gadtools.library", 37);
    if (!IntuitionBase || !GadToolsBase) goto cleanup;
    screen = LockPubScreen(NULL);
    if (!screen) goto cleanup;
    visual = GetVisualInfo(screen, TAG_DONE);
    if (!visual) goto cleanup;
    top = screen->WBorTop + screen->Font->ta_YSize + 1;
    left = screen->WBorLeft;
    previous = CreateContext(&gadgets);
    if (!previous) goto cleanup;
    memset(&ng, 0, sizeof(ng));
    ng.ng_LeftEdge = left + 12;
    ng.ng_TopEdge = top + 12;
    ng.ng_Width = 408;
    ng.ng_Height = 16;
    ng.ng_TextAttr = &font;
    ng.ng_VisualInfo = visual;
    previous = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, "Built locally. Running on your Amiga.", TAG_DONE);
    if (!previous) goto cleanup;
    ng.ng_TopEdge = top + 40;
    ng.ng_Height = 20;
    previous = counter = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, "Button count: 0", GTTX_Border, TRUE, TAG_DONE);
    if (!previous) goto cleanup;
    ng.ng_TopEdge = top + 76;
    ng.ng_Width = 128;
    ng.ng_Height = 22;
    for (i = 0; i < 3; ++i) {
        ng.ng_LeftEdge = left + 12 + i * 140;
        ng.ng_GadgetText = (STRPTR)labels[i];
        ng.ng_GadgetID = INCREMENT + i;
        previous = CreateGadget(BUTTON_KIND, previous, &ng, TAG_DONE);
        if (!previous) goto cleanup;
    }
    window = OpenWindowTags(NULL,
        WA_Title, "Termic Project", WA_PubScreen, screen,
        WA_Left, 100, WA_Top, 80, WA_InnerWidth, 432, WA_InnerHeight, 112,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        WA_Gadgets, gadgets,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP |
                  IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY, TAG_DONE);
    if (!window) goto cleanup;
    GT_RefreshWindow(window, NULL);
    UnlockPubScreen(NULL, screen);
    screen = NULL;
    while (running) {
        struct IntuiMessage *message;
        ULONG signals = Wait((1UL << window->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) running = FALSE;
        while ((message = GT_GetIMsg(window->UserPort)) != NULL) {
            ULONG kind = message->Class;
            UWORD code = message->Code;
            UWORD action = 0;
            if (kind == IDCMP_GADGETUP)
                action = ((struct Gadget *)message->IAddress)->GadgetID;
            GT_ReplyIMsg(message);
            if (!running) continue;
            if (kind == IDCMP_CLOSEWINDOW) running = FALSE;
            else if (kind == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(window);
                GT_EndRefresh(window, TRUE);
            } else {
                if (kind == IDCMP_VANILLAKEY) {
                    if (code == 'i' || code == 'I') action = INCREMENT;
                    if (code == 'r' || code == 'R') action = RESET;
                    if (code == 'q' || code == 'Q' || code == 27) action = QUIT;
                }
                if (action == INCREMENT && count < 999999) ++count;
                if (action == RESET) count = 0;
                if (action == QUIT) running = FALSE;
                if (action == INCREMENT || action == RESET)
                    update_counter(counter, window, count);
            }
        }
    }
    result = 0;
cleanup:
    if (window) CloseWindow(window);
    if (gadgets) FreeGadgets(gadgets);
    if (visual) FreeVisualInfo(visual);
    if (screen) UnlockPubScreen(NULL, screen);
    if (GadToolsBase) CloseLibrary(GadToolsBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return result;
}
