#!/usr/bin/env bash
set -u
command -v sqlite3 || true
command -v runuser || true
command -v openssl || true
systemctl --version | head -n 1
/tmp/goatcounter-v2.7.0 help serve | sed -n '/-base-path/,+8p'
