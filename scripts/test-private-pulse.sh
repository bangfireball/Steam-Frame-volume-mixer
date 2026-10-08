#!/bin/sh
# Run inside the build container as tester. No host audio sockets are mounted.
set -eu
build=${1:-out/linux-x64}
run() {
    if [ "${MIXER_QEMU:-0}" = 1 ]; then
        timeout -k 2s 30s qemu-aarch64 -L / -E LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu:/usr/aarch64-linux-gnu/lib "$@"
    else
        timeout -k 2s 30s "$@"
    fi
}
runtime=$(mktemp -d /tmp/mixer-test-pulse.XXXXXX)
chmod 700 "$runtime"
export XDG_RUNTIME_DIR="$runtime"
export PULSE_SERVER="unix:$runtime/mixer-test-pulse.sock"
pid=
cleanup() {
    if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; fi
    rm -rf "$runtime"
}
trap cleanup EXIT HUP INT TERM
pulseaudio -n --daemonize=no --use-pid-file=no --exit-idle-time=-1 \
    --load="module-null-sink sink_name=mixer_test" \
    --load="module-null-sink sink_name=mixer_test_alt" \
    --load="module-native-protocol-unix socket=$runtime/mixer-test-pulse.sock auth-anonymous=1" \
    >"$runtime/server.log" 2>&1 &
pid=$!
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    [ ! -S "$runtime/mixer-test-pulse.sock" ] || break
    sleep .2
done
# Explicitly choose the first private null sink; never touch a host output.
pactl set-default-sink mixer_test
run "$build/pulse_tests"
run "$build/frame-volume-mixer" --list
PULSE_SERVER="unix:$runtime/missing.sock" run "$build/frame-volume-mixer" --list && exit 1 || code=$?
[ "$code" -eq 2 ]
printf 'PASS unavailable server exits truthfully\n'
PULSE_SERVER="unix:$runtime/missing.sock" run "$build/pulse_tests" --unavailable-reset
