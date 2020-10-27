
#
#   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
#   CHECKED IT OUT USING THE COMMAND CHECKOUT.
#
#    $Id: makefile.sol,v 1.4 2004/04/29 22:44:51 kohler Exp $
#
#    Revision history:
#     $Log: makefile.sol,v $
#     Revision 1.4  2004/04/29 22:44:51  kohler
#     Pick_ew now produces new TYPE_PICK_SCNL and TYPE_CODA_SCNL messages.
#     The station list file now contains SCNLs, rather than SCNs.
#     Input waveform messages may be of either TYPE_TRACEBUF or TYPE_TRACEBUF2.
#     If the input waveform message is of TYPE_TRACEBUF (without a location code),
#     the location code is assumed to be "--".  WMK 4/29/04
#
#     Revision 1.3  2002/11/03 19:31:53  lombard
#     Added CFLAGS definition
#
#     Revision 1.2  2000/08/08 18:11:30  lucky
#     Added lint directive
#
#     Revision 1.1  2000/02/14 19:06:49  lucky
#     Initial revision
#
#
#


CFLAGS = ${GLOBALFLAGS}

B = $(EW_HOME)/$(EW_VERSION)/bin
L = $(EW_HOME)/$(EW_VERSION)/lib


O = pick_ew.o pick_ra.o restart.o config.o stalist.o compare.o \
    index.o sample.o report.o initvar.o scan.o sign.o \
    $L/kom.o $L/getutil.o $L/time_ew.o $L/chron3.o $L/logit.o \
    $L/transport.o $L/sleep_ew.o $L/swap.o $L/trheadconv.o

pick_ew: $O
	cc -o $B/pick_ew $O -lm -lposix4

lint:
	lint pick_ew.c pick_ra.c restart.c config.c stalist.c \
			compare.c index.c sample.c report.c initvar.c \
			scan.c sign.c $(GLOBALFLAGS)



# Clean-up rules
clean:
	rm -f a.out core *.o *.obj *% *~

clean_bin:
	rm -f $B/pick_ew*
