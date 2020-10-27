
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: sample.c,v 1.2 2007/12/16 19:18:43 paulf Exp $
 *
 *    Revision history:
 *     $Log: sample.c,v $
 *     Revision 1.2  2007/12/16 19:18:43  paulf
 *     fixed an improper use of long for 4 byte sample data, some OS have long as 8bytes.
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "pick_ew.h"


  /******************************************************************
   *                             Sample()                           *
   *                    Process one digital sample.                 *
   *                                                                *
   *  Arguments:                                                    *
   *    LongSample  One waveform data sample                        *
   *    Sta         Station list                                    *
   *                                                                *
   *  The constant SmallDouble is used to avoid underflow in the    *
   *  calculation of rdat.                                          *
   *                                                                *
   *  Modifies: rold, rdat, old_sample, esta, elta, eref, eabs      *
   ******************************************************************/

void Sample( int LongSample, STATION *Sta )
{
   PARM *Parm = &Sta->Parm;
   static double rdif;                    /* First difference */
   static double edat;                    /* Characteristic function */
   const  double small_double = 1.0e-10;

/* Store present value of filtered data */
   Sta->rold = Sta->rdat;

/* Compute new value of filtered data */
   Sta->rdat = (Sta->rdat * Parm->RawDataFilt) +
               (double) (LongSample - Sta->old_sample) + small_double;

/* Compute 1'st difference of filtered data */
   rdif = Sta->rdat - Sta->rold;

/* Store integer data value */
   Sta->old_sample = LongSample;

/* Compute characteristic function */
   edat = (Sta->rdat * Sta->rdat) + (Parm->CharFuncFilt * rdif * rdif);

/* Compute esta, the short-term average of edat */
   Sta->esta += Parm->StaFilt * (edat - Sta->esta);

/* Compute elta, the long-term average of edat */
   Sta->elta += Parm->LtaFilt * (edat - Sta->elta);

/* Compute eref, the reference level for event checking */
   Sta->eref = Sta->elta * Parm->EventThresh;

/* Compute eabs, the running mean absolute value of rdat */
   Sta->eabs = (Parm->RmavFilt * Sta->eabs) +
                (( 1.0 - Parm->RmavFilt ) * fabs( Sta->rdat ));
}
