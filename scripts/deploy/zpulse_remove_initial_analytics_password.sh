#!/usr/bin/env bash
set -eu
test -f /var/lib/goatcounter/goatcounter.sqlite3
rm -f -- /root/.goatcounter-initial-password
echo 'initial remote password file removed'
