# Amiga development in Termic

This checkout configures Termic's existing Run controls for an existing
Amiga DevBench workspace. Termic and agents run on the Mac; AmigaOS programs
run on the A4000TX. This is a project workflow integration, not a native
AmigaOS port of the Tauri application.

The supplied environment specifies TF4060/68060, Workbench 3.2.3, 2 MiB
Chip and 624 MiB Fast RAM, ZZ9000 XX19c, and X-Surf 100 with Roadshow.
The linked workspace's `amiga/target.json`, `docs/development-amiga.md`
and `docs/amiga-session-memory.md` remain authoritative. Read them before
hardware work. ZZ9000 ARM programs are a separate target from AmigaOS/68k.

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
experiment. Termic's graphical Run controls have not been manually tested;
this session exercised their configured command-line actions.
