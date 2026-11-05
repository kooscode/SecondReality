#!/bin/bash
# Build the Xbox ISO. Output: build/xbox/SecondReality.iso
# ./build.sh OG1993=1 uses the 1993 release's lens picture (third_party/FC-1993/README.md).
set -e
cd "$(dirname "$0")"
source "$(pwd)/tools/nxdk-env.sh"
B="$(pwd)/build/xbox"
mkdir -p "$B/bin"
MKDATA=
for a in "$@"; do [ "$a" = OG1993=1 ] && MKDATA=--og1993; done
python3 -I -B tools/mkdata.py $MKDATA
rsync -a --delete --filter='P *.obj' --filter='P *.d' src/ "$B/src/"
cp Makefile.xbox "$B/Makefile"
rsync -a --checksum --delete build/data/ "$B/bin/data/"  # by content: OG1993 swaps a same-size file
rm -f "$B/SecondReality.iso"
# make does not track compiler flags: rebuild everything when the options change
if [ "$(cat "$B/.buildopts" 2>/dev/null)" != "opts:$*" ]; then
    find "$B/src" -name '*.obj' -delete
    rm -f "$B/bin/default.xbe" "$B/main.exe"
    echo "opts:$*" > "$B/.buildopts"
fi
make -C "$B" -j16 "$@"
ls -la "$B/SecondReality.iso"
