#!/usr/bin/env bash
# Sync the vendored FreeInk SDK subset into components/xteink/sdk.
#   scripts/sync-sdk.sh <freeink-sdk commit sha> [path to a local freeink-sdk checkout]
set -euo pipefail
SHA=${1:?usage: sync-sdk.sh <freeink-sdk commit sha> [local checkout]}
REPO=https://github.com/Free-Ink/freeink-sdk
ROOT=$(cd "$(dirname "$0")/.." && pwd)
DST=$ROOT/components/xteink/sdk
LIBS=(display/FreeInkDisplay hardware/BoardConfig hardware/InputManager hardware/BatteryMonitor hardware/XteinkDetect hardware/FrontlightManager)

SRC=${2:-}
if [ -z "$SRC" ]; then
  SRC=$(mktemp -d); trap 'rm -rf "$SRC"' EXIT
  git init -q "$SRC"
  git -C "$SRC" fetch -q --depth 1 "$REPO" "$SHA"
  git -C "$SRC" checkout -q FETCH_HEAD
fi

rm -rf "$DST/libs"
for l in "${LIBS[@]}"; do
  mkdir -p "$DST/libs/$(dirname "$l")"
  cp -R "$SRC/libs/$l" "$DST/libs/$l"
done
cp "$SRC/LICENSE" "$SRC/NOTICE" "$DST/"
echo "$SHA" > "$DST/SDK_COMMIT"
echo "synced freeink-sdk@$SHA -> $DST"
