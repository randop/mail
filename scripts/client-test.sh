#!/bin/sh

swaks --to recipient@maildomain.ngo \
  --from sender@maildomain.ngo \
  --server 127.0.0.1:5255 \
  --header "Subject: Test Email" \
  --body "Test message body" \
  --no-suppress-data

swaks \
  --to recipient1@maildomain.ngo,recipient2@maildomain.ngo,cc1@maildomain.ngo,cc2@maildomain.ngo,bcc1@maildomain.ngo,bcc2@maildomain.ngo \
  --from sender1@maildomain.ngo \
  --header "To: recipient1@maildomain.ngo, recipient2@maildomain.ngo" \
  --header "Cc: cc1@maildomain.ngo, cc2@maildomain.ngo" \
  --header "From: Sender1 <sender1@maildomain.ngo>, Sender2 <sender2@maildomain.ngo>" \
  --header "Subject: Test Email" \
  --body "Test message body" \
  --server 127.0.0.1:5255 \
  --no-suppress-data

# === Trying 127.0.0.1:5255...
# === Connected to 127.0.0.1.
# <-  220 mail Service ready
# -> EHLO ubuntu22jammy
# <-  250-maildomain.ngo Nice to meet you, [192.168.100.8]
# <-  250-8BITMIME
# <-  250-SMTPUTF8
# <-  250 SIZE 524288
# -> MAIL FROM:<sender1@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<recipient1@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<recipient2@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<cc1@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<cc2@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<bcc1@maildomain.ngo>
# <-  250 OK
# -> RCPT TO:<bcc2@maildomain.ngo>
# <-  250 OK
# -> DATA
# <-  354 Start mail input; end with <CRLF>.<CRLF>
# -> Date: Wed, 22 Apr 2026 20:11:55 +0000
# -> To: recipient1@maildomain.ngo, recipient2@maildomain.ngo
# -> From: Sender1 <sender1@maildomain.ngo>, Sender2 <sender2@maildomain.ngo>
# -> Subject: Test Email
# -> Message-Id: <20260422201155.000365@ubuntu22jammy>
# -> X-Mailer: swaks v20201014.0 jetmore.org/john/code/swaks/
# -> Cc: cc1@maildomain.ngo, cc2@maildomain.ngo
# ->
# -> Test message body
# ->
# ->
# -> .
# <-  250 OK queued
# -> QUIT
# <-  221 Bye
# === Connection closed with remote host.

swaks \
  --to recipient@maildomain.ngo \
  --from sender@maildomain.ngo \
  --server 127.0.0.1:5255 \
  --header "Subject: Test Email" \
  --body "Test message body" \
  --attach /path/to/your/file.pdf \
  --no-suppress-data
