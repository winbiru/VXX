#!/usr/bin/env bash
# Fail closed when a supposedly freestanding executable acquires a loader,
# dynamic dependency, or unresolved external symbol.
set -euo pipefail
if [[ $# -ne 1 || ! -f "$1" ]]; then
    echo 'Usage: audit_linux_static_elf.sh <linux-executable>' >&2
    exit 2
fi
ELF="$1"
for utility in readelf nm; do
    if ! command -v "$utility" >/dev/null 2>&1; then
        echo "Missing ELF inspection tool: $utility" >&2
        exit 2
    fi
done
header="$(readelf -h "$ELF")" || exit 1
if ! grep -Eq 'Class:[[:space:]]+ELF64' <<< "$header" ||
   ! grep -Eq 'Type:[[:space:]]+EXEC ' <<< "$header" ||
   ! grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' <<< "$header"; then
    echo 'Expected a static ELF64 x86-64 executable (ET_EXEC).' >&2
    exit 1
fi
program_headers="$(readelf -l "$ELF")" || exit 1
stack_flags="$(readelf -W -l "$ELF" | awk '$1 == "GNU_STACK" {print $(NF-1)}')"
if [[ -z "$stack_flags" || "$stack_flags" == *E* ]]; then
    echo "Freestanding ELF has an executable or missing PT_GNU_STACK: ${stack_flags:-missing}." >&2
    exit 1
fi
if grep -Eq '(^|[[:space:]])INTERP([[:space:]]|$)' <<< "$program_headers"; then
    echo 'Unexpected ELF interpreter (PT_INTERP).' >&2
    exit 1
fi
dynamic="$(readelf -d "$ELF")" || exit 1
if grep -Eq '\((NEEDED|RPATH|RUNPATH)\)' <<< "$dynamic"; then
    echo 'Unexpected dynamic dependency (DT_NEEDED/RPATH/RUNPATH).' >&2
    exit 1
fi
if grep -Eq '(^|[[:space:]])DYNAMIC([[:space:]]|$)' <<< "$program_headers"; then
    echo 'Freestanding ELF has a PT_DYNAMIC segment.' >&2
    exit 1
fi
undefined="$(nm -u "$ELF")" || exit 1
if [[ -n "$undefined" ]]; then
    echo "Unresolved ELF symbols: $undefined" >&2
    exit 1
fi
echo 'Static ELF audit: ELF64 x86-64, non-executable GNU_STACK, no PT_INTERP/PT_DYNAMIC, no DT_NEEDED, no unresolved symbols.'
