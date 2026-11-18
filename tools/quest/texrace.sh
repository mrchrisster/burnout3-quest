#!/usr/bin/env bash
# texrace.sh <name> [KEY=VALUE ...] -- a Quest run of Takedawn with these
# RECOMP_* overrides, driven by RECOMP_PAD_LIVE from the boot to the start
# line of a Single Event race (Profile 1, first event, first car), then
# measured standing still for 15 s and a VR screenshot taken.
# Results: _shots/race_<name>.jpg and a line in _shots/race_results.tsv.
set -uo pipefail
NAME="$1"; shift
P=io.github.xenophobentx.takedawn
F=/sdcard/Android/data/$P/files
SD=/storage/emulated/0/Android/data/$P/files
HERE="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$HERE/_shots"
mkdir -p "$OUT"
TMP="$(mktemp)"
{ for kv in "$@"; do echo "$kv"; done; echo "RECOMP_PAD_LIVE=$SD/pad.txt"; echo "TAKEDAWN_AUTOLOAD=0"; } > "$TMP"
adb push "$TMP" "$F/env.txt" >/dev/null
rm -f "$TMP"
adb shell "chmod 644 $F/env.txt; rm -f $F/pad.txt; touch $F/pad.txt; chmod 666 $F/pad.txt"
adb shell am force-stop $P
adb shell am broadcast -a com.oculus.vrpowermanager.prox_close >/dev/null
adb logcat -c
adb shell am start -n $P/.StartActivity >/dev/null
p() { adb shell "echo $1 >> $F/pad.txt"; sleep "$2"; }
sleep 22
p start:200 4; p start:200 4; p a:200 4; p start:200 5
p start:1200 7          # title -> Load Profile
p a:200 6               # Profile 1 -> Load successful
p a:200 7               # -> main menu
p down:150 2; p a:200 5 # Single Event
p a:200 5; p a:200 5    # event, track
p a:200 20              # car -> loading, tutorial cards
for i in 1 2 3 4 5; do p a:200 3; done
sleep 10
adb logcat -c
sleep 15
LOG="$OUT/race_$NAME.log"
adb logcat -d > "$LOG"
adb shell "cat $F/takedawn/logs/run.tail.log" > "$OUT/race_$NAME.game.log" 2>/dev/null
VR="$(grep ' VrApi ' "$LOG" | tail -10)"
gpu=$(echo "$VR" | sed -n 's/.*GPU%=\([0-9.]*\).*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.2f", s/n}')
app=$(echo "$VR" | sed -n 's/.*App=\([0-9.]*\)ms.*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.2f", s/n}')
xr=$(echo "$VR" | sed -n 's/.*FPS=\([0-9]*\)\/.*/\1/p' | awk '{s+=$1;n++} END{if(n) printf "%.0f", s/n}')
game=$(grep 'PACING\] counters' "$OUT/race_$NAME.game.log" | tail -1 | sed -n 's/.*flips [0-9]* (\([0-9.]*\)\/s).*/\1/p')
tex=$(grep 'TEXUP\] flip' "$OUT/race_$NAME.game.log" | tail -1 | sed -n 's/.*, \([0-9]*\) upscaled (\([0-9.]*\) MB).*/\1 up (\2 MB)/p')
crash=$(grep -c 'Fatal signal' "$LOG")
adb shell am startservice -n com.oculus.metacam/.capture.CaptureService -a TAKE_SCREENSHOT >/dev/null 2>&1
sleep 4
SHOT=$(adb shell ls -t /sdcard/Oculus/Screenshots | grep "^$P" | head -1 | tr -d '\r')
[ -n "$SHOT" ] && adb pull "/sdcard/Oculus/Screenshots/$SHOT" "$OUT/race_$NAME.jpg" >/dev/null
[ -f "$OUT/race_results.tsv" ] || printf 'run\tsettings\tgame_fps\txr_fps\tgpu_util\tapp_gpu_ms\ttexup\tcrashes\n' > "$OUT/race_results.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$NAME" "$*" "${game:-?}" "${xr:-?}" "${gpu:-?}" "${app:-?}" "${tex:-off}" "$crash" | tee -a "$OUT/race_results.tsv"
