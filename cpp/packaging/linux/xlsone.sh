#!/bin/sh
# xlsOne launcher — sets up Qt plugin path before running the binary.
#
# Library resolution is handled by RPATH ($ORIGIN/../lib/xlsone) baked into the
# binary. When the bundled Qt libraries are compatible with this system, they
# are loaded from /usr/lib/xlsone/. If they are not compatible (e.g. glibc
# version mismatch on older systems like UOS 20), the postinst script moves
# them aside and the system Qt5 libraries are used instead.
#
# LD_LIBRARY_PATH is intentionally NOT set here to avoid contaminating
# child processes (e.g. browser launched via QDesktopServices::openUrl).

BUNDLEDIR="/usr/lib/xlsone"

# Force XCB platform plugin to avoid Wayland loading issues on systems where
# the bundled Qt5 wayland plugin is present but its system dependencies
# (libwayland-client etc.) are missing or incompatible. Some desktop
# environments set QT_QPA_PLATFORM=wayland, which breaks this bundled Qt5.
export QT_QPA_PLATFORM=xcb

# Set plugin path: use bundled plugins if available, otherwise Qt will
# auto-detect the system plugin directory (e.g. /usr/lib/.../qt5/plugins/).
if [ -d "$BUNDLEDIR/plugins" ]; then
    export QT_PLUGIN_PATH="$BUNDLEDIR/plugins"
elif [ -f "$BUNDLEDIR/.use-system-qt" ]; then
    # System Qt fallback — bundled plugins were disabled; Qt auto-detects.
    :
fi

exec /usr/bin/xlsOneQt "$@"
