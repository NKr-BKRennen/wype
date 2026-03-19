#!/bin/bash
# Wype Setup Script
# Configures auto-login and/or autostart for wype on Debian systems.

set -e

OVERRIDE_DIR="/etc/systemd/system/getty@tty1.service.d"
OVERRIDE_FILE="$OVERRIDE_DIR/override.conf"
PROFILE_FILE="/root/.bash_profile"
MARKER="# WYPE Autostart"

# --- Helper functions ---

check_root() {
    if [ "$(id -u)" -ne 0 ]; then
        echo "Error: This script must be run as root (sudo ./setup.sh)"
        exit 1
    fi
}

is_autologin_enabled() {
    [ -f "$OVERRIDE_FILE" ] && grep -q "autologin" "$OVERRIDE_FILE" 2>/dev/null
}

is_autostart_enabled() {
    [ -f "$PROFILE_FILE" ] && grep -q "$MARKER" "$PROFILE_FILE" 2>/dev/null
}

status_text() {
    if $1; then echo "[ENABLED]"; else echo "[DISABLED]"; fi
}

enable_autologin() {
    if is_autologin_enabled; then
        echo "Auto-login is already enabled."
        return
    fi
    mkdir -p "$OVERRIDE_DIR"
    cat > "$OVERRIDE_FILE" << 'EOF'
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin root --noclear %I $TERM
EOF
    systemctl daemon-reload
    echo "Auto-login for root on tty1 enabled."
}

disable_autologin() {
    if ! is_autologin_enabled; then
        echo "Auto-login is already disabled."
        return
    fi
    rm -f "$OVERRIDE_FILE"
    rmdir "$OVERRIDE_DIR" 2>/dev/null || true
    systemctl daemon-reload
    echo "Auto-login disabled."
}

enable_autostart() {
    if is_autostart_enabled; then
        echo "Autostart is already enabled."
        return
    fi
    cat >> "$PROFILE_FILE" << PROFILE

$MARKER
if [[ "\$(tty)" == "/dev/tty1" ]]; then
    wype
fi
PROFILE
    echo "Wype autostart on tty1 enabled."
}

disable_autostart() {
    if ! is_autostart_enabled; then
        echo "Autostart is already disabled."
        return
    fi
    # Remove the autostart block from .bash_profile
    sed -i "/$MARKER/,/^fi$/d" "$PROFILE_FILE"
    # Clean up trailing empty lines
    sed -i -e :a -e '/^\n*$/{$d;N;ba' -e '}' "$PROFILE_FILE"
    echo "Wype autostart disabled."
}

# --- Menu ---

show_menu() {
    while true; do
        AL=$(is_autologin_enabled && echo true || echo false)
        AS=$(is_autostart_enabled && echo true || echo false)

        echo ""
        echo "=== Wype Setup ==="
        echo ""
        echo "  Current status:"
        echo "    Auto-login (root on tty1):  $(status_text $AL)"
        echo "    Autostart (wype on tty1):   $(status_text $AS)"
        echo ""
        echo "  1) Enable auto-login + autostart (recommended)"
        echo "  2) Enable auto-login only"
        echo "  3) Enable autostart only"
        echo "  4) Disable auto-login"
        echo "  5) Disable autostart"
        echo "  6) Disable both"
        echo "  q) Quit"
        echo ""
        read -p "  Choice: " choice

        case "$choice" in
            1) enable_autologin; enable_autostart ;;
            2) enable_autologin ;;
            3) enable_autostart ;;
            4) disable_autologin ;;
            5) disable_autostart ;;
            6) disable_autologin; disable_autostart ;;
            q|Q) echo ""; exit 0 ;;
            *) echo "  Invalid choice." ;;
        esac
    done
}

# --- Main ---

check_root
show_menu
