# $Id: GNUmakefile 221 2013-07-06 12:22:09Z greg $

.PHONY:	all cscope etags tags ${SUBDIRS} ${MAKECMDGOALS}

CSCOPE_DIRS	?= . ${VPATH} /usr/src/lib /usr/src/sys


cscope: cscope.out

cscope.out: cscope.files ${HDR} ${SRC} GNUmakefile
	cscope -bukq

cscope.files: GNUmakefile
	find ${CSCOPE_DIRS} -name \*.[chsSyl] -o -name \*.cpp > $@

tags etags: TAGS

TAGS: cscope.files
	cat cscope.files | xargs etags -a --members --output=$@

all ${MAKECMDGOALS}:
	make ${MAKECMDGOALS}

