"""Fresh offline CPU comparison. No game launch, live file or config writes."""
from pathlib import Path
from datetime import datetime, timezone
import argparse, csv, hashlib, io, json, os, statistics, subprocess, tarfile, time

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT.parent / "performance-evidence"
CL = Path(r"E:\rufu client\.tools\llvm-22.1.8\bin\clang-cl.exe")
MSVC = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207")
WINSDK = Path(r"C:\Program Files (x86)\Windows Kits\10")
SDKVER = "10.0.26100.0"
GLM = Path(r"C:\Users\missp\Documents\Codex\2026-10-03\task-2\praxis-offline\packages\g\glm\1.0.1\b35502c78b9847ab89a7994a4f750c71\include")

def environment():
    env = dict(os.environ)
    env["INCLUDE"] = ";".join(str(p) for p in [MSVC / "include"] + [WINSDK / "Include" / SDKVER / n for n in ("ucrt", "shared", "um", "winrt")])
    env["LIB"] = ";".join(str(p) for p in [MSVC / "lib/x64"] + [WINSDK / "Lib" / SDKVER / n / "x64" for n in ("ucrt", "um")])
    env["PATH"] = str(CL.parent) + ";" + str(MSVC / "bin/HostX64/x64") + ";" + env["PATH"]
    env["TEMP"] = env["TMP"] = str(OUT / "temp")
    (OUT / "temp").mkdir(parents=True, exist_ok=True)
    return env

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def compile_bench(source, label):
    directory = OUT / label
    directory.mkdir(parents=True, exist_ok=True)
    exe = directory / "AuditBench.exe"
    argv = [str(CL), "/nologo", "/std:c++20", "/EHsc", "/O2", "/W4", "/WX", "/MD", "/utf-8", "/I" + str(source / "src"), "/I" + str(GLM), "/Fe:" + str(exe), str(source / "tests/bench/AuditBench.cpp")]
    started = time.perf_counter()
    result = subprocess.run(argv, cwd=directory, env=environment(), capture_output=True)
    (directory / "build.log").write_bytes(result.stdout + result.stderr)
    (directory / "build.json").write_text(json.dumps({"argv": argv, "exit": result.returncode, "seconds": time.perf_counter() - started}, indent=2))
    result.check_returncode()
    return exe

def summarize(path):
    rows = list(csv.DictReader(path.read_text().splitlines()))
    result = []
    for cells in sorted({int(r["cells"]) for r in rows}):
        for density in ("dense", "sparse"):
            for mode in ("full", "compact"):
                selected = [r for r in rows if int(r["cells"]) == cells and r["density"] == density and r["path"] == mode]
                item = {"cells": cells, "density": density, "path": mode, "samples": len(selected), "stored_bytes": int(selected[0]["stored_bytes"])}
                for field in ("capture_us", "lookup_us", "total_us"):
                    values = [float(r[field]) for r in selected]
                    item[field] = {"median": statistics.median(values), "mean": statistics.mean(values), "max": max(values)}
                result.append(item)
    return result

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--freeze", help="Commit of measurement-only baseline to freeze")
    parser.add_argument("--baseline-only", action="store_true")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    baseline = OUT / "frozen-baseline"
    if args.freeze:
        if baseline.exists(): raise RuntimeError("Baseline already exists; preserve evidence")
        archive = subprocess.check_output(["git", "archive", "--format=tar", args.freeze], cwd=ROOT)
        baseline.mkdir()
        with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
            tf.extractall(baseline, filter="data")
        (OUT / "baseline-identity.json").write_text(json.dumps({"commit": args.freeze, "tree": subprocess.check_output(["git", "rev-parse", args.freeze + "^{tree}"], cwd=ROOT, text=True).strip(), "archive_sha256": hashlib.sha256(archive).hexdigest()}, indent=2))
    fixtures = {"kind": "synthetic offline section_xyz CPU fixture; not native schematic", "cells": [1048576, 4194304, 8388608, 16777216], "density_stride": [1, 16], "array_pattern": "correction=index%5;actorRenderer=index%2", "sections": "center and six direct +/-16 neighbors", "query_passes": "center=8,neighbors=1", "dimensions": "128Z * (cells/(128*128))Y * 128X", "camera": "not used by snapshot; no render", "warmup": 2, "samples": 31, "production_threshold": 8388608}
    (OUT / "fixtures.json").write_text(json.dumps(fixtures, indent=2) + "\n")
    executables = {"baseline": compile_bench(baseline, "baseline")}
    if not args.baseline_only: executables["candidate"] = compile_bench(ROOT, "candidate")
    records = []
    for run in range(3):
        for label, exe in executables.items():
            started = time.perf_counter()
            result = subprocess.run([str(exe), "--snapshot-profile"], cwd=exe.parent, env=environment(), capture_output=True)
            path = exe.parent / f"profile-{run}.csv"
            path.write_bytes(result.stdout)
            (exe.parent / f"profile-{run}.stderr.log").write_bytes(result.stderr)
            result.check_returncode()
            records.append({"label": label, "run": run, "exit": result.returncode, "seconds": time.perf_counter() - started, "csv_sha256": digest(path), "exe_sha256": digest(exe), "summary": summarize(path)})
            print(f"{label} run {run}: PASS ({time.perf_counter() - started:.2f}s)", flush=True)
        (OUT / ("baseline-results.json" if args.baseline_only else "comparison-results.json")).write_text(json.dumps({"observed_utc": datetime.now(timezone.utc).isoformat(), "scope": "OFFLINE CPU ONLY; FPS/GPU/VRAM/native latency NOT_MEASURED", "fixture_sha256": digest(OUT / "fixtures.json"), "results": records}, indent=2) + "\n")

if __name__ == "__main__": main()
