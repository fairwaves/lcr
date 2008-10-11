#!/bin/sh

aclocal
autoheader
automake --foreign --copy --add-missing
autoconf
