#!/usr/bin/env python3
"""Build/run generic SDL3 presentation checks, with no game or DOS dependencies.

Run in an SDL3 compiler environment: python tools/overlaycheck.py [--sanitize].
Outputs go to build/overlaycheck; --output chooses another directory.
Software targets test different window/output dimensions on every host; an actual
window renderer separately tests resizing and fullscreen. The UI briefly opens.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "clang"))
    parser.add_argument("--output", type=Path, default=Path("build/overlaycheck"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    source = root / "tools/tests/overlay"
    platform = root / "runtime/port/platform"
    exe = out / ("overlaycheck.exe" if os.name == "nt" else "overlaycheck")
    flags = subprocess.check_output(["pkg-config", "--cflags", "--libs", "sdl3"], text=True).split()
    command = [shutil.which(args.cc) or args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
               "-g", "-I" + str(source), "-I" + str(platform), "-I" + str(root / "runtime/port"),
               str(source / "presentation.c"), str(platform / "png.c"), "-o", str(exe)]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command + flags + (["-lm"] if os.name != "nt" else []), check=True)
    for command in ([str(exe), "hidden"], [str(exe), str(out / "composition.png")]):
        result = subprocess.run(command, cwd=out, capture_output=True, text=True)
        print(result.stdout, end="", flush=True)
        print(result.stderr, end="", flush=True)
        result.check_returncode()


if __name__ == "__main__":
    main()
