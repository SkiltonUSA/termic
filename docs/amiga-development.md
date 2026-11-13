# Amiga development in Termic

This checkout provides two Amiga workflows: the existing Mac Termic/DevBench
integration described below, and a separate native Workbench client in
[`amiga/native`](../amiga/native/ReadMe). The native client edits and builds
C projects on the Amiga and calls OpenAI directly through AmiSSL. It does
not require the Mac or DevBench at runtime. It is a native MUI implementation; the desktop React/Tauri runtime is not
part of the Amiga application.

The supplied environment specifies TF4060/68060, Workbench 3.2.3, 2 MiB
Chip and 624 MiB Fast RAM, ZZ9000 XX19c, and X-Surf 100 with Roadshow.
The linked workspace's `amiga/target.json`, `docs/development-amiga.md`
and `docs/amiga-session-memory.md` remain authoritative. Read them before
hardware work. ZZ9000 ARM programs are a separate target from AmigaOS/68k.

## Native MUI workspace

`amiga/native` implements a three-pane MUI application inspired by
`docs/hero.png`: projects/tasks on the left, a tabbed workspace in the
centre and files on the right. TextEditor.mcc provides embedded editing,
clipboard operations, undo/redo and search. It replaces the earlier GadTools
launcher and external Ed workflow. The [native ReadMe](../amiga/native/ReadMe)
contains installation steps, controls, shortcuts and limits.

| Desktop concept | Native implementation |
| --- | --- |
| Projects and tasks | Named managed projects, source-drawer import, independent source-copy tasks, persistent status and last-workspace restore |
| Editor and files | C/header/text/Markdown browsing, new files, embedded editor, backup saves and search |
| Agent workspace | Editable instructions, transcript, model settings, bounded C/header file tools and local builds |
| Conversation resume | Completed Responses IDs saved per task; New conversation retains the transcript |
| Background work | Separate DOS workers, up to 16 distinct tasks, one worker per task, read-only editing while busy |
| Build and run | Native VBCC, configurable per-task build commands, integrated output and explicit program launch |
| Terminal | Explicit AmigaDOS commands with captured output; no interactive PTY or xterm emulation |
| Change review | Native Git init/status/diff and stage-existing-source/commit; other Git commands through the shell |
| Worktrees | Independent source snapshots; amiga-git 0.9.0 does not implement worktree |
| Desktop agent CLIs | OpenAI HTTPS function calling; desktop Claude/Codex/Gemini executables are not available on 68k |
| Queue, broadcast/races and remote PR UI | Not implemented in the native client |

### Installation and builds

The target is AmigaOS 3.2 with MUI 3.8+, TextEditor.mcc 15.56, Fast RAM,
VBCC/NDK, Roadshow-compatible networking and AmiSSL. Native Git was verified
with AmiSSL 5.27. Use its upstream OS3 installer and correct CPU library;
retain existing older-version libraries for applications using them.
The [5.27 release](https://github.com/jens-maus/amissl/releases/tag/5.27)
describes compatibility and publishes archive checksums.

`InstallSDK` extracts the pinned original compiler/NDK/AmiSSL developer
archives. `InstallUI` extracts MUI headers, installs TextEditor if absent,
and installs native Git under `Termic:Tools`. It does not replace the MUI
core or edit startup files. Execute both before `BuildNative`, which rebuilds
TermicAgent, TermicNative and TermicStorage on the Amiga. `Setup` adds the SDK and Termic MUI
bindings to the current build environment. The worker and Workbench icon
use a 256 KiB stack; asynchronous jobs explicitly set their Shell stack.

`BuildProject` operates in the worker's selected current directory. Its
default command compiles `main.c`; `build-command.txt` can supply a different
command which produces `Program.new`. Only successful builds promote it to
`Program`; a failed build keeps the previous executable. A `.busy` drawer prevents overlapping workers. A named Exec port
allows cancellation, and `.result` signals completion to the UI. Polling is
active only while jobs run. Imports and file enumeration remain synchronous;
large source trees can delay the UI.

Create a local kit from built Hunk binaries and verified original archives:

```sh
python3 scripts/amiga/package_native.py \
  --downloads .context/native-downloads \
  --binaries .context/amiga-native \
  --output .context/amiga-native/TermicMUI.lha \
  --lha /path/to/host/lha
```

`--download` fetches missing archives. SHA-256 mismatches stop packaging.
The kit uses clean sample files and an empty key, never live configuration,
projects or logs. MUI runtime is separately installed; the original AmiSSL runtime archive is
included for use with its upstream installer.
Original archives retain their licences and documentation.

### Optional project volume

Classic FFS limits filenames to 30 characters. Native Git needs longer
object filenames. `TermicStorage --create` can create a fresh 64 MiB
`Projects.hdf` file using FFS long filenames and select `TMGIT:Projects`.
It initializes only a newly created file, refuses an existing image, and
checks DOS-device ownership of diskimage.device unit 7. The mount helper
also inspects the inserted image and refuses to replace another image.
It never formats an existing partition or edits startup files.

The GUI reads `Config/projects-drawer`; the default is `Termic:Projects`.
When configured for `TMGIT:Projects`, it runs the mount helper at startup.
Settings can select another managed-project drawer for the next launch.
The original `Termic:Project` remains available for the starter example.

Close active workers and use a Shell whose current directory is outside
the image before `TermicStorage --eject`. Copy the closed `Projects.hdf` for
a consistent backup, then run `TermicStorage` to mount it again. The original
DiskImage 37.7 archive is pinned and retains its redistribution notice.

### Agent and hardware boundaries

The cloud worker implements the
[Responses function-calling protocol](https://developers.openai.com/api/docs/guides/function-calling).
It can list, read and write C/header files and compile locally. Paths reject
device names and traversal; native resolution also checks that existing
files or new-file parents remain within the workspace. Backups precede
replacement. The model has no shell-command tool. Compilers and generated
programs still have normal AmigaOS access; this is not an OS sandbox.

Settings stores the key locally in `Config/api-key`; the secret field never
loads it. It is excluded from prompts, command arguments, logs and packages.
Task text, requested source and compiler results are sent to OpenAI. The
API stores responses for conversation continuity. Each run is capped at
eight requests. Completed response IDs persist separately in each task.

TLS verifies certificates and hostnames, supplies SNI, and requires TLS
1.2+. No plaintext fallback exists. An accurate clock and installed CA
certificates are required. The HTTPS probe expects HTTP 401 without a key;
it proves connectivity, not model access. Network waits have a 120-second
deadline after the stack's DNS lookup. Source files are limited to 32 KiB,
instructions to 8 KiB, tool compiler output to 16 KiB and responses to 256 KiB.

The TLS unit uses SDK-derived VBCC bindings to avoid compiling unused
OpenSSL wrappers. Verify them with
`scripts/amiga/generate_amissl_subset.py SDK/Developer/include --check`.
The MUI bindings use the public MUI 3.8 `muimaster_lib.fd` ABI. Runtime library
calls remain native; neither binding layer replaces a system library.

ZZ9000 supplies the existing RTG screen. Compiler, TLS and file work run on
68k. ARM offload remains unavailable until a measured workload, exclusive
core ownership and verified memory/cache contract are established. Existing
ARM applications and display/firmware settings are preserved.

### Verification

`python3 scripts/amiga/test_native.py` exercises HTTP framing, synthetic
agent read/edit/build loops, multi-file tools, invalid paths, conversation
resume, errors, cancellation, step limits and backup/failure behavior under
ASan/UBSan. These tests never make network requests.

`scripts/amiga/accept_mui.py` is the opt-in physical-hardware suite. It uses
an already-running DevBench MCP, creates an isolated source fixture,
drives real MUI controls, and saves window-only screenshots and evidence
under `.context/amiga-mui`. It does not read credentials or submit a model
request. Use the DevBench Python environment while the keyboard is idle.
`accept_native.py` forwards to this maintained suite. A capture interruption
after the fixture commit can resume with `--resume-task <task-drawer>`;
the suite verifies the selected task and commit before continuing.

On 2026-11-12, the A4000TX rebuilt all three executables with VBCC. The
physical MUI suite verified independent task creation, panel navigation,
compilation, embedded editing and backup saves, Git init/commit/status/diff,
shell errors, failed builds retaining the previous executable, program
launch and keyboard interaction, quit and last-workspace restoration.
One screenshot transfer timed out; the suite resumed from the verified
commit fixture and completed. Captures were inspected. A focused stale-output
regression failed before the worker fix and passed after its native rebuild.
All three downloaded executables passed transfer CRC verification.

The final AmiSSL 5.27 HTTPS probe returned the expected unauthenticated 401.
Authenticated model/tool exchange remains unverified without a locally
configured API key. The installed FFS long-filename image was prepared
separately; startup mounting and refusal to recreate the active volume were
exercised. The helper's automatic fresh-image creation path has not been
hardware-tested. Automated checks do not replace the user's manual
assessment of focus, appearance and responsiveness.

## Configure and build

```sh
cp scripts/amiga/termic.yaml .termic.yaml
python3 scripts/amiga/workspace.py configure /path/to/existing/amiga-workspace
python3 scripts/amiga/workspace.py doctor
python3 scripts/amiga/workspace.py build
python3 scripts/amiga/workspace.py test
```

The absolute workspace path is kept only in gitignored
`.context/amiga-workspace.json`. `TERMIC_AMIGA_WORKSPACE` overrides it.
The template `scripts/amiga/termic.yaml` copies the local pointer into new
Termic tasks. The installed root `.termic.yaml` is gitignored by this
repository. Merge the template manually if you already have local actions.
Register this checkout as a Termic project. Run builds the bridge and probe;
the Run dropdown also offers diagnostics, protocol tests, simulator,
hardware bridge, and emulator. Personal project script overrides take
precedence over this repository configuration; clear them to use these actions.

These actions operate on the linked workspace, including its current source
edits and build output. They do not create isolated Amiga source worktrees.
Serialize builds and run only one DevBench server profile at a time. For
parallel source development, give each task its own prepared Amiga workspace
and configure its pointer separately. Host actions require access to the
linked directory; Docker-sandboxed agents need a separately provisioned
container environment and cannot assume that host path is mounted.

The existing workspace owns its compiler image pin, Python environment,
patches, hardware address and MCP configuration. This integration does not
replace them. Use that environment's documented MCP registration for agents
that need hardware tools; starting a server alone does not register MCP.
The wrapper propagates make's exit status. The upstream doctor prints runtime
readiness but may exit successfully even when the runtime is unavailable.

`build` stages Amiga Hunk executables in the linked workspace's
`.context/amiga/shared/Dev/`. It does not deploy or launch them on hardware.
Use the existing DevBench transfer/verification procedure and the configured
Amiga destination. Preserve executable protection bits and test on Workbench.
Protocol simulator tests do not establish Amiga execution or GUI correctness.

## Local verification

The routing tests run with:

```sh
python3 -m unittest discover -s scripts/amiga -p 'test_*.py' -v
```

The four wrapper tests and DevBench MCP smoke test passed (138 tools,
ping and task inspection). The pinned bridge/probe build passed and staged
Hunk binaries and the ARexx probe. Physical probe execution subsequently
passed on the A4000TX; see the acceptance record below.

The configured `podman-machine-default-root` connection requires its VM.
Only one Podman VM can run at a time on this host. The converter VM was
stopped with permission to run the Amiga build. Start the Amiga VM from
Podman Desktop or a persistent terminal before building. No VM is stopped
automatically by these actions.

A separate native Apple Silicon compiler experiment is under this checkout's
`.context/amiga-sdk/`: AmigaPorts 16.2-rc24 built a small C program as an
Amiga Hunk executable with `-m68020 -msoft-float -mcrt=nix20`. It is a release
candidate and has not replaced the existing environment's pinned compiler.
That experiment proves cross-compilation only, not target execution.

Compiler source: [AmigaPorts release](https://github.com/AmigaPorts/m68k-amigaos-gcc/releases/tag/v16.2-rc24).

## Physical acceptance, 2026-11-12

Reused the existing hardware-profile DevBench server and connected bridge.
AmigaDOS reported Kickstart 47.115, Workbench 47.2 and exec.library 47.13.
No existing instrumented clients were running before the test.

The staged `sixies_probe` (32,932 bytes, CRC32 `6ACEA83E`) was transferred
to a unique temporary RAM-disk path. Bridge CRC verification passed in both
directions, and a full downloaded copy matched the local binary byte for
byte. After setting execute permission and a 32 KiB stack, the probe
registered as a client. Two fresh reads showed `ticks` advancing from 1 to
10. Setting `running` to zero produced a stop log after 12 ticks and removed
the client registration.

The temporary executable was deleted. The final bridge ping showed zero
clients and the same free Chip/Fast memory counts as before the test. The
existing host server and target bridge were left running. No installed
applications, startup scripts, firmware or screen preferences were changed.

Local evidence is in this checkout's gitignored `.context/amiga-acceptance/`.
This verifies the existing pinned build's transfer and instrumented execution
on physical hardware. It does not verify Workbench GUI interaction, ARexx,
source-level stepping, ZZ9000 ARM code, or the separate native compiler
experiment. Termic's graphical Run controls were subsequently exercised by the opt-in
e2e check below. A human visual/interaction pass remains outstanding.

## Native Workbench example

The [Workbench demo](../amiga/workbench-demo/README.md) is a native C example
with Increment, Reset and Quit controls. Build this checkout's source with
`python3 scripts/amiga/workspace.py demo-build`, or the new **Build Workbench
demo** Run action. It uses the linked environment's pinned compiler image;
output stays in this checkout's `.context/amiga-demo/`.

Physical transfer/readback, keyboard increment/reset/quit, icon generation and
Workbench launch via `WBLoad` passed. The demo is not an Amiga port of Termic;
it demonstrates the native application build workflow. Mouse interaction and
a human pass through Termic's graphical Run controls remain manual
acceptance work.

## Termic application and UI acceptance

A normal debug application (without the e2e feature) was built and installed
locally as `/Applications/Termic.app`. Launch it from Finder or with
`open -a Termic`. It uses the separate `termic_dev` settings/data directory.
The current checkout is registered as the `muscat` project. Open a task in
that project to use the Run dropdown, including **Amiga environment check**
and **Build Workbench demo**. Agent logins remain the user's existing ones.

The opt-in `e2e/specs/amiga.e2e.ts` test runs these two named commands through
the actual Run dropdown, verifies their run tabs produce output and complete
without the failure flag, and checks a freshly generated Hunk executable.
It passed on macOS. The test uses an isolated fixture project and requires the
linked workspace plus the running pinned compiler VM; it does not deploy to
hardware. Ordinary e2e runs skip it.

```sh
node scripts/e2e-seed.mjs
npm run e2e:build
TERMIC_E2E_AMIGA=1 TERMIC_E2E_VISIBLE=1 npm run test:e2e -- --spec e2e/specs/amiga.e2e.ts
```

Type checking of the e2e sources also passed. Only this focused UI spec was
run for this integration, not the complete application test suite. Captures
under `.context/termic-amiga-*.png` show the test profile's controls, but did
not show terminal text; they are not proof of terminal rendering correctness.
The process-output timestamps and generated executable establish execution.
A human pass still needs to confirm terminal text, focus and button behaviour.
