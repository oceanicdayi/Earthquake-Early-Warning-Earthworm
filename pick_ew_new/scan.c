
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: scan.c,v 1.3 2007/12/16 19:18:43 paulf Exp $
 *
 *    Revision history:
 *     $Log: scan.c,v $
 *     Revision 1.3  2007/12/16 19:18:43  paulf
 *     fixed an improper use of long for 4 byte sample data, some OS have long as 8bytes.
 *
 *     Revision 1.2  2002/03/05 17:18:53  dietz
 *     minor debug logging changes
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

         /**********************************************
          *                   scan.c                   *
          **********************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <chron3.h>
#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include "pick_ew.h"
#include "sample.h"

int sniff_eew( int LongSample, PEEW *eew_Sta);

     /*****************************************************
      *                   ScanForEvent()                  *
      *                                                   *
      *              Search for a pick event.             *
      *                                                   *
      *  Returns 1 if event found; otherwise 0.           *
      *****************************************************/

int ScanForEvent( PEEW *eew_Sta, STATION *Sta, GPARM *Gparm, char *WaveBuf, int *sample_index )
{
   PICK *Pick = &Sta->Pick;        /* Pointer to pick variables */
   PARM *Parm = &Sta->Parm;        /* Pointer to config parameters */

   TRACE_HEADER *WaveHead = (TRACE_HEADER *) WaveBuf;
   int         *WaveLong = (int *) (WaveBuf + sizeof(TRACE_HEADER));
   int count_ratio;
   
/* Set pick and coda calculations to inactive mode
   ***********************************************/
   Pick->status = 0;
   //printf("------ WaveHead->nsamp: %d , *sample_index : %d \n", WaveHead->nsamp, *sample_index);
/* Loop through all samples in the message
   ***************************************/
	count_ratio = 0;	    
   while ( ++(*sample_index) < WaveHead->nsamp )
   {
      int   old_sample;                  /* Previous sample */
      int   new_sample;                  /* Current sample */
      //double old_eref;                    /* Old value of eref */

      new_sample = WaveLong[*sample_index];
      old_sample = Sta->old_sample;
      //old_eref   = Sta->eref;

/* Update Sta.rold, Sta.rdat, Sta.old_sample, Sta.esta,
   Sta.elta, Sta.eref, and Sta.eabs using the current sample
   *********************************************************/
//   printf("------ s3 \n");
      Sample( new_sample, Sta );
      sniff_eew( new_sample, eew_Sta);       

//printf(" Sta->eabs : %f ,  Parm->DeadSta: %f \n",  Sta->eabs, Parm->DeadSta); 
/* Station is assumed dead when (eabs > DeadSta)
   *********************************************/
      if ( Sta->eabs > Parm->DeadSta ) continue;

/* Has the short-term average abruptly increased
   with respect to the long-term average?
   *********************************************/
//printf(" Sta->esta : %f ,  Sta->eref: %f \n",  Sta->esta, Sta->eref);   
      if ( Sta->esta > Sta->eref ) count_ratio ++;
	  if(count_ratio > 1) // the ratio at least twice larger than the threshold
      {
      //   int wi;                              /* Window index */
//printf("-s2 \n");
/* Initialize pick variables
   *************************/
         Pick->time = WaveHead->starttime + 11676096000. +
                      (double)*sample_index / WaveHead->samprate;
         eew_Sta->ptime =  WaveHead->starttime+ (double)*sample_index / WaveHead->samprate; // for eew  
	 
	 //if(!strcmp("TWL",eew_Sta->sta))
	 //printf("SCAN--- %s %s %s %s ------ \n", eew_Sta->sta, eew_Sta->chan, eew_Sta->net, eew_Sta->loc);         
                               
         if ( 0 ) {
            char datestr[20];
            date17( Pick->time, datestr );
            //logit( "e", "Pick time: %.3lf  %s\n", Pick->time, datestr );
         }


         Sta->evlen     = 0;
         Sta->isml      = 0;
         Sta->k         = 0;
         Sta->m         = 1;
         Sta->mint      = 0;
         Sta->ndrt      = 0;
         Sta->next      = 0;
         Sta->nzero     = 0;
         Sta->rlast     = Sta->rdat;
         Sta->rsrdat    = 0.;
         Sta->sarray[0] = new_sample;
         Sta->tmax      = fabs( Sta->rdat );
         Sta->xfrz      = 1.6 * Sta->eabs;

/* Compute threshold for big zero crossings
   ****************************************/
         Sta->xdot = new_sample - old_sample;
         //Sta->rbig = ( (Sta->xdot < 0) ? -Sta->xdot : Sta->xdot ) / 3.;
         //Sta->rbig = (Sta->eabs > Sta->rbig) ? Sta->eabs : Sta->rbig;


         Pick->status =  1;   /* Picks are now active */
         return 1;
      }
   }
   return 0;                          /* Message ended; event not found */
}
