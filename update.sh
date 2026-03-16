#!/bin/bash
# BKR_NWIPE Update-Skript
# Pullt die neueste Version von main und baut/installiert nwipe.

set -e

cd "$(dirname "$0")"

echo "=== BKR_NWIPE Update ==="
echo ""

echo ">>> git pull origin main ..."
git pull origin main

echo ""

# build.sh aufrufen (fragt am Ende ob nwipe gestartet werden soll)
./build.sh
