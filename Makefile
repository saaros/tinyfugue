# $Id: Makefile,v 32101.0 1993/12/20 07:12:09 hawkeye Stab $
########################################################################
#  TinyFugue - programmable mud client
#  Copyright (C) 1993  Ken Keys
#
#  TinyFugue (aka "tf") is protected under the terms of the GNU
#  General Public Licence.  See the file "COPYING" for details.
########################################################################

#
# Top level Makefile.
#

TFVER = 32b1
SHELL = /bin/sh
MAKE  = make

install: _install

all files: _files

reconfigure:
	rm src/Makefile src/config.h
	$(MAKE) install

_install: src/Makefile src/config.h _log
	cd src; $(MAKE) install 2>&1 | tee -a ../Build.log; cat exitmsg

_files: src/Makefile src/config.h _log
	cd src; $(MAKE) files 2>&1 | tee -a ../Build.log; cat exitmsg

src/Makefile src/config.h: Makefile Config src/autoconfig src/mf.tail
	@rm -f Build.log
	@cd src; ./autoconfig $(TFVER) 2>&1 3>mf.vars 4>config.h
	@echo
	@echo '#### If any assumptions were wrong, stop now and edit Config.'
	@echo

_log:
	@{ cat src/mf.vars; echo; cat src/config.h; echo; } > Build.log

clean:
	rm -f core* *.log
	cd src; rm -f *.o Makefile core* exitmsg config.h
	cd src; rm -f *.log libtest.* test.c a.out libc.cont mf.vars
	cd src/regexp; make clean

distclean:  clean
	cd src; rm -f tf tf.connect makehelp

spotless cleanest:  distclean
	cd src; rm -f tf.1.catman tf.help.index

dist: distclean
	@echo 'Press return to archive tf.$(TFVER).'; read foo
	wd=`pwd`; [ "`basename $$wd`" = "work" ]
	cd src; $(MAKE) -f mf.tail dist
	rm -rf ../tf.$(TFVER)
	mkdir ../tf.$(TFVER)
	mkdir ../tf.$(TFVER)/src
	mkdir ../tf.$(TFVER)/src/regexp
	mkdir ../tf.$(TFVER)/tf.lib
	-for f in *; do [ ! -d "$$f" ] && cp $$f ../tf.$(TFVER); done
	-for f in src/*; do [ ! -d "$$f" ] && cp $$f ../tf.$(TFVER)/src; done
	-for f in src/regexp/*; do cp $$f ../tf.$(TFVER)/src/regexp; done
	-for f in tf.lib/*; do \
	    [ ! -d "$$f" ] && cp $$f ../tf.$(TFVER)/tf.lib; \
	done
	chmod ugo+r ../tf.$(TFVER)
	chmod ugo+r ../tf.$(TFVER)/*
	chmod ugo+r ../tf.$(TFVER)/src/*
	chmod ugo+r ../tf.$(TFVER)/tf.lib/*
	rm -f ../tf.$(TFVER)/src/dmalloc.c
	cd ..; tar -cf tf.$(TFVER).tar tf.$(TFVER); gzip tf.$(TFVER).tar
	cd ..; chmod ugo+r tf.$(TFVER).tar.gz

