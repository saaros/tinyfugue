### rules.mak - Makefile rules common to all systems.
# This file should be portable to all systems, so it can be included in or
# concatenated with all system-specific makefiles.
# Note the regexp.$O rule must be defined in a system-specific file.

# Predefined variables:
#   SYS          - required system type (unix, os2)
#   O            - required object suffix (e.g., "o" or "obj")
#   BUILDERS     - optional list of system-specific dependancies

# tf.h, and everything it #includes
TF_H = tf.h malloc.h globals.h enumlist.h hooklist.h varlist.h

command.$O: command.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	commands.h command.h world.h socket.h output.h macro.h keyboard.h \
	expand.h search.h signals.h variable.h $(BUILDERS)
dmalloc.$O: dmalloc.c config.h port.h $(TF_H) $(BUILDERS)
dstring.$O: dstring.c config.h port.h malloc.h dstring.h signals.h $(BUILDERS)
expand.$O: expand.c config.h port.h dstring.h $(TF_H) util.h tfio.h macro.h \
	signals.h socket.h search.h output.h keyboard.h expand.h commands.h \
	command.h variable.h world.h funclist.h $(BUILDERS)
help.$O: help.c config.h port.h dstring.h $(TF_H) tfio.h commands.h $(BUILDERS)
history.$O: history.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	history.h socket.h world.h output.h macro.h commands.h search.h \
	$(BUILDERS)
keyboard.$O: keyboard.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	macro.h keyboard.h output.h history.h expand.h search.h commands.h \
	keylist.h $(BUILDERS)
macro.$O: macro.c config.h port.h dstring.h $(TF_H) util.h tfio.h search.h \
	world.h macro.h keyboard.h expand.h socket.h commands.h command.h \
	hooklist.h $(BUILDERS)
main.$O: main.c config.h port.h dstring.h $(TF_H) util.h tfio.h history.h \
	world.h socket.h macro.h output.h signals.h command.h keyboard.h \
	variable.h tty.h $(BUILDERS)
makehelp.$O: makehelp.c config.h port.h $(BUILDERS)
malloc.$O: malloc.c config.h port.h signals.h malloc.h $(BUILDERS)
output.$O: output.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	socket.h output.h macro.h search.h tty.h variable.h $(BUILDERS)
process.$O: process.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	history.h world.h process.h socket.h expand.h commands.h $(BUILDERS)
search.$O: search.c config.h port.h malloc.h search.h $(BUILDERS)
signals.$O: signals.c config.h port.h dstring.h $(TF_H) util.h tfio.h world.h \
	process.h tty.h output.h signals.h variable.h $(BUILDERS)
socket.$O: socket.c config.h port.h dstring.h $(TF_H) util.h tfio.h tfselect.h \
	history.h world.h socket.h output.h process.h macro.h keyboard.h \
	commands.h command.h signals.h search.h $(BUILDERS)
tfio.$O: tfio.c config.h port.h dstring.h $(TF_H) util.h tfio.h tfselect.h \
	output.h macro.h history.h search.h signals.h variable.h $(BUILDERS)
tty.$O: tty.c config.h port.h $(TF_H) dstring.h util.h tty.h output.h macro.h \
	search.h variable.h $(BUILDERS)
util.$O: util.c config.h port.h dstring.h $(TF_H) util.h tfio.h output.h tty.h \
	signals.h variable.h $(BUILDERS)
variable.$O: variable.c config.h port.h dstring.h $(TF_H) util.h tfio.h \
	output.h socket.h search.h commands.h process.h expand.h variable.h \
	enumlist.h varlist.h $(BUILDERS)
world.$O: world.c config.h port.h dstring.h $(TF_H) util.h tfio.h history.h \
	world.h process.h macro.h commands.h socket.h $(BUILDERS)

