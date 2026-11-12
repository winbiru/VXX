"""Đo A/B AOT và tham chiếu C/Rust, kiểm tra kết quả trước khi tính tốc độ."""

import argparse
import hashlib
import json
import os
import platform
import random
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--optimized", type=Path, required=True)
    parser.add_argument("--c", type=Path, required=True)
    parser.add_argument("--rust", type=Path, required=True)
    parser.add_argument("--typed", type=Path, help="Kernel i32 V++ dùng cùng bộ đo với C/Rust")
    parser.add_argument("--runs", type=int, default=7)
    parser.add_argument("--target-ms", type=float, default=50)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.runs < 3 or args.target_ms <= 0:
        parser.error("Cần ít nhất ba mẫu và thời gian đích dương")
    programs = {name: getattr(args, name).resolve()
                for name in ("baseline", "optimized", "c", "rust")}
    if args.typed:
        programs["typed"] = args.typed.resolve()
    cases = ("arithmetic", "branch_loop", "fibonacci", "list_128",
             "unicode_replace", "json_parse")
    rows, calibration, summaries = [], {}, {}
    rng = random.Random(112603)
    report = dict(platform=platform.platform(), machine=platform.machine(),
                  seed=17, runs=args.runs, target_ms=args.target_ms,
                  warmup_batches=3, rows=rows, calibration=calibration,
                  summary=summaries, executables={
                      name: dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
                      for name, path in programs.items()})

    def run(name, case, iterations):
        env = dict(os.environ, BENCH_CASE=case, BENCH_ITERATIONS=str(iterations),
                   BENCH_SEED="17", VPP_VM_PROFILE="", VPP_AOT_CPU_PROFILE="")
        process = subprocess.run([str(programs[name])], env=env, text=True,
                                 capture_output=True, timeout=120, check=True)
        value = json.loads(process.stdout.strip())
        if value["case"] != case or value["iterations"] != iterations:
            raise ValueError(f"Sai đầu vào: {name}/{case}")
        if value["elapsed_ms"] <= 0:
            raise ValueError(f"Thời gian không hợp lệ: {name}/{case}")
        return value

    for case in cases:
        names = list(programs) if case in cases[:3] else ["baseline", "optimized"]
        counts = {}
        for name in names:
            # A/B V++ dùng cùng số lượt. C/Rust tăng số lượt để đủ độ phân giải đồng hồ.
            if name == "optimized":
                counts[name] = counts["baseline"]
                continue
            count = 16 if name == "baseline" else 65536
            while True:
                sample = run(name, case, count)
                if sample["elapsed_ms"] >= args.target_ms or count >= 67108864:
                    break
                count *= 2
            counts[name] = count
        calibration[case] = counts
        for index in range(args.runs):
            order = list(names)
            rng.shuffle(order)
            for name in order:
                value = run(name, case, counts[name])
                rows.append(dict(variant=name, case=case, run=index, **{
                    key: value[key] for key in ("iterations", "checks", "checksum", "elapsed_ms",
                                               "ns_per_op", "warmup_ms")}))
        current = [r for r in rows if r["case"] == case]
        if len({tuple(r["checks"]) for r in current}) != 1:
            raise ValueError(f"Sai kết quả từng đầu vào: {case}")
        # Chu kỳ đầu vào có 16 phần tử: checksum phải tỷ lệ đúng số lượt.
        reference = current[0]
        for row in current:
            if (int(row["checksum"]) * reference["iterations"] !=
                    int(reference["checksum"]) * row["iterations"]):
                raise ValueError(f"Sai checksum: {case}/{row['variant']}")
        summary = {}
        for name in names:
            values = [r["ns_per_op"] for r in current if r["variant"] == name]
            median = statistics.median(values)
            summary[name] = dict(median_ns=median, mad_ns=statistics.median(
                abs(v - median) for v in values))
        summaries[case] = summary
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
        print(case, {name: round(value["median_ns"], 3)
                     for name, value in summary.items()}, "ns/op", flush=True)


if __name__ == "__main__":
    main()
