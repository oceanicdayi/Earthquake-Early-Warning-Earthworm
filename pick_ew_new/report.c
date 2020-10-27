
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: report.c,v 1.6 2008/03/28 18:20:31 paulf Exp $
 *
 *    Revision history:
 *     $Log: report.c,v $
 *     Revision 1.6  2008/03/28 18:20:31  paulf
 *     added in PickIndexDir option to specify where pick indexes get stuffed
 *
 *     Revision 1.5  2007/04/27 17:38:54  paulf
 *     patched report to not send ANY codas if NoCoda param is set
 *
 *     Revision 1.4  2006/11/17 18:24:02  dietz
 *     Changed index file name from pick_ew.ndx to pick_ew_MMM.ndx where
 *     MMM is the moduleid of pick_ew. This will allow multiple instances
 *     of pick_ew to run without competing for the same index file.
 *
 *     Revision 1.3  2004/05/28 18:46:06  kohler
 *     Pick times are now rounded to nearest hundred'th of a second, as in
 *     Earthworm v6.2.  The milliseconds are hard wired to 0 in the output
 *     pick value.  The new pick_scnl message type requires a milliseconds value.
 *
 *     Revision 1.2  2004/04/29 22:44:52  kohler
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

  /**********************************************************************
   *                              report.c                              *
   *                                                                    *
   *                 Pick and coda buffering functions                  *
   *                                                                    *
   *  This file contains functions ReportPick(), ReportCoda().          *
   **********************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <earthworm.h>
#include <chron3.h>
#include <transport.h>
#include "pick_ew.h"

static char line[LINELEN];   /* Buffer to hold picks and codas */

/* Function prototypes
   *******************/
int GetPickIndex( unsigned char modid , char * dir);  /* function in index.c */


     /**************************************************************
      *               ReportPick() - Report one pick.              *
      **************************************************************/

void ReportPick( PICK *Pick, STATION *Sta, PEEW *eew_Sta, GPARM *Gparm, EWH *Ewh )
{
   MSG_LOGO    logo;      /* Logo of message to send to output ring */
   int         lineLen;
   struct Greg g;
   int         tsec, thun;
   int         PickIndex;
   char        firstMotion = Pick->FirstMotion;

/* Get the pick index and the SNC (station, network, component).
   They will be reported later, with the coda.
   ************************************************************/
   PickIndex = GetPickIndex( Gparm->MyModId , Gparm->PickIndexDir);


/* Convert julian seconds to date and time.
   Round pick time to nearest hundred'th
   of a second.
   ***************************************/
                  printf("report:  Pick->time %f \n ", Pick->time);
   
   datime( Pick->time, &g );
   tsec = (int)floor( (double) g.second );
   thun = (int)((100.*(g.second - tsec)) + 0.5);
   if ( thun == 100 )
      tsec++, thun = 0;

/* First motions aren't allowed to be blank
   ****************************************/
   if ( firstMotion == ' ' ) firstMotion = '?';

/* Convert pick to space-delimited text string.
   This is a bit risky, since the buffer could overflow.
   Milliseconds are always set to zero.
   ****************************************************/
   sprintf( line,              "%d",  (int) Ewh->TypePickScnl );
   sprintf( line+strlen(line), " %d", (int) Gparm->MyModId );
   sprintf( line+strlen(line), " %d", (int) Ewh->MyInstId );
   sprintf( line+strlen(line), " %d", PickIndex );
   sprintf( line+strlen(line), " %s", Sta->sta );
   sprintf( line+strlen(line), ".%s", Sta->chan );
   sprintf( line+strlen(line), ".%s", Sta->net );
   sprintf( line+strlen(line), ".%s", Sta->loc );

   sprintf( line+strlen(line), " %c%d", firstMotion, Pick->weight );

   sprintf( line+strlen(line), " %4d%02d%02d%02d%02d%02d.%02d0",
            g.year, g.month, g.day, g.hour, g.minute, tsec, thun );

   sprintf( line+strlen(line), " %d", (int)(Pick->xpk[0] + 0.5) );
   sprintf( line+strlen(line), " %d", (int)(Pick->xpk[1] + 0.5) );
   sprintf( line+strlen(line), " %d", (int)(Pick->xpk[2] + 0.5) );
   
   sprintf( line+strlen(line), " --- %f %f %f %f ", eew_Sta->a, eew_Sta->v, eew_Sta->d, eew_Sta->tc );
   
   strcat( line, "\n" );
   lineLen = strlen(line);



/* Print the pick
   **************/
 #if defined(_OS2) || defined(_WINNT)
   printf( "%s", line );
#endif

/* Send the pick to the output ring
   ********************************/
   logo.type   = Ewh->TypePickScnl;
   logo.mod    = Gparm->MyModId;
   logo.instid = Ewh->MyInstId;

//printf("line: %s \n", line);

   if ( tport_putmsg( &Gparm->OutRegion, &logo, lineLen, line ) != PUT_OK )
      logit( "et", "pick_ew: Error sending pick to output ring.\n" );
   return;
}



void ReportPick_eew( PICK *Pick, STATION *Sta, PEEW *eew_Sta, GPARM *Gparm, EWH *Ewh )
{
   MSG_LOGO    logo;      /* Logo of message to send to output ring */
   int         lineLen;
   struct Greg g;
   int         tsec, thun;
   int         PickIndex;
   char        firstMotion = Pick->FirstMotion;

/* Get the pick index and the SNC (station, network, component).
   They will be reported later, with the coda.
   ************************************************************/
   PickIndex = GetPickIndex( Gparm->MyModId , Gparm->PickIndexDir);


/* Convert julian seconds to date and time.
   Round pick time to nearest hundred'th
   of a second.
   ***************************************/
   datime( Pick->time, &g );
   tsec = (int)floor( (double) g.second );
   thun = (int)((100.*(g.second - tsec)) + 0.5);
   if ( thun == 100 )
      tsec++, thun = 0;

/* First motions aren't allowed to be blank
   ****************************************/
   if ( firstMotion == ' ' ) firstMotion = '?';

/* Convert pick to space-delimited text string.
   This is a bit risky, since the buffer could overflow.
   Milliseconds are always set to zero.
   ****************************************************/
   sprintf( line,              "%d",  (int) 114 );
   sprintf( line+strlen(line), " %d", (int) Gparm->MyModId );
   sprintf( line+strlen(line), " %d", (int) Ewh->MyInstId );
   sprintf( line+strlen(line), " %d", PickIndex );
   sprintf( line+strlen(line), " %s", Sta->sta );
   sprintf( line+strlen(line), ".%s", Sta->chan );
   sprintf( line+strlen(line), ".%s", Sta->net );
   sprintf( line+strlen(line), ".%s", Sta->loc );

   sprintf( line+strlen(line), " %c%d", firstMotion, Pick->weight );

   sprintf( line+strlen(line), " %4d%02d%02d%02d%02d%02d.%02d0",
            g.year, g.month, g.day, g.hour, g.minute, tsec, thun );

   
   sprintf( line+strlen(line), " %f %f %f %f ", eew_Sta->pa, eew_Sta->pv, eew_Sta->pd, eew_Sta->tc );
   
   strcat( line, "\n" );
   lineLen = strlen(line);

/* Print the pick
   **************/
 #if defined(_OS2) || defined(_WINNT)
   printf( "%s", line );
#endif

/* Send the pick to the output ring
   ********************************/
   logo.type   = Ewh->TypePickScnl;
   logo.mod    = Gparm->MyModId;
   logo.instid = Ewh->MyInstId;

//printf("line: %s \n", line);

   if ( tport_putmsg( &Gparm->OutRegion, &logo, lineLen, line ) != PUT_OK )
      logit( "et", "pick_ew: Error sending pick to output ring.\n" );
   return;
}


void ReportPick_eew2( STATION *Sta, PEEW *eew_Sta, GPARM *Gparm, EWH *Ewh, char *outmsg_P )
{

   MSG_LOGO    logo;      /* Logo of message to send to output ring */
   int         lineLen;


   lineLen = strlen(outmsg_P);



/* Print the pick
   **************/
 #if defined(_OS2) || defined(_WINNT)
   // printf( "===outmsg_P: %s\n", outmsg_P );
#endif

/* Send the pick to the output ring
   ********************************/
   logo.type   = Gparm->TypeEEW;
   logo.mod    = Gparm->MyModId;
   logo.instid = Ewh->MyInstId;

//printf("line: %s \n", line);

   if ( tport_putmsg( &Gparm->OutRegion, &logo, lineLen, outmsg_P ) != PUT_OK )
      printf( "Error sending: %s\n",  outmsg_P);
   return;


}


  /**************************************************************
   *               ReportCoda() - Report one coda.              *
   **************************************************************/


