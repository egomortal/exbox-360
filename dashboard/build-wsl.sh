#!/bin/bash
# Build Ex-Box 360 with the Xbox controller map, Guide and glossy prompts.
# Run inside WSL Ubuntu-24.04 (build-exbox-xboxmap.cmd does this). No console actions.
# Log: outputs/exbox-xboxmap-build.log   Status: outputs/exbox-xboxmap-build-status.txt
set -euo pipefail
cd "$(dirname "$0")"
OUT="${OUT:-$PWD/build/logs}"; mkdir -p "$OUT"; STATUS="$OUT/exbox-xboxmap-build-status.txt"
exec > >(tee "$OUT/exbox-xboxmap-build.log") 2>&1
echo "RUNNING $(date -Is)" > "$STATUS"
trap 'echo "FAIL $(date -Is)" > "$STATUS"' ERR

SDK=${SDK:-/opt/ps5-payload-sdk}
if [ ! -x "$SDK/bin/prospero-clang++" ]; then
    echo "PS5 payload SDK not found at $SDK"; echo "FAIL no-sdk" > "$STATUS"; exit 1
fi

# Keep the build that is on the console now, once, before replacing it.
if [ -f build/eboot.elf ] && [ ! -f build/eboot.pre-xboxmap.elf ]; then
    cp build/eboot.elf build/eboot.pre-xboxmap.elf
    echo "Saved the previous ELF as build/eboot.pre-xboxmap.elf"
fi

if command -v g++ >/dev/null; then
    make test
else
    echo "g++ not installed in WSL: skipping host unit tests (sudo apt install g++ to run them)"
fi

make -B build/eboot.elf SDK="$SDK"
sha256sum build/eboot.elf | tee "$OUT/exbox-xboxmap-eboot.sha256"
echo "OK $(date -Is) $(sha256sum build/eboot.elf | cut -d' ' -f1)" > "$STATUS"
echo
echo "Built nxe-ps5/bigapp/build/eboot.elf. Nothing was sent to the PS5."
echo "To install later, with Omni tests finished and Ex-Box closed:"
echo "  python3 deploy.py   then   python3 activate.py"
