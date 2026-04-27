#!/bin/sh

gdb -batch -ex run -ex 'bt' --args .build/smtp-server --proxy-support