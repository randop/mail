#!/bin/sh
set -eu
export LD_LIBRARY_PATH=/server/lib
./smtp-server \
  --port 2525 \
  --proxy-support \
  --datadir /server/emails \
  --logdir /server/logs \
  --certificate=/server/certificate.crt \
  --privatekey=/server/private.key 2>&1 |
  stdbuf -oL sed 's/\x1b[@A-Z\\\]^_]\|\x1b\[[0-9:;<=>?]*[-!"#$%&'"'"'()*+,.\/]*[][\\@A-Z^_`a-z{|}~]//g' |
  tee -a service.log
