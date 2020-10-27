
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: compare.c,v 1.2 2004/04/29 22:44:50 kohler Exp $
 *
 *    Revision history:
 *     $Log: compare.c,v $
 *     Revision 1.2  2004/04/29 22:44:50  kohler
 *     Pick_ew now produces new TYPE_PICK_SCNL and TYPE_CODA_SCNL messages.
 *     The station list file now contains SCNLs, rather than SCNs.
 *     Input waveform messages may be of either TYPE_TRACEBUF or TYPE_TRACEBUF2.
 *     If the input waveform message is of TYPE_TRACEBUF (without a location code),
 *     the location code is assumed to be "--".  WMK 4/29/04
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

#include <string.h>
#include <earthworm.h>
#include <transport.h>
#include "pick_ew.h"


     /*************************************************************
      *                       CompareSCNL()                       *
      *                                                           *
      *  This function is passed to qsort() and bsearch().        *
      *  We use qsort() to sort the station list by SCNL numbers, *
      *  and we use bsearch to look up an SCNL in the list.       *
      *************************************************************/

int CompareSCNL( const void *s1, const void *s2 )
{
   int rc;
   STATION *t1 = (STATION *) s1;
   STATION *t2 = (STATION *) s2;

   rc = strcmp( t1->sta, t2->sta );
   if ( rc != 0 ) return rc;
   rc = strcmp( t1->chan, t2->chan );
   if ( rc != 0 ) return rc;
   rc = strcmp( t1->net,  t2->net );
   if ( rc != 0 ) return rc;
   rc = strcmp( t1->loc,  t2->loc );
   return rc;
}
