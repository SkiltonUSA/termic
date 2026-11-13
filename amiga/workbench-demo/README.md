# Native Workbench demo

A small AmigaOS application with Increment, Reset and Quit buttons. The same
commands are available as I, R and Q; Escape also quits. The counter saturates
at 999999. It uses the default public screen, GadTools and the system's Topaz
font, without changing screen modes or accessing ZZ9000 hardware directly.

## Build on the Mac

From the Termic repository root, after linking the existing DevBench workspace:

```sh
python3 scripts/amiga/workspace.py demo-build
```

Or choose **Build Workbench demo** in Termic's Run menu after installing the
updated `scripts/amiga/termic.yaml` configuration. The source in this checkout
is compiled using the linked workspace's pinned compiler image and container
connection. Output is `.context/amiga-demo/TermicDemo`. No sources in the
linked workspace are changed. Each Termic worktree gets its own output.

The compiler targets 68020-compatible instructions with software floating
point and static libnix. Build failures propagate to Termic, and successful
output must have the Amiga Hunk header. The container clock may be behind the
Mac and produce make clock-skew warnings; check the compilation result.

## Run on the Amiga

Transfer `TermicDemo` using the existing DevBench bridge. Verify the transfer
and use a development drawer, not a system directory. In an Amiga Shell:

```text
Protect RAM:TermicDemo +e
RAM:TermicDemo --icon RAM:TermicDemo
Stack 32768
RAM:TermicDemo
```

The `--icon` command writes a standard Tool icon with a 32768-byte stack.
After quitting, the icon can launch the program from Workbench. Use Show All
Files if needed to find it. Keep the executable and its `.info` file together.
For a persistent install, substitute a development drawer for `RAM:` in these
commands. RAM-disk files disappear on reboot. Icon creation replaces that
program's `.info` file, so don't rerun it over customized tooltypes.

The program opens no network connection and does not require DevBench at
runtime. Window close, Quit, Q, Escape and a shell Ctrl-C signal all use the
same resource cleanup path. Allocation failures return AmigaDOS error 20.

## Verification

On the physical A4000TX, the 5392-byte binary passed bridge CRC verification
and a byte-for-byte transfer readback. The initial window displayed count 0;
two I key presses displayed 2; R restored 0; Q removed the window. A generated
Tool icon was retrieved and Workbench launch via `WBLoad` reopened the window.
The screenshots are local under `.context/amiga-demo/` and are not committed.
Mouse clicks, the close gadget and visual refresh after window obstruction
still require manual checks. No emulator/AGA-only display test was performed.
Termic's graphical Run controls remain unverified locally.

The implementation follows the documented GadTools message and refresh
lifecycle: [AmigaOS GadTools reference](https://wiki.amigaos.net/wiki/GadTools_Gadgets).
