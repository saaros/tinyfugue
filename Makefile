########################################################################
#
#  TinyFugue - programmable mud client
#  Copyright (C) 1994 - 1999 Ken Keys
#
#  TinyFugue (aka "tf") is protected under the terms of the GNU
#  General Public Licence.  See the file "COPYING" for details.
#
########################################################################

# Note: the space on the end of the next line is intentional, so it will
# still work in unix for idiots who got ^M on the end of every line.
default:  all 

all:
	@echo :
	@echo : Use one of the following commands to build TinyFugue:
	@echo :
	@echo : sh unixmake [options]
	@echo : os2make [options]
	@echo :

install files tf clean uninstall: all

# The next line is a hack to get around a bug in BSD/386 make.
make:
