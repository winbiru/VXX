#!/usr/bin/env python3
import json
import os
import time

TEXTS = ["Xin chào Việt Nam! " * (i + 1) for i in range(16)]
DOCUMENTS = ['{"name":"V++ Việt","quantity":' + str(i)
             + ',"active":true,"values":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]}'
             for i in range(16)]


def arithmetic(i):
    return (1000 + i * 7 + 20 + i) - (i % 7) * (i % 11)


def branch(i):
    x = i * 4093 + 7
    for _ in range(128 + i):
        x += 251
        if x >= 65521:
            x -= 65521
    return x


def fibonacci(i):
    a, b = 0, 1
    for _ in range(20 + i):
        next_value = a + b
        a = b
        b = next_value
    return a


def collection(i):
    values = []
    for j in range(128):
        values.append((j * 17 + i) % 1000)
    result = 0
    for j in range(128):
        value = values[j]
        if value >= 500:
            result += value
    return result


def text(i):
    return len(TEXTS[i].replace("Việt", "V++"))


def parse_json(i):
    parsed = json.loads(DOCUMENTS[i])
    result = parsed["quantity"] + len(parsed["name"])
    values = parsed["values"]
    for j in range(16):
        result += values[j]
    return result


if __name__ == "__main__":
    work = {"arithmetic": arithmetic, "branch": branch, "fibonacci": fibonacci,
            "list": collection, "text": text, "json": parse_json}[os.environ["VPP_BENCH_CASE"]]
    iterations = int(os.environ["VPP_BENCH_ITERS"])
    for index in range(16):
        print(work(index))
    previous = None
    for _ in range(4):
        checksum = 0.0
        start = time.perf_counter_ns()
        for i in range(iterations):
            checksum += work(i % 16)
        elapsed_ms = (time.perf_counter_ns() - start) / 1e6
        assert previous is None or previous == checksum
        previous = checksum
    print(elapsed_ms)
    print(int(checksum))
    print(iterations)
