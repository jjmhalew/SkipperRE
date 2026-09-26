#!/bin/sh
# Regressietest (headless): alle hotspots in Skipper's kamer + een paar spelscenario's.
# Draait met een eigen, lege APPDATA zodat echte spelposities niet geraakt worden.
#   tools/regress.sh [exe]      (standaard out/skipper.exe)
# Elke regel: naam, OK/FOUT, film + frame aan het eind. Screenshots in out/regress/.
EXE=${1:-out/skipper.exe}
DATA=${DATA:-extract}   # DATA=- : niets opgeven (bijv. een exe met ingepakte spelbestanden)
OUTD=out/regress
rm -rf "$OUTD"; mkdir -p "$OUTD/appdata"
APPDATA=$(cygpath -w "$PWD/$OUTD/appdata" 2>/dev/null || echo "$PWD/$OUTD/appdata")
export APPDATA
FAIL=0

# check naam verwacht-film-of-global  args...
check() {
    name=$1; want=$2; shift 2
    out=$(timeout 300 "$EXE" $DATA "$@" --shot "${SHOT:-450}" "$OUTD/$name.bmp" --dump 2>&1)
    last=$(printf '%s\n' "$out" | grep '^frame ' | tail -1)
    if printf '%s\n' "$out" | grep -q -- "$want"; then echo "OK    $name: $last"
    else echo "FOUT  $name: verwacht '$want' | $last"; FAIL=1; fi
}

INTRO="--click 320 240 170"
# kamer C5: hotspot-midden (x + w/2, y + h/2) -> verwachte film
while read -r name x y w h want; do
    check "$name" "van $want" $INTRO --click $((x + w / 2)) $((y + h / 2)) 300 &
done <<EOF
Spell 422 89 34 53 MMB10
ABC 383 53 35 90 MMB09
Music 357 61 24 83 MMB05
Clock 195 105 39 43 MMB06
Balloon 236 61 28 90 MMB08
Paint 268 45 12 106 MMB11
Memory 283 53 9 92 MMB07
Calc 296 64 10 81 MMB02
Count 312 53 14 90 MMB04
Animals 333 73 21 73 MMB01
Hide 553 335 54 58 MMB03
EOF
wait

# spelscenario's
SHOT=560 check mailbox "Mailbox.*open\|OBD5B0[2-5]" $INTRO --click 320 393 300 --click 398 268 420 &
SHOT=520 check coin "gPoints = 3" $INTRO --click 320 393 300 --drag 100 350 150 450 420 &
SHOT=900 check boss "gCurrentLoc = #E6" $INTRO --click 320 393 300 --click 320 393 400 --click 615 200 470 &
# E2: emmer onder de voorgrond (kale sprite) naar de zak slepen
E2="--click 320 393 300 --click 615 200 400 --click 400 180 500 --click 200 125 600 --click 170 160 700"
SHOT=950 check bucket "ch 35. 1:49 BucketS" $INTRO $E2 --drag 552 385 60 450 800 &
SHOT=360 check savedlg "van Magnus" $INTRO --key 97 0 300 &
wait
exit $FAIL
