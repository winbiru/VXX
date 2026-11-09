#!/usr/bin/env bash
set -eu
# Each background worker owns its process group, including descendants.
set -m
source "$1/scripts/quality/native_test_workers.sh"
temporary=$(mktemp -d)
slow=""
cleanup() {
  if [ -n "$slow" ]; then vpp_test_stop_tree "$slow"; fi
  rm -rf "$temporary"
}
trap cleanup EXIT
sleep 0.1 & first=$!
sleep 0.2 & second=$!
vpp_test_wait_workers "$first" "$second"

# A later worker fails before the first worker. Waiting in launch order
# would spend 20 seconds here and leave the grandchild alive on cancellation.
bash -c 'sleep 20 & echo "$!" > "$1"; wait' _ "$temporary/child" & slow=$!
for ((attempt=0; attempt<100; attempt++)); do
  [ ! -f "$temporary/child" ] || break
  sleep 0.01
done
[ -f "$temporary/child" ]
child=$(cat "$temporary/child")
(sleep 0.2; exit 7) & failed=$!
started=$SECONDS
if vpp_test_wait_workers "$slow" "$failed"; then
  echo "worker failure was lost" >&2
  exit 1
fi
[ "$((SECONDS - started))" -lt 5 ]
for ((attempt=0; attempt<100; attempt++)); do
  if ! kill -0 "$child" 2>/dev/null; then break; fi
  sleep 0.01
done
if kill -0 "$slow" 2>/dev/null || kill -0 "$child" 2>/dev/null; then
  echo "cancelled worker left a live descendant" >&2
  exit 1
fi
slow=""
echo "Native test workers: success, prompt failure and descendant cleanup: PASS"
