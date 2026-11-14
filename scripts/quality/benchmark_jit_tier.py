"""Compare dynamic guest execution with and without hot native recompilation."""
import argparse
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'benchmark/native_jit_hot.vi'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('vpp_home', type=Path)
    parser.add_argument('--repetitions', type=int, default=3)
    args = parser.parse_args()
    if args.repetitions < 1:
        parser.error('--repetitions must be positive')
    executable = args.executable.resolve(strict=True)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('VPP_')}
    env.update(VPP_HOME=str(args.vpp_home.resolve(strict=True)),
               VPP_AOT_ASSEMBLER='/assembler-must-not-run',
               VPP_AOT_LINKER='/linker-must-not-run', VPP_JIT_PROFILE='1')
    samples = {'baseline': [], 'tiered': []}
    for i in range(args.repetitions):
        # Alternate order to reduce systematic thermal and scheduling bias.
        order = ('baseline', 'tiered') if i % 2 == 0 else ('tiered', 'baseline')
        for mode in order:
            run_env = dict(env, VPP_JIT_TIER='0' if mode == 'baseline' else '1')
            result = subprocess.run([str(executable), '--dynamic', str(SOURCE)],
                                    env=run_env, capture_output=True, timeout=600,
                                    check=True, encoding='utf-8')
            lines = result.stdout.splitlines()
            if len(lines) != 6 or lines[-1] != '500064':
                raise RuntimeError(f'{mode}: unexpected output {result.stdout!r}')
            times = [float(line) for line in lines[:5]]
            if any(not math.isfinite(value) or value <= 0 for value in times):
                raise RuntimeError(f'{mode}: invalid sample {times}')
            if mode == 'baseline' and result.stderr:
                raise RuntimeError(f'baseline emitted diagnostics: {result.stderr!r}')
            if mode == 'tiered' and not re.search(
                    r'^VPP_JIT_PROFILE fn=\d+ name=hot_add calls=500064 i32=500064 compiled=1 hits=500033$',
                    result.stderr, re.MULTILINE):
                raise RuntimeError(f'tiered leaf was not exercised: {result.stderr!r}')
            samples[mode].extend(times)
            print(f'{mode} run {i + 1}: {times} ms', flush=True)
    baseline = statistics.median(samples['baseline'])
    tiered = statistics.median(samples['tiered'])
    print(f'baseline median: {baseline:.3f} ms')
    print(f'tiered median:   {tiered:.3f} ms')
    print(f'baseline/tiered: {baseline / tiered:.3f}x')


if __name__ == '__main__':
    main()
