#!/usr/bin/env bash
set -u

/usr/local/bin/goatcounter help 2>&1 || true
printf '\n--- db help ---\n'
/usr/local/bin/goatcounter db -help 2>&1 || true
printf '\n--- serve help backup lines ---\n'
/usr/local/bin/goatcounter serve -help 2>&1 | grep -i -C 2 -E 'backup|database|sqlite' || true
printf '\n--- sqlite versions ---\n'
sqlite3 --version || true
python - <<'PY'
import sqlite3
print('python sqlite', sqlite3.sqlite_version)
PY
