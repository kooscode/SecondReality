# Environment for building with nxdk. build.sh sources this.
# Set NXDK_DIR to your nxdk checkout, or put it in tools/nxdk-env.local.sh
# (not tracked by git). Default: an nxdk folder next to this project.
_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$_here/nxdk-env.local.sh" ] && . "$_here/nxdk-env.local.sh"
: "${NXDK_DIR:=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/nxdk}"
export NXDK_DIR
export PATH="$NXDK_DIR/bin:$PATH"
# nxdk needs clang/lld; add a versioned LLVM install if the plain names are missing
if ! command -v lld-link >/dev/null 2>&1; then
    for v in 20 19 18 17 16 15; do
        if [ -d /usr/lib/llvm-$v/bin ]; then export PATH="/usr/lib/llvm-$v/bin:$PATH"; break; fi
    done
fi
if [ ! -f "$NXDK_DIR/Makefile" ]; then
    echo "nxdk not found at $NXDK_DIR: set NXDK_DIR or create tools/nxdk-env.local.sh" >&2
    return 1 2>/dev/null || exit 1
fi
