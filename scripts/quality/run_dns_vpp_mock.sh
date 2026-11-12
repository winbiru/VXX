#!/bin/sh
# Shell only manages lifetime; the DNS server and client are V++.
set -eu
server=$1 cli=$2 source=$3 mode=$4 queries=$5 out=$6 backend=$7
mkdir -p "$out"
printf '%s\n%s\n' "$mode" "$queries" >"$out/config"
: >"$out/server.out"
: >"$out/server.err"
PATH= "$server" <"$out/config" >"$out/server.out" 2>"$out/server.err" &
worker=$!
trap 'kill "$worker" 2>/dev/null || :; wait "$worker" 2>/dev/null || :' EXIT HUP INT TERM
attempt=0
while [ ! -s "$out/server.out" ]; do
  if ! kill -0 "$worker" 2>/dev/null; then
    code=0
    wait "$worker" || code=$?
    cat "$out/server.err" >&2
    [ "$code" -ne 0 ] || code=1
    exit "$code"
  fi
  attempt=$((attempt + 1))
  [ "$attempt" -lt 100 ] || exit 1
  sleep 0.02
done
IFS= read -r port <"$out/server.out"
case "$port" in ''|*[!0-9]*) exit 1 ;; esac
[ "$port" -gt 0 ] && [ "$port" -le 65535 ]
export VPP_DNS_MOCK_MODE="$mode" VPP_DNS_VI_PORT="$port"
export VPP_DNS_VI_SERVER=127.0.0.1 VPP_DNS_VI_SERVERS=
if [ "$mode" = 4 ]; then
  export VPP_DNS_VI_SERVERS=127.0.0.2,127.0.0.1
fi
export VPP_DNS_VI_HOSTS_FILE="$out/empty-hosts" VPP_DNS_VI_RESOLV_CONF=
export VPP_SOCKET_VI_ONLY=1
: >"$out/empty-hosts"
client_status=0
if [ "$backend" = asm ]; then
  PATH= "$cli" "$source" >"$out/client.out" 2>"$out/client.err" || client_status=$?
elif [ "$backend" = exe ]; then
  "$cli" >"$out/client.out" 2>"$out/client.err" || client_status=$?
else
  "$cli" run --compiler="$backend" "$source" >"$out/client.out" 2>"$out/client.err" || client_status=$?
fi
wait "$worker"
trap - EXIT HUP INT TERM
[ "$(tail -n 1 "$out/server.out")" = "$queries" ]
if [ "$backend" = asm ] && [ "$mode" -ge 10 ]; then
  [ "$client_status" -ne 0 ]
  grep -q 'error=16' "$out/client.err"
  printf 'VPP DNS VM REJECTED\n'
else
  [ "$client_status" -eq 0 ]
  cat "$out/client.out"
fi
