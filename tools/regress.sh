#!/bin/sh
# Regressietest (headless): alle hotspots in Skipper's kamer + een paar spelscenario's.
# Draait met een eigen, lege opslagmap (APPDATA / XDG_DATA_HOME) zodat echte spelposities niet geraakt worden.
# Headless loopt de tijd virtueel, dus de uitkomst is op elke machine en elk platform gelijk.
#   tools/regress.sh [exe]      (standaard out/skipper.exe, op Linux out/skipper)
# Elke regel: naam, OK/FOUT, film + frame aan het eind. Screenshots in out/regress/.
case "$(uname -s)" in Linux*) DEF=out/skipper ;; *) DEF=out/skipper.exe ;; esac
EXE=${1:-$DEF}
DATA=${DATA:-extract}   # DATA=- : niets opgeven (bijv. een exe met ingepakte spelbestanden)
OUTD=out/regress
rm -rf "$OUTD"; mkdir -p "$OUTD/appdata"
APPDATA=$(cygpath -w "$PWD/$OUTD/appdata" 2>/dev/null || echo "$PWD/$OUTD/appdata")
XDG_DATA_HOME="$PWD/$OUTD/appdata"
export APPDATA XDG_DATA_HOME
FAIL=0

# check naam verwacht-film-of-global  args...
check() {
    name=$1; want=$2; shift 2
    out=$(timeout 300 "$EXE" $DATA "$@" --shot "${SHOT:-450}" "$OUTD/$name.bmp" --dump 2>&1)
    last=$(printf '%s\n' "$out" | grep '^frame ' | tail -1)
    if printf '%s\n' "$out" | grep -q -- "$want"; then echo "OK    $name: $last"
    else echo "FOUT  $name: verwacht '$want' | $last"; FAIL=1; fi
}

# De Deense cd van 1996 (Director 4, DATA=releases/da/files): eigen kamerindeling, geen klik voor de intro nodig
if [ -f "$DATA/MAGNUS0.DXR" ]; then
    SHOT=300 check d4start "gCurrentLoc = #C5"   # eerst alleen: haalt de hoofdfilm eenmalig uit MAGNUS.EXE
    while read -r name x y want; do
        SHOT=700 check "d4$name" "van $want" --click "$x" "$y" 300 &
    done <<EOF
Spell 439 115 MMB10
ABC 400 98 MMB09
Music 369 102 MMB05
Clock 214 126 MMB06
Balloon 250 106 MMB08
Paint 274 98 MMB11
Memory 287 99 MMB07
Calc 301 104 MMB02
Count 319 98 MMB04
Animals 343 109 MMB01
Hide 576 386 MMB03
EOF
    SHOT=700 check d4exit "gCurrentLoc = #D5" --click 240 400 300 &
    # ABC: letter A, de toets a, het goede plaatje -> video A.AVI (Cinepak), de lengte via "the duration of cast"
    SHOT=1200 check d4video "gVideoDuration = 840" --click 400 98 300 --click 57 425 500 --key 0 97 700 --click 165 215 900 &
    SHOT=800 check d4print "\[print\]" --click 274 98 300 --click 562 164 600 &
    wait
    # opslaan bij het afsluiten (Esc, Ja: MMSYS.LoadSaveGame -> positie 1) en bij de volgende start laden, in een eigen opslagmap
    (
        D="$OUTD/appsave"; mkdir -p "$D/SkipperRE"
        cp "$OUTD/appdata/SkipperRE/magnus_d4.dxr" "$D/SkipperRE/"
        APPDATA=$(cygpath -w "$PWD/$D" 2>/dev/null || echo "$PWD/$D"); XDG_DATA_HOME="$PWD/$D"; SKIPPER_SLOT=1
        export APPDATA XDG_DATA_HOME SKIPPER_SLOT
        SHOT=900 check d4save "film magnus1" --click 240 400 300 --key 53 27 500 --click 390 330 560
        SHOT=400 check d4load "gCurrentLoc = #D5"
        exit $FAIL
    ) || FAIL=1
    exit $FAIL
fi

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
# spelling (MMB10): een letter aanklikken zet hem in zijn vakje (the clickOn van een moveable sprite)
# de eerste letter van het eerste woord: P van "paard" (Nederlands), E van "elg" (Scandinavisch)
case "$DATA" in *no*) LETTER="333 183" ;; *) LETTER="277 185" ;; esac
SHOT=960 check spelling 'gDroppedSprites = \[.*"[A-Z]"' $INTRO --click 439 115 300 --click 90 450 700 --click $LETTER 920 &
wait
exit $FAIL
