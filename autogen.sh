#!/bin/sh

LIBTOOLIZE=libtoolize
if ! libtoolize --help >/dev/null 2>&1 && glibtoolize --help >/dev/null 2>&1
then
  LIBTOOLIZE=glibtoolize
fi
export LIBTOOLIZE

autoreconf -fi -I m4
