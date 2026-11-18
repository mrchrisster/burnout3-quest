#!/usr/bin/env bash
# texrun.sh <name> [KEY=VALUE ...] -- one Quest run of Takedawn with the given
# RECOMP_* overrides in env.txt: restart, settle, measure, VR screenshot.
# Results: _shots/<name>.jpg and a line in _shots/results.tsv.
set -uo pipefail
NAME="$1"; shift
P=io.github.xenophobentx.takedawn
F=/sdcard/Android/data/$P/files
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$HERE/_shots"
SETTLE="${SETTLE:-45}"
mkdir -p "$OUT"
TMP="$(mktemp)"
for kv in "$@"; do echo "$kv"; done > "$TMP"
adb push "$TMP" "$F/env.txt" >/dev/null
rm -f "$TMP"
adb shell am force-stop $P
adb shell am broadcast -a com.oculus.vrpowermanager.prox_close >/dev/null
adb logcat -c
adb shell am start -n $P/.StartActivity >/dev/null
sleep "$SETTLE"
LOG="$OUT/$NAME.log"
adb logcat -d > "$LOG"
VR="$(grep ' VrApi ' "$LOG" | tail -10)"
gpu=$(echo "$VR" | sed -n 's/.*GPU%=\([0-9.]*\).*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.2f", s/n}')
app=$(echo "$VR" | sed -n 's/.*App=\([0-9.]*\)ms.*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.2f", s/n}')
fps=$(echo "$VR" | sed -n 's/.*FPS=\([0-9]*\)\/.*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.0f", s/n}')
flips=$(grep 'PACING\] counters' "$LOG" | tail -1 | sed -n 's/.*flips \([0-9]*\) (\([0-9.]*\)\/s).*/\2/p')
crash=$(grep -c 'Fatal signal' "$LOG")
adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null 2>&1
sleep 4
SHOT=$(adb shell ls -t /sdcard/Oculus/Screenshots | grep "^$P" | head -1 | tr -d '\r')
[ -n "$SHOT" ] && adb pull "/sdcard/Oculus/Screenshots/$SHOT" "$OUT/$NAME.jpg" >/dev/null
[ -f "$OUT/results.tsv" ] || printf 'run\tsettings\tgame_fps\txr_fps\tgpu_util\tapp_gpu_ms\tcrashes\n' > "$OUT/results.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$NAME" "$*" "${flips:-?}" "${fps:-?}" "${gpu:-?}" "${app:-?}" "$crash" | tee -a "$OUT/results.tsv"
