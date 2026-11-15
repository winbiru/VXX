"""Compare cold source execution using two packaged JIT binaries and one runner."""
import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / 'scripts/quality/run_native_regression.py'
DEFAULT_CASES = (
    'anonymous_functions', 'kiem_tra_boolean', 'kiem_tra_closure_capture',
    'kiem_tra_de_quy', 'kiem_tra_khi', 'kiem_tra_toan_tu_bit',
    'kiem_tra_json_escape_span',
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--case', action='append', dest='cases')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--logs', type=Path, default=ROOT / 'build-pure-vpp/jit-cold-benchmark')
    args = parser.parse_args()
    if args.repetitions < 1 or args.timeout < 1:
        parser.error('--repetitions and --timeout must be positive')
    cases = tuple(dict.fromkeys(args.cases or DEFAULT_CASES))
    binaries = {mode: getattr(args, mode).resolve(strict=True) for mode in ('baseline', 'candidate')}
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('VPP_')}
    env.update(VPP_HOME=str(ROOT), VPP_NATIVE_NO_CACHE='1')
    args.logs.mkdir(parents=True, exist_ok=True)
    logs = Path(tempfile.mkdtemp(prefix='comparison-', dir=args.logs.resolve()))
    samples = {mode: {case: [] for case in cases} for mode in binaries}
    print(f'Evidence: {logs}', flush=True)
    for repetition in range(args.repetitions):
        order = ('baseline', 'candidate') if repetition % 2 == 0 else ('candidate', 'baseline')
        for mode in order:
            destination = logs / f'{repetition + 1}-{mode}'
            command = [sys.executable, str(RUNNER), '--suite', 'full', '--backend', 'jit',
                       '--jit', str(binaries[mode]), '--jobs', '1', '--timeout', str(args.timeout),
                       '--logs', str(destination)]
            for case in cases:
                command.extend(('--case', case))
            print(f'{mode}, repetition {repetition + 1}', flush=True)
            subprocess.run(command, cwd=ROOT, env=env, check=True)
            report, = destination.glob('run-*/results.json')
            results = {entry['id']: entry for entry in json.loads(report.read_text(encoding='utf-8'))}
            if set(results) != set(cases):
                raise RuntimeError(f'{report}: unexpected cases')
            for case in cases:
                entry = results[case]
                run = entry.get('stages', {}).get('run')
                if entry['status'] != 'PASS' or run is None or run <= 0:
                    raise RuntimeError(f'{report}: {case} did not complete a timed JIT run')
                samples[mode][case].append({'total': entry['seconds'], 'run': run})
    summary = {}
    print('case | baseline run | candidate run | baseline total | candidate total')
    for case in cases:
        medians = {mode: {stage: statistics.median(s[stage] for s in samples[mode][case])
                          for stage in ('run', 'total')} for mode in binaries}
        summary[case] = medians
        print(f"{case} | {medians['baseline']['run']:.3f}s | {medians['candidate']['run']:.3f}s | "
              f"{medians['baseline']['total']:.3f}s | {medians['candidate']['total']:.3f}s")
    (logs / 'comparison.json').write_text(json.dumps({'binaries': {k: str(v) for k, v in binaries.items()},
                                                     'samples': samples, 'medians': summary}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
