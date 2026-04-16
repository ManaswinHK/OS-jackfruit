#!/usr/bin/env bash
set -euo pipefail

echo "[check] kernel: $(uname -r)"
echo "[check] distro: $(lsb_release -ds 2>/dev/null || echo unknown)"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "[warn] run this script with sudo for the most accurate checks"
fi

if [[ ! -d "/lib/modules/$(uname -r)/build" ]]; then
  echo "[fail] missing kernel headers for $(uname -r)"
  exit 1
fi

if mokutil --sb-state 2>/dev/null | grep -qi enabled; then
  echo "[warn] Secure Boot appears to be enabled"
else
  echo "[ok] Secure Boot does not appear to block module loading"
fi

echo "[ok] environment check complete"
