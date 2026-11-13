# Amiga development in Termic

This checkout provides two Amiga workflows: the existing Mac Termic/DevBench
integration described below, and a separate native Workbench client in
[`amiga/native`](../amiga/native/ReadMe). The native client edits and builds
a C project on the Amiga and calls OpenAI directly through AmiSSL. It does
not require the Mac or DevBench at runtime. It is a first native client,
not a port of React/Tauri or the complete desktop feature set.

The supplied environment specifies TF4060/68060, Workbench 3.2.3, 2 MiB
Chip and 624 MiB Fast RAM, ZZ9000 XX19c, and X-Surf 100 with Roadshow.
The linked workspace's `amiga/target.json`, `docs/development-amiga.md`
and `docs/amiga-session-memory.md` remain authoritative. Read them before
hardware work. ZZ9000 ARM programs are a separate target from AmigaOS/68k.

## Native Workbench client

The native client has Source, Task, Build, Run, Agent, Log, Model,
API key and HTTPS test controls. Ed opens source and configuration files;
VBCC builds `Project/main.c` into `Project/Program`. A separate DOS process
runs compiler and network jobs so the Workbench window stays responsive.
Only the user-facing Run action executes generated programs.

The cloud agent uses the
[Responses function-calling protocol](https://developers.openai.com/api/docs/guides/function-calling).
It can read `main.c`, replace it with a backup, and compile locally. Each run
is limited to eight model requests. It sends the task, source and compiler
results to OpenAI and uses stored response IDs for conversation continuity.
The model is editable; the sample config uses
[`gpt-5-mini`](https://developers.openai.com/api/docs/models/gpt-5-mini).
Enter an API key locally using the **API key** control; it is never included
in prompts, command arguments, transcripts or the packaged installation kit.
The file is plain text on classic AmigaOS, which has no per-user isolation.

HTTPS requires an accurate clock, a running TCP/IP stack and the installed
AmiSSL CA certificates. The transport verifies certificates and hostnames,
uses SNI, and requires TLS 1.2 or later. It has no HTTP fallback. The HTTPS
test expects HTTP 401 from `/v1/models` without sending any API key. This
test establishes connectivity, not authenticated model access.

The native compiler setup uses the original VBCC, NDK 3.2 and AmiSSL 5.23 SDK
archives pinned by SHA-256 in `amiga/native/sdk-lock.json`. Native scripts
set session assignments and paths; they do not edit system startup files.
`InstallSDK` extracts the SDKs, `BuildNative` rebuilds both Termic programs,
and `BuildProject` compiles the editable example. The worker uses 256 KiB of
stack and software floating point through the Amiga IEEE math libraries.
The TLS unit uses a generated subset of the pinned SDK's VBCC bindings to
avoid compiling unused OpenSSL stack helpers. Regenerate or verify it with
`scripts/amiga/generate_amissl_subset.py SDK/Developer/include [--check]`.
It copies SDK declarations, constants and library offsets; the implementation
still calls the installed AmiSSL library. Other compilers use the full SDK.

Create a local installation archive from built binaries and verified SDKs:

```sh
python3 scripts/amiga/package_native.py \
  --downloads .context/native-downloads \
  --binaries .context/amiga-native \
  --output .context/amiga-native/TermicNative.lha \
  --lha /path/to/host/lha
```

Use `--download` to retrieve missing SDK archives. A changed upstream
archive fails hash verification. The package contains fresh sample files
and an empty API key, never a copy of the live Amiga project or config.
Unpack into a new drawer and follow the included `ReadMe` from an Amiga
Shell. It includes all native sources and the original SDK archives.

Current scope is one source file (32 KiB), one task (8 KiB), bounded compiler
output (16 KiB), and responses up to 256 KiB. Network waits have a 120-second
deadline after DNS; DNS uses the stack's resolver timeout. There is no Git,
streaming, multi-file editing or parallel agent support yet. The fixed tool
list is not an OS sandbox: compilation and generated code have normal
AmigaOS access. Review source before Run. A `.busy` directory prevents
overlapping workers; after a crash, remove it only when the worker is gone.

The ZZ9000 supplies the existing RTG screen. No ARM payload or acceleration
is enabled. The agent, compiler and TLS currently run on the 68k. The
existing applications may own the ARM core; acceleration requires a
measured compute workload, exclusive ownership, and the established
shared-memory/cache contract. No firmware or graphics settings are changed.

Portable tests run with `python3 scripts/amiga/test_native.py`. They use
synthetic responses and ASan/UBSan to exercise HTTP framing and agent
read/edit/build behavior, error responses, incomplete output, parallel-call
rejection, cancellation, missing credentials and the step limit.

The opt-in `scripts/amiga/accept_native.py` uses an existing DevBench MCP
connection to build the installed sample, test its keyboard controls and
capture only its windows. It also checks Workbench client launch, Quit and
the Build keyboard action, then leaves its build console open for inspection.
It never contacts a model or reads a key. Run it with the DevBench Python
environment while the Amiga keyboard is idle. It writes local evidence to
`.context/amiga-native/`; inspect the images separately. It does not test
mouse interaction or establish authenticated cloud behavior.

### Native hardware acceptance, 2026-11-12

Installed the SDK and native sources in a separate Termic drawer on the
A4000TX. Both `TermicAgent` and `TermicNative` were rebuilt successfully by
the installed native VBCC toolchain. The supplied counter project also
compiled locally. This goes beyond the earlier cross-compiled GUI demo.

The clock check refused the machine's 1978 date. After correcting the
running system clock, the native-built worker completed verified HTTPS to
OpenAI and received the expected unauthenticated HTTP 401. The RTC was not
written. Check the clock again after reboot if the machine does not retain
time. No API key was supplied, so a real authenticated model/tool exchange
remains unverified; the portable agent-loop tests use synthetic responses.

The written hardware suite verified project Increment/Reset/Quit,
Workbench client launch/Quit, and the Build action starting its asynchronous
worker and producing a fresh executable. Captures were inspected and show
the counter at 2 and then 0, the native client, and compiler exit code 0.
These are automated checks, not a
human mouse/focus test. The native window and compiler worker do not use
the Mac bridge at runtime; DevBench was used only for installation and tests.

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
