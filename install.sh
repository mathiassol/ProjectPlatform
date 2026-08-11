#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

echo
echo " ProjectPlatform"
echo " ==============="
echo

if [[ ! -x ./pp && ! -f ./pp ]]; then
  echo "error: pp binary not found next to this installer."
  echo "Extract the full release zip before running install.sh"
  exit 1
fi

chmod +x ./pp
./pp install

echo
echo " Tip: open a NEW terminal (or source ~/.zprofile) and run:"
echo "   pp list"
echo
echo " Optional shell integration (off by default):"
echo "   pp hook install"
echo "   source ~/.zshrc"
echo
