#!/bin/bash
# Drives the crosspoint-simulator through an overview of inx-t5s3 at the device's
# native 540x960 portrait logical resolution (960x540 simulated panel, 2x Retina
# screenshots). Timeline/coordinates below match the T5S3 layout exactly.
#
# Tour: home -> library -> home -> open book -> book menu -> close -> quick menu
#       -> close -> back home -> quotes browser
#
# Output: /tmp/simshots/01_home.bmp ... 06_quotes.bmp (1080x1920 px each).
# Feed them to scripts/make_overview_gif.py to build docs/overview.gif.
#
# Requires: a built simulator env (.pio/build/simulator/program) and the sample
# SD card at <repo>/fs_ (books + highlights so the banner/recents are populated).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
FS="$REPO/fs_"
BUILD="$REPO/.pio/build/simulator"
cd "$BUILD" || exit 1
rm -f "$FS/.system/state.bin"
rm -f "$FS/.system/settings.bin"
rm -rf /tmp/simshots && mkdir -p /tmp/simshots
CROSSPOINT_SIM_SD="$FS" CROSSPOINT_SIM_INPUT_SCRIPT="\
2500:TAP:162,930;\
4500:TAP:54,930;\
6500:TAP:270,380;\
11500:SWIPE:270,936,270,600,300;\
14000:BACK:80;\
15500:SWIPE:270,24,270,360,400;\
17500:TAP:22,840;\
19000:BACK:80;\
20500:TAP:270,110;\
24000:QUIT" \
CROSSPOINT_SIM_SCREENSHOTS="\
2200:/tmp/simshots/01_home.bmp;\
4000:/tmp/simshots/02_library.bmp;\
10500:/tmp/simshots/03_reader.bmp;\
12000:/tmp/simshots/04_bookmenu.bmp;\
17000:/tmp/simshots/05_quickmenu.bmp;\
23500:/tmp/simshots/06_quotes.bmp" \
./program > /tmp/sim_tour.log 2>&1
echo "exit=$?"
grep -E "\[SIM\] Activity" /tmp/sim_tour.log
ls -la /tmp/simshots/
