#!/bin/bash
# WYPE Update Script
# Pulls the latest version from main and builds/installs wype.

set -e

cd "$(dirname "$0")"

echo "=== WYPE Update ==="
echo ""

echo ">>> git pull origin main ..."
git pull origin main

echo ""

# Force reconfigure so that new/removed libraries are detected
rm -f Makefile

# Run build.sh (asks at the end whether to start wype)
./build.sh
