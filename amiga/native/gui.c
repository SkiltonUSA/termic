/* Native Workbench front end. Workers run in separate DOS processes. */
#include <exec/types.h>
#include <dos/dostags.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/icon.h>
#include <stdio.h>
#include <string.h>

struct IntuitionBase *IntuitionBase;
struct Library *GadToolsBase, *IconBase;
static struct TextAttr font = { (STRPTR)"topaz.font", 8, 0, 0 };
const char stack_size[] = "$STACK:32768";

enum { EDIT = 1, TASK, BUILD, RUN, AGENT, LOG, MODEL, KEY, PROBE, QUIT };
static const char *labels[] = {
    "Source (S)", "Task (T)", "Build (B)", "Run (R)", "Agent (A)",
    "Log (L)", "Model (M)", "API key (K)", "HTTPS test (H)", "Quit (Q)"
};
static const char *commands[] = {
    "C:Ed Termic:Project/main.c", "C:Ed Termic:Project/task.txt",
    "Execute Termic:StartBuild", "Execute Termic:StartRun",
    "Execute Termic:StartAgent", "C:Type Termic:Project/transcript.txt",
    "C:Ed Termic:Config/model", "C:Ed Termic:Config/api-key",
    "Execute Termic:TestHTTPS"
};

static int make_icon(const char *path)
{
    struct DiskObject *icon;
    int result = 20;
    IconBase = OpenLibrary((STRPTR)"icon.library", 37);
    if (!IconBase) return 20;
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

static const char *launch(int action)
{
    BPTR console, busy;
    LONG result;
    if (action < EDIT || action > PROBE) return "Ready.";
    busy = Lock((STRPTR)"Termic:Project/.busy", ACCESS_READ);
    if (busy) {
        UnLock(busy);
        if (action <= AGENT) return "Project busy. Finish or cancel the worker first.";
    }
    console = Open((STRPTR)"CON:30/40/620/240/Termic Native/AUTO/CLOSE/WAIT", MODE_NEWFILE);
    if (!console) return "Cannot open console.";
    result = SystemTags((STRPTR)commands[action - 1], SYS_Input, console,
                        SYS_Output, 0, SYS_Asynch, TRUE, TAG_DONE);
    if (result == -1) { Close(console); return "Cannot start native command."; }
    if (action == AGENT) return "Agent started. See its console; Ctrl-C cancels network waits.";
    if (action == BUILD) return "Compiling locally. See the build console for results.";
    return "Opened command console. Close it when finished.";
}

int main(int argc, char **argv)
{
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    struct Gadget *gadgets = NULL, *previous, *status = NULL;
    struct NewGadget ng;
    APTR visual = NULL;
    BOOL running = TRUE;
    int result = 20, i;
    WORD top, left;
    BPTR root;
    if (argc == 3 && !strcmp(argv[1], "--icon")) return make_icon(argv[2]);
    root = DupLock(GetProgramDir());
    if (!root) return 20;
    if (!AssignLock((STRPTR)"Termic", root)) { UnLock(root); return 20; }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 37);
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
    ng.ng_TopEdge = top + 10;
    ng.ng_Width = 592;
    ng.ng_Height = 16;
    ng.ng_TextAttr = &font;
    ng.ng_VisualInfo = visual;
    previous = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, (ULONG)"TERMIC  |  Native Amiga development", TAG_DONE);
    if (!previous) goto cleanup;
    ng.ng_TopEdge += 22;
    previous = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, (ULONG)"Edit C, build with VBCC, run on Workbench. Cloud agent via AmiSSL.", TAG_DONE);
    if (!previous) goto cleanup;
    ng.ng_TopEdge += 22;
    previous = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, (ULONG)"Project: Termic:Project/main.c   |   Task: Termic:Project/task.txt", TAG_DONE);
    if (!previous) goto cleanup;
    for (i = 0; i < 10; ++i) {
        ng.ng_TopEdge = top + 90 + (i / 5) * 38;
        ng.ng_LeftEdge = left + 12 + (i % 5) * 120;
        ng.ng_Width = 112;
        ng.ng_Height = 26;
        ng.ng_GadgetText = (STRPTR)labels[i];
        ng.ng_GadgetID = i + 1;
        previous = CreateGadget(BUTTON_KIND, previous, &ng, TAG_DONE);
        if (!previous) goto cleanup;
    }
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID = 0;
    ng.ng_LeftEdge = left + 12;
    ng.ng_TopEdge = top + 174;
    ng.ng_Width = 592;
    ng.ng_Height = 20;
    previous = status = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, (ULONG)"Ready. Set API key and model before starting the cloud agent.",
        GTTX_Border, TRUE, TAG_DONE);
    if (!previous) goto cleanup;
    ng.ng_TopEdge += 30;
    previous = CreateGadget(TEXT_KIND, previous, &ng,
        GTTX_Text, (ULONG)"Agent sends task, source and compiler output to OpenAI when started.", TAG_DONE);
    if (!previous) goto cleanup;
    window = OpenWindowTags(NULL, WA_Title, (ULONG)"Termic Native Amiga",
        WA_PubScreen, (ULONG)screen, WA_Left, 40, WA_Top, 50,
        WA_InnerWidth, 616, WA_InnerHeight, 236,
        WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_Gadgets, (ULONG)gadgets,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY,
        TAG_DONE);
    if (!window) goto cleanup;
    GT_RefreshWindow(window, NULL);
    UnlockPubScreen(NULL, screen); screen = NULL;
    while (running) {
        struct IntuiMessage *message;
        ULONG signals = Wait((1UL << window->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) running = FALSE;
        while ((message = GT_GetIMsg(window->UserPort)) != NULL) {
            ULONG kind = message->Class;
            UWORD code = message->Code;
            int action = 0;
            if (kind == IDCMP_GADGETUP) action = ((struct Gadget *)message->IAddress)->GadgetID;
            GT_ReplyIMsg(message);
            if (!running) continue;
            if (kind == IDCMP_CLOSEWINDOW) running = FALSE;
            else if (kind == IDCMP_REFRESHWINDOW) {
                GT_BeginRefresh(window); GT_EndRefresh(window, TRUE);
            } else {
                if (kind == IDCMP_VANILLAKEY) {
                    const char *keys = "stbralmkhq";
                    const char *key;
                    if (code >= 'A' && code <= 'Z') code += 'a' - 'A';
                    key = code ? strchr(keys, code) : NULL;
                    if (key) action = (int)(key - keys) + 1;
                    if (code == 27) action = QUIT;
                }
                if (action == QUIT) running = FALSE;
                else if (action) GT_SetGadgetAttrs(status, window, NULL,
                    GTTX_Text, (ULONG)launch(action), TAG_DONE);
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
