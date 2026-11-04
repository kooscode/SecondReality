#!/bin/bash
# Build the Xbox ISO. Output: build/xbox/SecondReality.iso
set -e
cd "$(dirname "$0")"
source "$(pwd)/tools/nxdk-env.sh"
B="$(pwd)/build/xbox"
mkdir -p "$B/bin"
python3 -I -B tools/mkdata.py
rsync -a --delete --filter='P *.obj' --filter='P *.d' src/ "$B/src/"
cp Makefile.xbox "$B/Makefile"
rsync -a --delete build/data/ "$B/bin/data/"
rm -f "$B/SecondReality.iso"
# make does not track compiler flags: rebuild everything when the options change
if [ "$(cat "$B/.buildopts" 2>/dev/null)" != "opts:$*" ]; then
    find "$B/src" -name '*.obj' -delete
    rm -f "$B/bin/default.xbe" "$B/main.exe"
    echo "opts:$*" > "$B/.buildopts"
fi
make -C "$B" -j16 "$@"
ls -la "$B/SecondReality.iso"
