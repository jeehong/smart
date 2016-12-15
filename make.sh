#!/bin/sh
# make distclean
aclocal
autoconf
autoheader
automake --add-missing
