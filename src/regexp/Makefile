# This is a customized Makefile, containing only enough to compile regexp.o
# for use with TinyFugue.  The original Makefile has been renamed original.mak.
#
# CC and O should be defined on the make command line.
# CFLAGS may optionally be defined on the make command line.


# -DCHARBITS=0377	compilers without unsigned char (ok on those with it)
# -I.			regexp.h from current directory, not /usr/include

regexp.$O:	regexp.c regexp.h regmagic.h ../config.h
	$(CC) -DCHARBITS=0377 -I. $(CFLAGS) -c regexp.c

# The next line is a hack to get around a bug in BSD/386 make.
make:

