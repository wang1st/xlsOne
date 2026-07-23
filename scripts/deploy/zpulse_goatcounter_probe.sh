#!/usr/bin/env bash
set -eu
chmod 0700 /tmp/goatcounter-v2.7.0
echo '== checksum =='
sha256sum /tmp/goatcounter-v2.7.0
echo '== version =='
/tmp/goatcounter-v2.7.0 version
echo '== serve flags =='
/tmp/goatcounter-v2.7.0 help serve | grep -E -i -C 1 'listen|tls|path|prefix|base|proxy|db|automigrate|public' || true
echo '== db create flags =='
/tmp/goatcounter-v2.7.0 help db create site | sed -n '1,180p'
