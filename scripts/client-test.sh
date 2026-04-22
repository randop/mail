#!/bin/sh

swaks --to recipient@maildomain.ngo \
  --from sender@maildomain.ngo \
  --server 127.0.0.1:5255 \
  --header "Subject: Test Email" \
  --body "Test message body" \
  --no-suppress-data
