#!/bin/sh

swaks --to recipient@example.com \
  --from sender@example.com \
  --server 192.168.100.12:5255 \
  --header "Subject: Test from swaks" \
  --body "Test message body" \
  --no-suppress-data