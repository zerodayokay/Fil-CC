#!/usr/bin/env python3
"""Measure native Clang, stock Fil-C, and the Fil-CC flag on and off."""

import argparse
import hashlib
import json
import os
import platform
import random
import re
import shlex
import shutil
import statistics
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
BATCH = ["-mllvm", "-filcc-batch-reads"]
IMAGE = HERE.parent / "benchmarkData" / "Pizigani_1367_Chart_10MB.pnm"


def command(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def verify(executable, safe):
    command([executable], stdout=subprocess.DEVNULL)
    if not safe:
        return
    for case in ("null", "short", "freed"):
        result = subprocess.run(
            [executable, case], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
        )
        if not result.returncode or not re.search(
            r"(safety error|panic|out.of.bounds|invalid.*capability)", result.stdout, re.I
        ):
            raise RuntimeError(f"{executable.name} {case}: expected a Fil-C diagnostic\n{result.stdout}")


def measure(executable, workload):
    result = command([executable, *workload], capture_output=True)
    elapsed, units, checksum = map(int, result.stdout.split())
    return {"ns": elapsed, "units": units, "checksum": checksum}


def ratio_interval(stock, modified):
    rng = random.Random(20261127)
    ratios = [a["ns"] / b["ns"] for a, b in zip(stock, modified, strict=True)]
    draws = sorted(
        statistics.median(rng.choices(ratios, k=len(ratios))) for _ in range(4000)
    )
    return [statistics.median(ratios), draws[100], draws[3899]]


def median_per_unit(samples):
    return statistics.median(s["ns"] / s["units"] for s in samples)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", default="clang", help="unmodified Clang")
    parser.add_argument("--stock", required=True, help="stock Fil-C compiler")
    parser.add_argument("--filcc", required=True, help="patched Fil-CC compiler")
    parser.add_argument("--out", type=Path, default=Path("filcc-results"))
    parser.add_argument("--image", type=Path, default=IMAGE)
    parser.add_argument("--samples", type=int, default=11)
    parser.add_argument("--cpu", type=int, help="Linux logical CPU, defaults to first allowed")
    parser.add_argument("--cflags", default="-O3 -march=native")
    parser.add_argument("--perf", action="store_true", help="also collect whole-process perf counters")
    args = parser.parse_args()
    if args.perf and not shutil.which("perf"):
        parser.error("--perf requires the Linux perf command")
    if args.samples < 3:
        parser.error("--samples must be at least 3")
    if not args.image.is_file():
        parser.error(f"missing PPM input {args.image}")

    affinity = sorted(os.sched_getaffinity(0))
    cpu = args.cpu if args.cpu is not None else affinity[0]
    os.sched_setaffinity(0, {cpu})
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    flags = shlex.split(args.cflags)
    toolchains = {
        "native": (args.native, []),
        "stock": (args.stock, []),
        "off": (args.filcc, []),
        "filcc": (args.filcc, BATCH),
    }
    metadata = {}
    executables = {}
    for label, (compiler, extra) in toolchains.items():
        compiler_path = shutil.which(compiler)
        if not compiler_path:
            parser.error(f"compiler not found {compiler}")
        cc = [str(Path(compiler_path).absolute()), *flags, *extra]
        version = command([cc[0], "--version"], capture_output=True).stdout.strip()
        metadata[label] = {"version": version, "compile_prefix": cc}
        checks = out / f"check-{label}"
        run = out / f"bench-{label}"
        for stem, output in (("bench", run), ("check", checks)):
            command([*cc, HERE / "kernels.c", HERE / f"{stem}.c", "-o", output])
        verify(checks, label != "native")
        ir = out / f"kernels-{label}.ll"
        diagnostic = ["-mllvm", "-filc-light-verbose"] if label != "native" else []
        with (out / f"lowering-{label}.log").open("w") as log:
            command([*cc, *diagnostic, "-S", "-emit-llvm", HERE / "kernels.c", "-o", ir], stderr=log)
        executables[label] = run

    ir_on = (out / "lowering-filcc.log").read_text()
    ir_off = (out / "lowering-off.log").read_text()
    if ("filcc.batch.guard" not in ir_on or "load <4 x i32>" not in ir_on
            or "filcc.batch.guard" in ir_off):
        raise RuntimeError("Expected checked vector batches only with -filcc-batch-reads")

    workloads = {
        "scan-16KiB": ("scan", "4096", "8192"),
        "scan-4MiB": ("scan", "1048577", "32"),
        "scan-128MiB": ("scan", "33554433", "3"),
        "callback-mono": ("callback-mono", "1048577", "20"),
        "callback-mixed": ("callback-mixed", "1048577", "20"),
        "callback-aot-mono": ("callback-aot-mono", "1048577", "20"),
        "callback-aot-mixed": ("callback-aot-mixed", "1048577", "20"),
        "chase": ("chase", "65536", "64"),
        "ppm": ("ppm", str(args.image.resolve()), "3"),
    }
    cpu_lines = Path("/proc/cpuinfo").read_text().splitlines()
    cpu_model = next(line.partition(":")[2].strip() for line in cpu_lines
                     if line.startswith("model name"))
    with args.image.open("rb") as image_file:
        digest = hashlib.file_digest(image_file, "sha256").hexdigest()
    results = {
        "machine": {"platform": platform.platform(), "cpu": cpu_model, "logical_cpu": cpu},
        "toolchains": metadata,
        "image_sha256": digest,
        "sample_count": args.samples,
        "workloads": {},
    }
    rng = random.Random(20261127)
    print("workload              native     stock       off     filcc    stock / filcc (95% CI)")
    for name, workload in workloads.items():
        samples = {label: [] for label in executables}
        order = list(executables)
        for sample in range(args.samples + 1):
            rng.shuffle(order)
            checksums = set()
            for label in order:
                observation = measure(executables[label], workload)
                checksums.add((observation["units"], observation["checksum"]))
                if sample:
                    samples[label].append(observation)
            if len(checksums) != 1:
                raise RuntimeError(f"Checksum or unit count disagrees for {name}: {checksums}")
        medians = {label: median_per_unit(values) for label, values in samples.items()}
        ratio, lo, hi = ratio_interval(samples["stock"], samples["filcc"])
        record = {
            "command_tail": workload,
            "median_ns_per_unit": medians,
            "stock_over_filcc_95ci": [ratio, lo, hi],
            "off_over_filcc_95ci": ratio_interval(samples["off"], samples["filcc"]),
            "samples": samples,
        }
        if name.startswith("callback-aot-"):
            generic = name.replace("-aot", "")
            generic_samples = results["workloads"][generic]["samples"]
            if any(samples[label][0]["checksum"] != generic_samples[label][0]["checksum"]
                   for label in samples):
                raise RuntimeError(f"Specialized callback checksum differs for {name}")
        results["workloads"][name] = record
        if args.perf:
            for label, exe in executables.items():
                path = out / f"perf-{name}-{label}.csv"
                with path.open("w") as log:
                    perf = subprocess.run(
                        ["perf", "stat", "-x,", "-e", "cycles,instructions,branches,branch-misses",
                         exe, *workload],
                        stdout=subprocess.DEVNULL, stderr=log,
                    )
                if perf.returncode:
                    record["perf_unavailable"] = True
        times = "  ".join(f"{value:8.3f}" for value in medians.values())
        print(f"{name:<20} {times}    {ratio:.3f}x [{lo:.3f}, {hi:.3f}]", flush=True)
        (out / "results.json").write_text(json.dumps(results, indent=2) + "\n")


if __name__ == "__main__":
    main()
