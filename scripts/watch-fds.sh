#!/bin/sh

set -eu

watch -n 2 '
  echo "Total FDs: $(ls /proc/$(pidof smtp-server)/fd 2>/dev/null | wc -l)";
  echo "Socket FDs: $(ls -l /proc/$(pidof smtp-server)/fd/ 2>/dev/null | grep socket | wc -l)"
'