#!/usr/bin/env python3
"""Opt-in physical checks for the native About window and Amiga version tags."""

import asyncio
import json
import re
import subprocess
import urllib.parse
import urllib.request
from pathlib import Path
from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client


async def main():
    version = re.search(
        r'#define TERMIC_VERSION "([^"]+)"', Path("amiga/native/version.h").read_text()
    ).group(1)
    out = Path(".context/amiga-about")
    out.mkdir(exist_ok=True)
    async with streamablehttp_client("http://127.0.0.1:55010/mcp") as (r, w, _):
        async with ClientSession(r, w) as session:
            await session.initialize()

            async def call(name, args):
                result = await session.call_tool(name, args)
                text = "\n".join(getattr(c, "text", "") for c in result.content)
                with (out / "evidence.log").open("a") as f:
                    f.write(name + "\n" + text + "\n")
                if result.isError:
                    raise RuntimeError(text)
                return text

            async def key(raw, title="Termic Amiga Workspace"):
                windows = await call("amiga_list_screen_windows", {})
                first = windows.split("\n\n", 1)[-1].splitlines()[0].strip()
                assert first.startswith(title + "  pos="), first
                for direction in ["down", "up"]:
                    await call(
                        "amiga_input_key", {"rawkey": raw, "direction": direction}
                    )
                await asyncio.sleep(0.7)

            async def capture(title, name):
                with urllib.request.urlopen(
                    "http://127.0.0.1:55010/api/screenshot?"
                    + urllib.parse.urlencode({"window": title}),
                    timeout=60,
                ) as response:
                    data = json.load(response)
                subprocess.run(
                    [
                        "sips",
                        "-s",
                        "format",
                        "png",
                        data["path"],
                        "--out",
                        str(out / name),
                    ],
                    check=True,
                    capture_output=True,
                )

            for local, remote in [
                ("TermicNativeMUI", "TermicAbout"),
                ("TermicAgentMUI", "TermicAgentVer"),
                ("TermicStorage", "TermicStorageVer"),
            ]:
                await call(
                    "amiga_push_file",
                    {
                        "local_path": str(
                            Path(".context/amiga-native", local).resolve()
                        ),
                        "amiga_path": "Termic:" + remote,
                    },
                )
                result = await call(
                    "amiga_run_script",
                    {
                        "script": "Protect Termic:"
                        + remote
                        + " +e\nTermic:"
                        + remote
                        + " --version\nVersion Termic:"
                        + remote
                        + " FILE FULL",
                        "timeout": 10,
                    },
                )
                assert result.count(version) == 2, result
            await key("0x59")
            await call(
                "amiga_run_script",
                {"script": "Run >NIL: <NIL: Termic:TermicAbout", "timeout": 10},
            )
            await asyncio.sleep(2)
            await key("0x51")  # Focus the embedded editor first
            await key("0x5f")  # Help opens About, even from the embedded editor
            windows = await call("amiga_list_screen_windows", {})
            assert windows.count("About Termic Amiga  pos=") == 1, windows
            await capture("About Termic Amiga", "about.png")
            await key("0x45", "About Termic Amiga")  # Escape closes only About
            windows = await call("amiga_list_screen_windows", {})
            assert (
                "About Termic Amiga  pos=" not in windows
                and "Termic Amiga Workspace  pos=" in windows
            )
            await key("0x5f")
            windows = await call("amiga_list_screen_windows", {})
            assert windows.count("About Termic Amiga  pos=") == 1, windows
            await key("0x44", "About Termic Amiga")  # Return closes the reused window
            await key("0x51")
            await capture("Termic Amiga Workspace", "workspace.png")
            (out / "passed.json").write_text(
                json.dumps(
                    {
                        "version": version,
                        "open_close_reopen": True,
                        "version_tags": 3,
                    }
                )
            )


if __name__ == "__main__":
    asyncio.run(main())
