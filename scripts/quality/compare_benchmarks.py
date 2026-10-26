#!/usr/bin/env python3
"""Validate and replay a same-host benchmark report. No third-party packages."""

import argparse
import json
import math
import statistics
from pathlib import Path


class InvalidReport(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise InvalidReport(message)


def number(value, label, positive=False):
    require(isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value) and (value > 0 if positive else value >= 0),
            f"invalid {label}: {value!r}")
    return value


def percentile(values, fraction=0.95):
    """Linear interpolation at (n-1)*p, including endpoints."""
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lo, hi = math.floor(position), math.ceil(position)
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (position - lo)


def describe(values):
    median = statistics.median(values)
    mad = statistics.median(abs(value - median) for value in values)
    return {"median": median, "p95": percentile(values), "mad": mad,
            "mad_over_median": mad / median if median else 0,
            "min": min(values), "max": max(values), "samples": len(values)}


def compare(report):
    require(isinstance(report, dict), "report must be a JSON object")
    require(report.get("schema") == 1, "unsupported report schema")
    require(report.get("system") and report.get("environment"), "missing host/environment metadata")
    require(report.get("harness_sha256"), "missing harness fingerprint")
    require(report.get("protocol") == "alternating-independent-processes-v1", "wrong sampling protocol")
    budget = report["budget"]
    for key in ("time", "rss", "gc_p95", "allocations", "noise"):
        number(budget[key], f"budget {key}", positive=True)
    count, warmups = report["sample_count"], report["warmup_count"]
    require(isinstance(count, int) and count >= 10, "at least 10 independent samples required")
    require(isinstance(warmups, int) and warmups >= 3, "at least 3 warm-ups required")
    sources, builds = report["sources"], report["builds"]
    for side in ("baseline", "candidate"):
        require(sources[side].get("commit") and sources[side].get("snapshot_sha256"),
                f"missing {side} source identity")
        require(builds[side].get("binary_sha256"), f"missing {side} binary identity")
        require(builds[side]["meta"].get("build") == "Release", "non-Release timing")
        require(builds[side]["meta"].get("coverage") == "OFF", "coverage timing is forbidden")
        flags = builds[side]["meta"].get("flags", "").lower()
        require(not any(token in flags for token in ("sanitize", "coverage", "-pg", "profile")),
                "instrumented timing is forbidden")
        effective_flags = " ".join(str(value) for key, value in builds[side]["cmake"].items()
                                   if "FLAGS" in key).lower()
        require(not any(token in effective_flags for token in ("sanitize", "coverage", "-pg", "profile")),
                "instrumented CMake compile/link flags are forbidden")
    require(builds["baseline"]["meta"] == builds["candidate"]["meta"],
            "build/harness/GC metadata mismatch")
    require(builds["baseline"]["cmake"] == builds["candidate"]["cmake"], "CMake configuration mismatch")
    plan = report["plan"]
    names = [item["case"] for item in plan]
    require(names and len(names) == len(set(names)), "empty/duplicate case plan")
    groups = {(side, name): [] for side in ("baseline", "candidate") for name in names}
    checksums = {name: set() for name in names}
    seen = set()
    iterations = {item["case"]: item["iterations"] for item in plan}
    for item in report["samples"]:
        side, phase, pair, record = item["side"], item["phase"], item["pair"], item["record"]
        name = record["case"]
        require(side in ("baseline", "candidate") and name in names, "unexpected side/case")
        require(phase in ("warmup", "measure"), "unexpected sample phase")
        limit = warmups if phase == "warmup" else count
        require(isinstance(pair, int) and 0 <= pair < limit, "sample index out of range")
        expected_side = ("baseline", "candidate") if pair % 2 == 0 else ("candidate", "baseline")
        require(item["order"] == expected_side.index(side), "non-alternating process order")
        key = (side, name, phase, pair)
        require(key not in seen, "duplicate sample")
        seen.add(key)
        require(record["iterations"] == iterations[name] and iterations[name] > 0,
                "iteration mismatch")
        number(record["checksum"], "checksum", positive=True)
        checksums[name].add(record["checksum"])
        number(record["ns_per_iteration"], "timing", positive=True)
        number(record["peak_rss_bytes"], "RSS", positive=True)
        if name.startswith("gc_burst_"):
            for field in ("allocation_count", "allocation_bytes", "gc_allocation_count",
                          "gc_allocation_bytes", "managed_allocations", "stack_resize_count",
                          "stack_trim_count", "tracked_after", "swept"):
                number(record[field], field)
            pauses = record["gc_pauses_ns"]
            require(len(pauses) == iterations[name], "missing GC pause observations")
            for value in pauses:
                number(value, "GC pause", positive=True)
            require(record["managed_allocations"] == 1024 and record["swept"] == 1024
                    and record["tracked_after"] == 1, "GC roots/cycles invariant failed")
            if name == "gc_burst_collect_reuse":
                require(record["stack_resize_count"] == 0 and record["stack_trim_count"] == 0,
                        "periodic GC capacity thrashing")
        if phase == "measure":
            groups[side, name].append(record)
    require(len(seen) == len(names) * 2 * (count + warmups), "missing samples/warm-ups/cases")
    results = []
    for name in names:
        left, right = groups["baseline", name], groups["candidate", name]
        require(len(checksums[name]) == 1,
                f"output checksum mismatch: {name}")
        metrics = {"ns_per_iteration": budget["time"], "peak_rss_bytes": budget["rss"]}
        if name.startswith("gc_burst_"):
            metrics.update(allocation_count=budget["allocations"],
                           allocation_bytes=budget["allocations"],
                           gc_allocation_count=budget["allocations"])
        metric_results, reasons, failed, noisy = {}, [], False, False
        for field, threshold in metrics.items():
            base, cand = describe([row[field] for row in left]), describe([row[field] for row in right])
            delta = cand["median"] / base["median"] - 1 if base["median"] else (0 if not cand["median"] else math.inf)
            # JSON must remain finite even when a zero baseline becomes nonzero.
            regression = delta > threshold
            metric_results[field] = {"baseline": base, "candidate": cand,
                                     "change_fraction": delta if math.isfinite(delta) else None,
                                     "budget": threshold, "regression": regression}
            failed |= regression
            if regression:
                reasons.append(f"{field} exceeds {threshold:.0%} budget")
            if max(base["mad_over_median"], cand["mad_over_median"]) > budget["noise"]:
                noisy = True
                reasons.append(f"{field} MAD/median exceeds {budget['noise']:.0%}")
        insufficient = False
        if name.startswith("gc_burst_"):
            base = describe([percentile(row["gc_pauses_ns"]) for row in left])
            cand = describe([percentile(row["gc_pauses_ns"]) for row in right])
            # Gate pooled p95; also show dispersion of the independent process p95s.
            base_tail = percentile([p for row in left for p in row["gc_pauses_ns"]])
            cand_tail = percentile([p for row in right for p in row["gc_pauses_ns"]])
            delta = cand_tail / base_tail - 1
            metric_results["gc_pause_p95_ns"] = {
                "baseline": base, "candidate": cand, "baseline_pooled_p95": base_tail,
                "candidate_pooled_p95": cand_tail, "change_fraction": delta,
                "budget": budget["gc_p95"], "regression": delta > budget["gc_p95"]}
            insufficient = count < 40
            if insufficient:
                reasons.append("GC p95 requires >=40 independent processes per version")
            elif delta > budget["gc_p95"]:
                failed = True
                reasons.append("GC pause p95 exceeds budget")
            if max(base["mad_over_median"], cand["mad_over_median"]) > budget["noise"]:
                noisy = True
                reasons.append("GC process p95 MAD/median exceeds noise budget")
        status = "INCONCLUSIVE" if noisy or insufficient else "FAIL" if failed else "PASS"
        results.append({"case": name, "status": status, "reasons": reasons, "metrics": metric_results})
    status = "FAIL" if any(row["status"] == "FAIL" for row in results) else (
        "INCONCLUSIVE" if any(row["status"] == "INCONCLUSIVE" for row in results) else "PASS")
    return {"status": status, "cases": results}


def summarize(result):
    lines = [f"Benchmark gate: {result['status']}",
             "case | status | time median baseline/candidate (ns) | RSS baseline/candidate (MiB)"]
    for row in result["cases"]:
        timing, rss = row["metrics"]["ns_per_iteration"], row["metrics"]["peak_rss_bytes"]
        lines.append(f"{row['case']} | {row['status']} | "
                     f"{timing['baseline']['median']:.1f}/{timing['candidate']['median']:.1f} | "
                     f"{rss['baseline']['median']/1048576:.2f}/{rss['candidate']['median']/1048576:.2f}")
        if "gc_pause_p95_ns" in row["metrics"]:
            gc = row["metrics"]["gc_pause_p95_ns"]
            lines.append(f"  GC pause pooled p95: {gc['baseline_pooled_p95']:.1f}/"
                         f"{gc['candidate_pooled_p95']:.1f} ns")
        lines.extend(f"  {reason}" for reason in row["reasons"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    try:
        result = compare(json.loads(args.report.read_text()))
        print(summarize(result), end="")
        return {"PASS": 0, "FAIL": 1, "INCONCLUSIVE": 2}[result["status"]]
    except (InvalidReport, KeyError, TypeError, ValueError, OSError) as error:
        print(f"INVALID: {error}")
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
