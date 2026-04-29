#!/bin/sh

gdb -batch -ex run -ex 'bt' .build/smtp-server
