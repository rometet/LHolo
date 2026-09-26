"""Compile and execute the same pure C0 shadow code used by the runtime build."""

from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / "build" / "liquid-cull-shadow-tests.exe"
out.parent.mkdir(parents=True, exist_ok=True)
compiler = os.environ.get("CXX", "clang++")
subprocess.run([
    compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "src"),
    str(root / "tests/liquid_cull_shadow/portable_main.cpp"),
    "-o", str(out),
], cwd=root, check=True)
subprocess.run([str(out)], cwd=root, check=True)
