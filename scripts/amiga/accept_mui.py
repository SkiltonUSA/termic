#!/usr/bin/env python3
"""Opt-in MUI hardware acceptance. Only inject keys into our frontmost window.

Captures actual MUI panels. Does not call a model or read credentials.
Requires a running DevBench MCP on the same host and a candidate installed.
"""

import argparse
import time
import asyncio
import json
from pathlib import Path
import subprocess
import urllib.request
import urllib.parse
from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

TITLE = "Termic Amiga Workspace"


async def main(args):
    args.output.mkdir(parents=True, exist_ok=True)
    evidence = (
        json.loads((args.output / "acceptance.json").read_text())
        if args.resume_task
        else []
    )
    async with streamablehttp_client(args.server + "/mcp") as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()

            async def call(name, arguments):
                r = await session.call_tool(name, arguments)
                text = "\n".join(getattr(x, "text", "") for x in r.content)
                evidence.append({"tool": name, "result": text})
                (args.output / "acceptance.json").write_text(
                    json.dumps(evidence, indent=2)
                )
                if r.isError or text.startswith(("ERROR:", "ERR|", "Timeout")):
                    raise RuntimeError(text)
                return text

            if args.worker_errors_only:
                task = args.worker_errors_only
                result = await call(
                    "amiga_run_script",
                    {
                        "script": 'FailAt 21\nStack 262144\nEcho "stale-output-fixture" >'
                        + task
                        + "/.command.log\n"
                        'Echo "nosuchtermiccommand" >' + task + "/command.txt\n"
                        "Termic:TermicAgent --shell --project "
                        + task
                        + " >RAM:termic-worker-after.txt\n"
                        "Type RAM:termic-worker-after.txt\nType " + task + "/.result",
                        "timeout": 20,
                    },
                )
                if (
                    "nosuchtermiccommand" not in result
                    or "stale-output-fixture" in result
                    or "\n0\n" in result
                ):
                    raise RuntimeError("Worker error capture regression: " + result)
                (args.output / "worker-errors.json").write_text(
                    json.dumps(evidence, indent=2)
                )
                return

            async def key(code, title=TITLE, delay=1):
                windows = await call("amiga_list_screen_windows", {})
                first = windows.split("\n\n", 1)[-1].splitlines()[0].strip()
                if not first.startswith(title + "  pos="):
                    raise RuntimeError(
                        "Test window is not frontmost; no key injected: " + first
                    )
                for direction in ("down", "up"):
                    await call(
                        "amiga_input_key", {"rawkey": code, "direction": direction}
                    )
                await asyncio.sleep(delay)

            async def chord(code):
                # Modifier state is released even if a key assertion fails.
                try:
                    for raw in ("0x60",):
                        await call(
                            "amiga_input_key", {"rawkey": raw, "direction": "down"}
                        )
                    await key(code)
                finally:
                    for raw in ("0x60",):
                        await call(
                            "amiga_input_key", {"rawkey": raw, "direction": "up"}
                        )

            async def type_text(text):
                mapping = {
                    c: hex(start + i)
                    for row, start in [
                        ("qwertyuiop", 0x10),
                        ("asdfghjkl", 0x20),
                        ("zxcvbnm", 0x31),
                    ]
                    for i, c in enumerate(row)
                }
                mapping.update({" ": "0x40", ".": "0x39", ";": "0x29", "\n": "0x44"})
                for char in text:
                    await key(mapping[char], delay=0.1)

            async def capture(name):
                with urllib.request.urlopen(
                    args.server
                    + "/api/screenshot?"
                    + urllib.parse.urlencode({"window": TITLE}),
                    timeout=60,
                ) as r:
                    data = json.load(r)
                subprocess.run(
                    [
                        "sips",
                        "-s",
                        "format",
                        "png",
                        data["path"],
                        "--out",
                        str(args.output / name),
                    ],
                    check=True,
                    capture_output=True,
                )

            async def wait_job(expect_failure=False):
                for _ in range(60):
                    await asyncio.sleep(1)
                    result = await call(
                        "amiga_run_script",
                        {
                            "script": "If EXISTS "
                            + task
                            + "/.result\n Type "
                            + task
                            + "/.result\n Echo JOB_COMPLETE\nEndIf",
                            "timeout": 10,
                        },
                    )
                    if "JOB_COMPLETE" in result:
                        if ("\n0\n" in result) == expect_failure:
                            raise RuntimeError("Unexpected job result: " + result)
                        await asyncio.sleep(1)
                        return
                raise RuntimeError("Job did not complete")

            if args.resume_task:
                task = args.resume_task
                fixture = task.rsplit("/Tasks/", 1)[0]
                current = await call(
                    "amiga_run_script",
                    {"script": "Type Termic:Config/last-workspace", "timeout": 10},
                )
                if task not in current:
                    raise RuntimeError("Resume task is not selected")
                result = await call(
                    "amiga_run_script",
                    {
                        "script": "CD " + task + "\nTermic:Tools/amiga-git/git log -1",
                        "timeout": 15,
                    },
                )
                if "fixture commit" not in result:
                    raise RuntimeError(
                        "Resume requires the previously tested fixture commit"
                    )
            else:
                windows = await call("amiga_list_screen_windows", {})
                if TITLE + "  pos=" in windows:
                    await key("0x59")  # F10
                fixture = args.projects_root + "/acceptance-" + str(int(time.time()))
                output = await call(
                    "amiga_run_script",
                    {
                        "script": "FailAt 10\nMakeDir "
                        + fixture
                        + "\nCopy Termic:Template/main.c "
                        + fixture
                        + "/main.c\nCopy Termic:Template/task.txt "
                        + fixture
                        + '/task.txt\nEcho "MUI acceptance" >'
                        + fixture
                        + "/name.txt\nRun >NIL: <NIL: "
                        + args.binary
                        + " --workspace "
                        + fixture,
                        "timeout": 15,
                    },
                )
                if "[STILL RUNNING]" in output:
                    raise RuntimeError("Fixture preparation did not finish")
                await asyncio.sleep(2)
                windows = await call("amiga_list_screen_windows", {})
                if TITLE + "  pos=" not in windows:
                    raise RuntimeError("MUI window did not open: " + windows)
                await capture("mui-agent.png")
                for code, name in [
                    ("0x51", "mui-editor.png"),
                    ("0x52", "mui-output.png"),
                    ("0x53", "mui-changes.png"),
                    ("0x54", "mui-settings.png"),
                ]:
                    await key(code)
                    await capture(name)
                await key("0x58")  # F9 creates an independent task
                task = fixture + "/Tasks/task-0001"
                for _ in range(20):
                    output = await call(
                        "amiga_run_script",
                        {"script": "Type Termic:Config/last-workspace", "timeout": 15},
                    )
                    if task in output:
                        break
                    await asyncio.sleep(1)
                else:
                    raise RuntimeError("New task was not selected: " + output)

                await key("0x55")  # F6 local build
                await wait_job()
                await capture("mui-build-success.png")
                await key("0x51")
                await capture("mui-task-editor.png")
                # Create and edit a second source file using real MUI controls.
                await chord("0x51")  # Shift-F2: focus new filename
                await type_text("fixture.h")
                await key("0x44")  # Enter creates and focuses the file
                await type_text("int fixture;\n")
                await key("0x57")  # F8 save
                await capture("mui-editor-save.png")
                text = await call(
                    "amiga_run_script",
                    {"script": "Type " + task + "/fixture.h", "timeout": 10},
                )
                if "int fixture;" not in text:
                    raise RuntimeError(
                        "Embedded editor did not save the typed source: " + text
                    )
                await type_text("int another;\n")
                await key("0x57")
                text = await call(
                    "amiga_run_script",
                    {"script": "Type " + task + "/fixture.h.bak", "timeout": 10},
                )
                if "int fixture;" not in text or "int another;" in text:
                    raise RuntimeError(
                        "Source backup does not preserve the previous save"
                    )
                await capture("mui-edited-header.png")
                # Real local Git repository; no remote, push or user identity changes.
                await key("0x53")
                await chord("0x54")  # Shift-F5: init repository
                await wait_job()
                result = await call(
                    "amiga_run_script",
                    {
                        "script": "FailAt 10\nCD "
                        + task
                        + '\nTermic:Tools/amiga-git/git config user.name "Termic acceptance"\nTermic:Tools/amiga-git/git config user.email user@example.com\nEcho GIT_IDENTITY_READY',
                        "timeout": 15,
                    },
                )
                if "GIT_IDENTITY_READY" not in result:
                    raise RuntimeError("Local Git identity setup failed")
                await chord("0x53")  # Shift-F4: focus commit message
                await type_text("fixture commit")
                await key("0x44")  # Enter stages source and commits
                await wait_job()
                result = await call(
                    "amiga_run_script",
                    {
                        "script": "FailAt 10\nCD "
                        + task
                        + "\nTermic:Tools/amiga-git/git log -1",
                        "timeout": 15,
                    },
                )
                if "fixture commit" not in result:
                    raise RuntimeError(
                        "MUI commit did not create a Git commit: " + result
                    )
            await capture("mui-git-commit.png")
            await chord("0x55")  # Shift-F6: status
            await wait_job()
            await capture("mui-git-status.png")
            await key("0x51")
            await type_text("int changed;\n")
            await key("0x57")
            await chord("0x56")  # Shift-F7: review the actual source diff
            await wait_job()
            result = await call(
                "amiga_run_script",
                {"script": "Type " + task + "/job.log", "timeout": 10},
            )
            if "fixture.h" not in result or "+int changed;" not in result:
                raise RuntimeError("Git diff did not show the edited header: " + result)
            await capture("mui-git-diff.png")
            await chord("0x52")  # Shift-F3: explicit shell command
            await type_text("nosuchtermiccommand")
            await key("0x44")
            await wait_job(expect_failure=True)
            result = await call(
                "amiga_run_script",
                {"script": "Type " + task + "/job.log", "timeout": 10},
            )
            if "nosuchtermiccommand" not in result or "diff --git" in result:
                raise RuntimeError(
                    "Shell error diagnostics were not captured: " + result
                )
            await capture("mui-shell-error.png")
            await call(
                "amiga_run_script",
                {
                    "script": 'Echo "nosuchtermiccompiler" >'
                    + task
                    + "/build-command.txt",
                    "timeout": 10,
                },
            )
            await key("0x55")
            await wait_job(expect_failure=True)
            result = await call(
                "amiga_run_script",
                {
                    "script": "Type "
                    + task
                    + "/build.log\nIf EXISTS "
                    + task
                    + "/Program\n Echo PREVIOUS_PROGRAM_PRESERVED\nEndIf\nDelete "
                    + task
                    + "/build-command.txt",
                    "timeout": 10,
                },
            )
            if (
                "nosuchtermiccompiler" not in result
                or "PREVIOUS_PROGRAM_PRESERVED" not in result
            ):
                raise RuntimeError(
                    "Failed build lost diagnostics or the previous executable: "
                    + result
                )
            await capture("mui-build-error.png")
            await key("0x55")
            await wait_job()
            await key("0x56")  # F7 runs the compiled fixture
            for _ in range(20):
                windows = await call("amiga_list_screen_windows", {})
                if "Termic Project  pos=" in windows:
                    break
                await asyncio.sleep(1)
            else:
                raise RuntimeError("Run did not launch the compiled program")
            for code in ("0x17", "0x17", "0x13", "0x10"):
                await key(code, "Termic Project")
            await wait_job()
            await key("0x59")  # quit
            windows = await call("amiga_list_screen_windows", {})
            if TITLE + "  pos=" in windows:
                raise RuntimeError("Quit did not close the MUI window")
            await call(
                "amiga_run_script",
                {"script": "Run >NIL: <NIL: " + args.binary, "timeout": 15},
            )
            await asyncio.sleep(2)
            text = await call(
                "amiga_run_script",
                {"script": "Type Termic:Config/last-workspace", "timeout": 10},
            )
            if task not in text:
                raise RuntimeError("Last workspace was not restored")
            await key("0x51")
            await capture("mui-final-workspace.png")
            (args.output / "passed.json").write_text(
                json.dumps({"project": fixture, "task": task})
            )
            (args.output / "acceptance.json").write_text(json.dumps(evidence, indent=2))


def cli():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--projects-root", default="Termic:Projects")
    p.add_argument(
        "--worker-errors-only",
        help="Check stale-output regression in an existing fixture task",
    )
    p.add_argument(
        "--resume-task",
        help="Resume after a capture interruption following the fixture commit",
    )
    p.add_argument("--binary", default="Termic:TermicNative")
    p.add_argument("--server", default="http://127.0.0.1:55010")
    p.add_argument("--output", type=Path, default=Path(".context/amiga-mui"))
    asyncio.run(main(p.parse_args()))


if __name__ == "__main__":
    cli()
