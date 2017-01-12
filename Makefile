
# Copyright (c) 2013,2016-2017 Greg Becker.  All rights reserved.

KMOD    = pmt

SRCS    = pmt.c tests.c

.include <bsd.kmod.mk>

#CFLAGS	+= -DINVARIANTS
#CFLAGS	+= -DDIAGNOSTIC
#CFLAGS	+= -O0 -g

CLEANFILES += cscope.* TAGS
CSCOPE_DIRS ?= . ${VPATH} /usr/src/lib /usr/src/sys

.PHONY:	cscope etags

cscope: cscope.out

cscope.out: cscope.files ${HDR} ${SRC}
	cscope -bukq

cscope.files:
	find ${CSCOPE_DIRS} -name \*.[chsSyl] -o -name \*.cpp > $@

etags: TAGS

TAGS: cscope.files
	cat cscope.files | xargs etags -a --members --output=$@

objdump:
	objdump -sdwx -Mintel --prefix-addresses pmt.o > pmt.objdump
	objdump -sdwx -Mintel --prefix-addresses tests.o > tests.objdump
