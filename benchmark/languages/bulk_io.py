"""Reference bulk-data workloads. Run as child process of benchmark_bulk_io.py."""
import csv
import io
import json
import mmap
import os
import time


CASE = os.environ["BENCH_CASE"]
PATH = os.environ["BENCH_FILE"]
REPEATS = int(os.environ["BENCH_ITERATIONS"])
EXPECTED = int(os.environ["BENCH_EXPECT_UNIT"])
FEATURE_N = int(os.environ.get("BENCH_FEATURE_N", "32"))
FILE_BYTES = int(os.environ["BENCH_FILE_BYTES"])
OUTPUT = os.environ.get("BENCH_OUTPUT")
WRITE_PAYLOAD = open(PATH, "rb").read() if CASE == "write_bytes" else None


def list_append():
    data = []
    for i in range(FEATURE_N):
        data.append(i)
    return sum(data)


def filter_sum():
    values = list(range(FEATURE_N))
    return sum(value for value in values if value % 3 == 0)


def numpy_filter_sum():
    import numpy as np
    values = np.arange(FEATURE_N, dtype=np.int64)
    return int(values[values % 3 == 0].sum())


def sort_numbers():
    values = list(range(FEATURE_N - 1, -1, -1))
    values.sort()
    return sum((i + 1) * v for i, v in enumerate(values))


def linear_search():
    values = list(range(FEATURE_N))
    return values.index(FEATURE_N - 1)


def set_dedup():
    values = [i % 16 for i in range(FEATURE_N)]
    distinct = set(values)
    return len(distinct) + sum(distinct)


def map_lookup():
    data = {"k" + str(i): 2 * i for i in range(FEATURE_N)}
    return sum(data["k" + str(i)] for i in range(FEATURE_N))


def feature_call(x):
    return x * 3 + 7


def function_calls():
    return sum(feature_call(i) for i in range(FEATURE_N))


def string_concat():
    result = ""
    for _ in range(FEATURE_N):
        result += "V++"
    return len(result)


def alloc_objects():
    objects = [{"id": i, "n": i * 2} for i in range(FEATURE_N)]
    return sum(obj["n"] for obj in objects)


def exceptions():
    count = 0
    for i in range(FEATURE_N):
        try:
            if i % 16 == 0:
                raise ValueError("benchmark exception")
        except ValueError:
            count += 1
    return count


def feature_fib(n):
    return n if n < 2 else feature_fib(n - 1) + feature_fib(n - 2)


def recursion():
    return sum(feature_fib(12) for _ in range(FEATURE_N))


def read_bytes():
    with open(PATH, "rb") as f:
        return len(f.read())


def seek_sample():
    total = 0
    with open(PATH, "rb", buffering=4096) as f:
        for i in range(32):
            f.seek((i * 1009 + 17) % FILE_BYTES)
            total += f.read(1)[0]
    return total


def write_bytes():
    with open(OUTPUT, "wb") as f:
        f.write(WRITE_PAYLOAD)
    return len(WRITE_PAYLOAD)


def bytes_sum():
    with open(PATH, "rb") as f:
        return sum(f.read()) % 65521


def numpy_sum():
    import numpy as np
    return int(np.fromfile(PATH, dtype=np.uint8).sum(dtype=np.uint64) % 65521)


def readinto_64k():
    total = 0
    buf = bytearray(65536)
    with open(PATH, "rb", buffering=0) as f:
        while n := f.readinto(buf):
            total += n
    return total


def mmap_read():
    with open(PATH, "rb") as f:
        with mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            # Touch every page, avoiding a benchmark that only measures mmap setup.
            count = 0
            for pos in range(0, len(mapped), 4096):
                count += mapped[pos] * 0 + min(4096, len(mapped) - pos)
            return count


def read_text():
    with open(PATH, "r", encoding="utf-8", newline="") as f:
        return len(f.read())


def stream_64k():
    total = 0
    with open(PATH, "rb") as f:
        while block := f.read(65536):
            total += len(block)
    return total


def csv_split():
    with open(PATH, "r", encoding="utf-8") as f:
        lines = f.read().split("\n")
    total = 0
    for line in lines:
        if line:
            fields = line.split(",")
            if len(fields) != 3:
                raise ValueError("CSV fixture field count")
            total += int(fields[0]) + int(fields[2])
    return total


def csv_module():
    total = 0
    with open(PATH, "r", encoding="utf-8", newline="") as f:
        for fields in csv.reader(f):
            total += int(fields[0]) + int(fields[2])
    return total


def pandas_csv():
    import pandas as pd
    data = pd.read_csv(PATH, header=None)
    return int((data.iloc[:, 0] + data.iloc[:, 2]).sum())


def polars_csv():
    import polars as pl
    data = pl.read_csv(PATH, has_header=False)
    return int(data.select((pl.col('column_1') + pl.col('column_3')).sum()).item())


def jsonl_parse():
    with open(PATH, "r", encoding="utf-8") as f:
        lines = f.read().split("\n")
    total = 0
    for line in lines:
        if line:
            entry = json.loads(line)
            total += entry["id"] + entry["n"]
    return total


FUNCTIONS = {name: value for name, value in globals().copy().items()
             if name in ("read_bytes", "write_bytes", "readinto_64k", "mmap_read", "read_text",
                         "seek_sample", "filter_sum", "numpy_filter_sum",
                         "bytes_sum", "numpy_sum", "stream_64k", "csv_split",
                         "csv_module", "pandas_csv", "polars_csv", "jsonl_parse",
                         "list_append", "map_lookup", "function_calls", "string_concat",
                         "alloc_objects", "exceptions", "recursion", "sort_numbers",
                         "linear_search", "set_dedup")}
assert 1 <= REPEATS <= 1000000
fn = FUNCTIONS[CASE]
assert fn() == EXPECTED, f"preflight failed: {CASE}"
start = time.perf_counter_ns()
checksum = 0
for _ in range(REPEATS):
    result = fn()
    if result != EXPECTED:
        raise AssertionError((CASE, result, EXPECTED))
    checksum += result
elapsed_ms = (time.perf_counter_ns() - start) / 1e6
print(json.dumps(dict(case=CASE, iterations=REPEATS, unit=EXPECTED,
                      checksum=checksum, elapsed_ms=elapsed_ms)))
