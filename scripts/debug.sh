#!/bin/sh

gdb -batch -ex run -ex 'bt' --args .build/smtp-server --datadir data --logdir logs --certificate certificate.crt --privatekey private.key
