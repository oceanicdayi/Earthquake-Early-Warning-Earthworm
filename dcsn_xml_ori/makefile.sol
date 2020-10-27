
#
#   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
#   CHECKED IT OUT USING THE COMMAND CHECKOUT.
#
#    $Id: makefile.sol,v 1.5 2007/02/20 13:29:20 paulf Exp $
#
#    Revision history:
#     $Log: makefile.sol,v $
#     Revision 1.5  2007/02/20 13:29:20  paulf
#     added lockfile testing to template module
#
#     Revision 1.4  2001/02/01 01:37:10  dietz
#     *** empty log message ***
#
#     Revision 1.3  2000/08/08 17:54:53  lucky
#     Added lint directive
#
#     Revision 1.2  2000/02/14 21:48:14  lucky
#     *** empty log message ***
#
#     Revision 1.1  2000/02/14 19:43:11  lucky
#     Initial revision
#
#
#

CFLAGS=${GLOBALFLAGS} -g

B = $(EW_HOME)/$(EW_VERSION)/bin
L = $(EW_HOME)/$(EW_VERSION)/lib

TEMPLATE = template.o $L/logit.o $L/kom.o $L/getutil.o $L/sleep_ew.o \
           $L/time_ew.o $L/transport.o $L/lockfile_ew.o $L/lockfile.o

template: $(TEMPLATE)
	cc -o $B/template $(TEMPLATE)  -lm -lposix4

lint:
	lint template.c ${GLOBALFLAGS}


# Clean-up rules
clean:
	rm -f a.out core *.o *.obj *% *~

clean_bin:
	rm -f $B/template*
