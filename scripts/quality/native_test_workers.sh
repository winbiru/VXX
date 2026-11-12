#!/usr/bin/env bash
# Shared by the integration runner and its scheduler regression. Only touch
# descendants of PIDs launched by this runner; never match command names.
vpp_test_stop_tree() {
  local pid="$1" child
  # Job control gives each background worker its own process group. Signal
  # the entire group first; process enumeration (pgrep/ps) can be unavailable
  # in sandboxes, leaving grandchildren alive after the worker exits.
  if kill -0 -- "-$pid" 2>/dev/null; then
    kill -TERM -- "-$pid" 2>/dev/null || true
    return
  fi
  # Retain descendant traversal for callers that did not create a job group.
  for child in $(pgrep -P "$pid" 2>/dev/null || true); do
    vpp_test_stop_tree "$child"
  done
  kill "$pid" 2>/dev/null || true
}

vpp_test_wait_workers() {
  [ "$#" -gt 0 ] || return 0
  local -a processes=("$@")
  local remaining="$#" index pid failure=0
  while [ "$remaining" -gt 0 ]; do
    for ((index=0; index<${#processes[@]}; index++)); do
      pid="${processes[index]}"
      [ -n "$pid" ] || continue
      if ! kill -0 "$pid" 2>/dev/null; then
        if ! wait "$pid"; then failure=1; fi
        processes[index]=""
        remaining=$((remaining - 1))
      fi
    done
    if [ "$failure" -ne 0 ]; then
      for pid in "${processes[@]}"; do
        [ -z "$pid" ] || vpp_test_stop_tree "$pid"
      done
      for pid in "${processes[@]}"; do
        [ -z "$pid" ] || wait "$pid" 2>/dev/null || true
      done
      return 1
    fi
    if [ "$remaining" -gt 0 ]; then sleep 0.1; fi
  done
}
