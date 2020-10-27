
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: restart.c,v 1.2 2007/12/16 19:18:43 paulf Exp $
 *
 *    Revision history:
 *     $Log: restart.c,v $
 *     Revision 1.2  2007/12/16 19:18:43  paulf
 *     fixed an improper use of long for 4 byte sample data, some OS have long as 8bytes.
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <earthworm.h>
#include <trace_buf.h>
#include <transport.h>
#include "pick_ew.h"

/* Function prototypes
   *******************/
void InitVar( STATION * );


 /*********************************************************************
  *                             Restart()                             *
  *                                                                   *
  *         Check for breaks in the time sequence of messages.        *
  *                                                                   *
  *  Returns 1 if the picker is in restart mode                       *
  *          0 if the picker is not in restart mode                   *
  *********************************************************************/

int Restart( STATION *Sta, GPARM *Gparm, int nsamp, int GapSize )
{

/* If GapSize > Gparm->MaxGap, begin a restart sequence.
   Initialize internal variables.
   Save the number of samples processed in restart mode.
   ****************************************************/
   if ( GapSize > Gparm->MaxGap )
   {
      InitVar( Sta );
      Sta->ns_restart = nsamp;
      return 1;
   }
   else
   {

/* The restart sequence is over
   ****************************/
      if ( Sta->ns_restart >= Gparm->RestartLength )
      {
         return 0;
      }

/* The restart sequence is still in progress
   *****************************************/
      else
      {
         Sta->ns_restart += nsamp;
         return 1;
      }
   }
}


   /*************************************************************
    *                       Interpolate()                       *
    *                                                           *
    *  Interpolate samples and insert them at the beginning of  *
    *  the waveform.                                            *
    *************************************************************/

void Interpolate( STATION *Sta, char *WaveBuf, int GapSize )
{
   int      i;
   int      j = 0;
   int      nInterp = GapSize - 1;
   TRACE_HEADER *WaveHead  = (TRACE_HEADER *) WaveBuf;
   int         *WaveLong  = (int *) (WaveBuf + sizeof(TRACE_HEADER));
   double   SampleInterval = 1. / WaveHead->samprate;
   double   delta = (double)(WaveLong[0] - Sta->enddata) / GapSize;

/* logit( "et", "Found %4d sample gap. Interpolating station ", GapSize );
   logit( "e", "%-5s%-2s%-3s\n", Sta->name, Sta->net, Sta->chan ); */

   for ( i = WaveHead->nsamp - 1; i >= 0; i-- )
      WaveLong[i + nInterp] = WaveLong[i];

   for ( i = 0; i < nInterp; i++ )
      WaveLong[i] = (int) (Sta->enddata + (++j * delta) + 0.5);

   WaveHead->nsamp += nInterp;
   WaveHead->starttime = Sta->endtime + SampleInterval;
}
