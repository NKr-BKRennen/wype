#!/bin/bash
# BKR_NWIPE Build-Skript
# Baut nwipe und installiert es nach /usr/local/bin.

set -e

echo "=== BKR_NWIPE Build ==="
echo ""

# Ins Skript-Verzeichnis wechseln
cd "$(dirname "$0")"

# autogen.sh nur ausführen wenn configure nicht existiert oder configure.ac neuer ist
if [ ! -f configure ] || [ configure.ac -nt configure ]; then
    echo ">>> autogen.sh ..."
    ./autogen.sh
fi

# configure nur ausführen wenn Makefile nicht existiert oder configure neuer ist
if [ ! -f Makefile ] || [ configure -nt Makefile ]; then
    echo ">>> configure ..."
    ./configure
fi

echo ">>> make ..."
make -j$(nproc)

echo ">>> make install ..."
sudo make install

echo ""
echo "=== Installation abgeschlossen ==="
echo "Starten mit: sudo nwipe"
