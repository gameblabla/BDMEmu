#!/bin/sh
set -eu
if [ $# -lt 1 ]; then
  echo "usage: $0 /path/to/rom-directory-or-bin [steps]" >&2
  exit 2
fi
STEPS=${2:-200000}
if [ -d "$1" ]; then
  for f in "$1"/*.bin; do
    [ -e "$f" ] || continue
    case "$f" in
      *"[G."*) ;;
      *) echo "skip data/design cart: $f"; continue ;;
    esac
    echo "== $f =="
    ./bdm_headless --cart "$f" --steps "$STEPS"
  done
else
  ./bdm_headless --cart "$1" --steps "$STEPS"
fi
