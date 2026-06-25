#!/bin/sh
# xlsOne launcher — sets up bundled Qt plugin path before running the binary.
# Library resolution is handled by RPATH ($ORIGIN/../lib/xlsone) baked into the
# binary; LD_LIBRARY_PATH is intentionally NOT set here to avoid contaminating
# child processes (e.g. browser launched via QDesktopServices::openUrl).
PLUGINDIR="/usr/lib/xlsone/plugins"

if [ -d "$PLUGINDIR" ]; then
    export QT_PLUGIN_PATH="${PLUGINDIR}"
fi

exec /usr/bin/xlsOneQt "$@"
