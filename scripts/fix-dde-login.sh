#!/bin/bash
set -e

echo "=== xlsOne DDE login fix script ==="
echo "This script fixes DDE desktop login failure caused by xlsone 1.0.0 deb."
echo "Root cause: /usr/bin/qt.conf overrides system Qt plugin path,"
echo "           breaking dde-desktop's xcb platform plugin loading."
echo ""

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: This script must be run as root (sudo)."
    exit 1
fi

FIXED=0

# 1. Remove/backup the problematic qt.conf from /usr/bin/
if [ -f /usr/bin/qt.conf ]; then
    echo "[1/4] Backing up /usr/bin/qt.conf -> /usr/bin/qt.conf.bak"
    mv /usr/bin/qt.conf /usr/bin/qt.conf.bak
    FIXED=1
else
    echo "[1/4] /usr/bin/qt.conf not found — already fixed or not installed."
fi

# 2. Install wrapper script
echo "[2/4] Installing /usr/bin/xlsone.sh wrapper..."
cat > /usr/bin/xlsone.sh << 'XEOF'
#!/bin/bash
LIBDIR="/usr/lib/xlsone"
PLUGINDIR="${LIBDIR}/plugins"

if [ -d "$LIBDIR" ]; then
    export LD_LIBRARY_PATH="${LIBDIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
fi
if [ -d "$PLUGINDIR" ]; then
    export QT_PLUGIN_PATH="${PLUGINDIR}"
fi

exec /usr/bin/xlsOneQt "$@"
XEOF
chmod +x /usr/bin/xlsone.sh

# 3. Update symlink
echo "[3/4] Updating /usr/bin/xlsone symlink -> /usr/bin/xlsone.sh"
ln -sf /usr/bin/xlsone.sh /usr/bin/xlsone

# 4. Update desktop entry
if [ -f /usr/share/applications/xlsone.desktop ]; then
    echo "[4/4] Patching /usr/share/applications/xlsone.desktop Exec line..."
    sed -i 's|^Exec=.*|Exec=/usr/bin/xlsone.sh %F|' /usr/share/applications/xlsone.desktop
else
    echo "[4/4] Desktop entry not found — skipping."
fi

echo ""
if [ "$FIXED" -eq 1 ]; then
    echo "Fix applied. Please log out and log back in."
    echo "DDE desktop should now work normally."
else
    echo "No changes needed — system was already fixed."
fi
