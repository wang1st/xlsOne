#!/usr/bin/env bash
set -eu
test_dir=/tmp/goatcounter-probe
rm -rf -- "$test_dir"
mkdir -p "$test_dir"
/tmp/goatcounter-v2.7.0 db create site \
  -db="sqlite+$test_dir/test.sqlite3" \
  -createdb=true \
  -vhost z-pulse.cn \
  -user.email probe@example.invalid \
  -user.password 'probe-only-not-a-real-secret'
/tmp/goatcounter-v2.7.0 db show site \
  -db="sqlite+$test_dir/test.sqlite3" \
  -find z-pulse.cn -format csv
rm -rf -- "$test_dir"
