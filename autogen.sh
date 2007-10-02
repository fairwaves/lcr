#!/bin/sh


# call autoconf, autoheader and automake
echo autoheader..
autoheader || exit $?
echo aclocal..
aclocal || exit $?
echo autoconf..
autoconf || exit $?
echo automake..
automake || exit $?

#./configure
#make distcheck
