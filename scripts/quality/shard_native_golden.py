#!/usr/bin/env python3
"""Balance native golden tests while preserving the ordered fixture lane.

External-state fixtures are pinned to worker zero in their original order.
Independent fixtures can use its remaining capacity or any other worker;
repeated source paths always stay in one worker.
"""

from __future__ import annotations

import argparse
from collections import Counter
from fnmatch import fnmatchcase
from pathlib import Path


# Keep the Windows native lane's external-state policy identical to the
# conservative Unix policy in run_tests.sh. The Unix runner also has a shell
# fallback when Python is unavailable.
IN_MEMORY_EXCEPTIONS = {
    "kiem_tra_su_kien.vi", "kiem_tra_khai_bao_kieu_ffi_tu_chu.vi",
    "kiem_tra_goi_mang.vi", "kiem_tra_goi_luong_tu.vi",
    "kiem_tra_mang_3_chieu.vi", "kiem_tra_so_nguyen.vi",
    "kiem_tra_http_thuan_vpp.vi", "kiem_tra_http_response_vpp.vi",
    "kiem_tra_http_may_chu_thuan_vpp.vi", "kiem_tra_goi_may_chu_http.vi",
    "kiem_tra_process_quote_windows.vi", "kiem_tra_socket_ip.vi",
    "kiem_tra_dns_goi_tin.vi",
}
EXTERNAL_STATE_PATTERNS = (
    "*file*", "*tep*", "*thumuc*", "*thu_muc*", "*luu_tru*", "*zip*",
    "*json_file*", "*http*", "*socket*", "*dns*", "*tls*", "*ffi*",
    "*network*", "*mang*", "*o_cam*", "*process*", "*tien_trinh*",
    "*worker*", "*dong_thoi*", "*su_kien*", "*db*", "*database*",
    "*sqlite*", "*api*", "*nhat_ky*", "*logger*", "*cau_hinh*",
    "*thoi_gian*", "*goi_repl*", "*cong_cu*", "*dong_goi*",
    "*goi_dung*", "*preferences*", "*goi_luong_tu*",
    "*goi_ngau_nhien*", "*stdlib_io*", "*goi_kich_ban*",
    "*goi_soan_thao*", "*ngu*", "*entropy*",
)


def is_independent(source: str) -> bool:
    """Conservatively classify fixture side effects for concurrent workers."""
    name = source.replace("\\", "/").rsplit("/", 1)[-1]
    return name in IN_MEMORY_EXCEPTIONS or not any(
        fnmatchcase(name, pattern) for pattern in EXTERNAL_STATE_PATTERNS
    )


def load_costs(path: Path) -> dict[str, int]:
    costs = {}
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        name, milliseconds = line.split("\t")
        cost = int(milliseconds)
        if cost <= 0 or name in costs:
            raise ValueError(f"{path}:{number}: invalid or duplicate fixture cost")
        costs[name] = cost
    return costs


def shard(
    sources: list[str],
    workers: int,
    costs: dict[str, int],
    ordered: list[str] | None = None,
) -> tuple[list[list[str]], list[int]]:
    if workers < 1:
        raise ValueError("worker count must be positive")
    ordered = ordered or []
    if set(ordered) & set(sources):
        raise ValueError("ordered and independent fixtures overlap")

    def cost(source: str) -> int:
        return costs.get(Path(source).stem, 2000)

    multiplicity = Counter(sources)
    loads = [0] * workers
    groups = [[] for _ in range(workers)]
    groups[0].extend(ordered)
    loads[0] = sum(cost(source) for source in ordered)
    owners = {}
    # Largest first minimizes the slowest worker; stable names break ties.
    for source in sorted(multiplicity, key=lambda s: (-(cost(s) * multiplicity[s]), s)):
        worker = min(range(workers), key=lambda index: (loads[index], index))
        owners[source] = worker
        loads[worker] += cost(source) * multiplicity[source]
    for source in sources:
        groups[owners[source]].append(source)
    return groups, loads


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--costs", type=Path, required=True)
    parser.add_argument("--workers", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ordered-file", type=Path, help="newline-delimited fixtures pinned to worker 0")
    parser.add_argument("--sources-file", type=Path, help="UTF-8 newline-delimited full fixture manifest")
    parser.add_argument("--auto-classify", action="store_true", help="partition the manifest into ordered/independent fixtures")
    parser.add_argument("sources", nargs="*")
    options = parser.parse_args()
    weights = load_costs(options.costs)
    sources = options.sources + (options.sources_file.read_text(encoding="utf-8").splitlines() if options.sources_file else [])
    if options.auto_classify:
        if options.ordered_file:
            parser.error("--auto-classify and --ordered-file cannot be combined")
        ordered = [source for source in sources if not is_independent(source)]
        sources = [source for source in sources if is_independent(source)]
    else:
        ordered = options.ordered_file.read_text(encoding="utf-8").splitlines() if options.ordered_file else []
    groups, loads = shard(sources, options.workers, weights, ordered)
    options.output.mkdir(parents=True, exist_ok=True)
    for worker, group in enumerate(groups):
        if any("\n" in source for source in group):
            raise ValueError("test filename contains newline")
        (options.output / f"{worker}.list").write_text(
            "".join(source + "\n" for source in group), encoding="utf-8"
        )
    print(f"Native golden shard estimates (ms): {', '.join(map(str, loads))}", flush=True)


if __name__ == "__main__":
    main()
