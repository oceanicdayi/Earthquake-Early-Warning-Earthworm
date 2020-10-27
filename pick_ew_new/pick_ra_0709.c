
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: pick_ra.c,v 1.5 2007/12/16 19:18:43 paulf Exp $
 *
 *    Revision history:
 *     $Log: pick_ra.c,v $
 *     Revision 1.5  2007/12/16 19:18:43  paulf
 *     fixed an improper use of long for 4 byte sample data, some OS have long as 8bytes.
 *
 *     Revision 1.4  2007/02/26 13:59:08  paulf
 *     no coda changes (option)
 *
 *     Revision 1.3  2002/03/05 17:17:03  dietz
 *     minor debug logging changes.
 *
 *     Revision 1.2  2000/07/19 21:12:24  kohler
 *     Now calculates coda lengths correctly for non-100hz data.
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

    /******************************************************************
     *                            pick_ra.c                           *
     *                                                                *
     *              Contains PickRA(), a function to pick             *
     *                    one demultiplexed message                   *
     ******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <earthworm.h>
#include <trace_buf.h>
#include <transport.h>
#include <time_ew.h>
#include "pick_ew.h"
#include "sample.h"

/* Function prototypes
   *******************/
int    ScanForEvent( PEEW *, STATION *, GPARM *, char *, int * );      
int    EventActive( PEEW *, STATION *, char *, GPARM *, EWH *, int * );


double Sign( double, double );

void ReportPick_eew2(  STATION *Sta, PEEW *eew_Sta, GPARM *Gparm, EWH *Ewh, char *outmsg_P );
int sniff_eew( int LongSample, PEEW *eew_Sta);
void Ini_eew_para(PEEW *eew_Sta);


 /***********************************************************************
  *                              PickRA()                               *
  *                   Pick one demultiplexed message                    *
  *                                                                     *
  *  Arguments:                                                         *
  *     Sta              Pointer to station being processed             *
  *     WaveBuf          Pointer to the message buffer                  *
  *                                                                     *
  *  Picks are held until it is determined that the coda length is at   *
  *  least three seconds.  Each coda is reported as a separate message  *
  *  after it's corresponding pick is reported, even if the coda        *
  *  calculation is finished before the pick is ready to report.        *
  *  Codas are released from 3 to 144 seconds after the pick time.      *
  ***********************************************************************/

void PickRA(PEEW *eew_Sta, STATION *Sta, char *WaveBuf, GPARM *Gparm, EWH *Ewh )
{
   int  event_found;               /* 1 if an event was found */
   int  event_active;              /* 1 if an event is active */
   int  sample_index = -1;         /* Sample index */
   PICK *Pick = &Sta->Pick;        /* Pointer to pick variables */


/* A pick is active; continue it's calculation
   *******************************************/


   if ( (Pick->status > 0) )   
   {
      event_active = EventActive( eew_Sta, Sta, WaveBuf, Gparm, Ewh, &sample_index );      

      if ( event_active == 1 )           /* Event active at end of message */
         return;
   }

/* Search mode
   ***********/
   while ( 1 )
   {
      event_found = ScanForEvent( eew_Sta, Sta, Gparm, WaveBuf, &sample_index );      
      if ( event_found )
      {
      }
      else
      {
         return;
      }
      event_active = EventActive( eew_Sta, Sta, WaveBuf, Gparm, Ewh, &sample_index );      
      if ( event_active == 1 )           /* Event active at end of message */
         return;
   }
}


     /*****************************************************
      *                   EventActive()                   *
      *                                                   *
      *  Returns 1 if pick is active; otherwise 0.        *
      *****************************************************/

int EventActive( PEEW *eew_Sta, STATION *Sta, char *WaveBuf, GPARM *Gparm, EWH *Ewh,
                 int *sample_index )
{
   PICK *Pick = &Sta->Pick;        /* Pointer to pick variables */
   PARM *Parm = &Sta->Parm;        /* Pointer to config parameters */

   TRACE_HEADER *WaveHead = (TRACE_HEADER *) WaveBuf;
   int         *WaveLong = (int *) (WaveBuf + sizeof(TRACE_HEADER));
   
   char 	outmsg_P[200], cmd[300], tins[10];
   
   int upd_sec=0;			 /* Seconds for updating parameters */

/* An event (pick and/or coda) is active.
   See if it should be declared over.
   *************************************/
   while ( ++(*sample_index) < WaveHead->nsamp )
   {
      int new_sample;         /* Current sample */

      new_sample = WaveLong[*sample_index];

/* Update Sta.rold, Sta.rdat, Sta.old_sample, Sta.esta,
   Sta.elta, Sta.eref, and Sta.eabs using the current sample
   *********************************************************/ 
      Sample( new_sample, Sta );
      sniff_eew( new_sample, eew_Sta);
    
/* EEW
   *******************/
    if(Pick->status == 1 || Pick->status == 2 || Pick->status == 3)
    {     	        
/* Compute maximum pa pv pd tc 
   ****************************************/                        
	    // if(eew_Sta->pa < eew_Sta->a)     {  eew_Sta->pa = eew_Sta->a; eew_Sta->count_cpa += 1;   }   
	    if( fabs(eew_Sta->pa) < fabs(eew_Sta->a ) )     {  eew_Sta->pa = fabs(eew_Sta->a );  }	
	    if( fabs(eew_Sta->pv) < fabs(eew_Sta->v ) )     {  eew_Sta->pv = fabs(eew_Sta->v );  }
	    if( fabs(eew_Sta->pd) < fabs(eew_Sta->d ) )     {  eew_Sta->pd = fabs(eew_Sta->d );  }
	    // if( fabs(eew_Sta->tc) < fabs(eew_Sta->tc) )     {  eew_Sta->tc = fabs(eew_Sta->tc);  }

			// if( eew_Sta->buf == 0.1*((int)(WaveHead->samprate)) )
				// eew_Sta->pa_01sec_count = eew_Sta->count_cpa;	
		
			// if( eew_Sta->buf == 0.2*((int)(WaveHead->samprate)) )
			// {
				// eew_Sta->pa_02sec_count = eew_Sta->count_cpa;	
				// if(eew_Sta->pa_02sec_count < 1) 
				// {
					// printf("Pa02---%02d -- %d \n", eew_Sta->pa_02sec_count, eew_Sta->buf);				
					// Ini_eew_para(eew_Sta);
					// Pick->status = 0;
					// return 0;  // noise					
				// }		    
			// }
			
			// if( eew_Sta->buf == 0.5*((int)(WaveHead->samprate)) )
			// {
				// eew_Sta->pa_05sec_count = eew_Sta->count_cpa;	
				// if(eew_Sta->pa_05sec_count < 3) 
				// {
					// printf("Pa05---%02d -- %d \n", eew_Sta->pa_05sec_count, eew_Sta->buf);				
					// Ini_eew_para(eew_Sta);
					// Pick->status = 0;
					// return 0;  // noise					
				// }					
			// }
			
			// if( eew_Sta->buf == 1*((int)(WaveHead->samprate)) )
			// {
				// eew_Sta->pa_1sec_count = eew_Sta->count_cpa;
			// }
			
			// if( eew_Sta->buf == 1.5*((int)(WaveHead->samprate)) )
			// {
				// eew_Sta->pa_15sec_count = eew_Sta->count_cpa;				
			// }	
		 
			
/* Report pick 
   ****************************************/ 
        if(Pick->status == 2)
        {
		    for(upd_sec=2;upd_sec<limit_sec;upd_sec+=1)
		    {
         	    if( eew_Sta->buf == upd_sec*((int)(WaveHead->samprate)) )
         	    {         		         		         		      		 
         	    	 if (eew_Sta->tc > 4.0) eew_Sta->tc= 0.0;         		         		        		         		
			    	sprintf(outmsg_P,"%s %s %s %s %f %f %f %f %f %f %11.5f %d %d %d"
                                    ,eew_Sta->sta 
                                    ,eew_Sta->chan
                                    ,eew_Sta->net
                                    ,eew_Sta->loc
                                    ,eew_Sta->lon 
                                    ,eew_Sta->lat ,eew_Sta->pa
                                    ,eew_Sta->pv  ,eew_Sta->pd
                                    ,eew_Sta->tc  ,eew_Sta->ptime
			    					, eew_Sta->weight
			    					, eew_Sta->inst
			    					, upd_sec);     

			    	// printf( "outmsg_P--: %s %s %s %s %d %d\n"
                                    // ,eew_Sta->sta 
                                    // ,eew_Sta->chan
                                    // ,eew_Sta->net
                                    // ,eew_Sta->loc
									// ,eew_Sta->weight
									// ,upd_sec);	

					if(upd_sec==2)
					{
					  // if(eew_Sta->pa_01sec_count < 4)
						// if(eew_Sta->pa_01sec_count==eew_Sta->pa_02sec_count)
							// if(eew_Sta->pa_02sec_count==eew_Sta->pa_05sec_count)
								// if(eew_Sta->pa_05sec_count==eew_Sta->pa_1sec_count)
									// if(eew_Sta->pa_1sec_count==eew_Sta->pa_15sec_count)																					
										// if(eew_Sta->pa_15sec_count==eew_Sta->count_cpa)
									    // {	
											// printf ("-pa-%s-%.4lf-%.4lf-%02d-%02d-%02d-%02d-%02d-%02d\n"
													// ,outmsg_P
													// ,Parm->MinPa
													// ,Parm->MinPv
													// ,eew_Sta->pa_01sec_count										
													// ,eew_Sta->pa_02sec_count									
													// ,eew_Sta->pa_05sec_count
													// ,eew_Sta->pa_1sec_count
													// ,eew_Sta->pa_15sec_count
													// ,eew_Sta->count_cpa);
											// Ini_eew_para(eew_Sta); 		
											// Pick->status = 0;   									
											// return 0; 
										// }					
						if( eew_Sta->pv < Parm->MinPv || eew_Sta->pa < Parm->MinPa  )
						{	

							// printf ("-c6-%s-%.4lf-%.4lf-%02d-%02d-%02d-%02d-%02d-%02d\n"
									// ,outmsg_P
									// ,Parm->MinPa
									// ,Parm->MinPv
									// ,eew_Sta->pa_01sec_count										
									// ,eew_Sta->pa_02sec_count									
									// ,eew_Sta->pa_05sec_count
									// ,eew_Sta->pa_1sec_count
									// ,eew_Sta->pa_15sec_count
									// ,eew_Sta->count_cpa);							
							Ini_eew_para(eew_Sta); 		
							Pick->status = 0;    
							return 0; 
						}
						else
						{
							// printf("\n");
						}						
					}
						
					if ( Gparm->StorePicks == 1 && upd_sec < 3)
					{
						printf( "outmsg_P: %s \n"
                                    ,outmsg_P
									);						
						sprintf(cmd,"echo %s >> pfile_P.p12", outmsg_P);
						system(cmd);
					}	
           	    	ReportPick_eew2( Sta, eew_Sta, Gparm, Ewh, outmsg_P );	
     	 	    	if(upd_sec>=9) 
			    		Pick->status = 3;   
					break;
         	    }		
		    }
        }
        if( Pick->status==3 && eew_Sta->buf >= 20*((int)(WaveHead->samprate)) )    
        {		  
     	 		Ini_eew_para(eew_Sta); 		
				Pick->status = 0;      
		// printf("----c7 --- MinPv: %lf , upd_sec: %d , buf: %d \n"
				// , Parm->MinPv, upd_sec, eew_Sta->buf);				
				return 0; 
        }          
        eew_Sta->buf ++;  	 
    }
     

/* A pick is active
   ****************/
    if ( Pick->status == 1 )
    {
         int    i;              /* Peak index */
         int    noise;          /* 1 if ievent is noise */
         double xon;            /* Used in pick weight calculation */
         double xpc;            /* ditto */
         double xp0,xp1,xp2;    /* ditto */


/* zero cressings should be equal to setting value within 1 sec
   **************************************************************/		 
		if( eew_Sta->buf > 1*((int)(WaveHead->samprate)) )
		{
			//printf("buf:%d > %d \n", eew_Sta->buf, (int)(WaveHead->samprate));
			Ini_eew_para(eew_Sta);
			Pick->status = 0;
			return 0;  // noise
		}
				 
/* Save first 10 points after pick for first motion determination
   **************************************************************/
         if ( ++Sta->evlen < 10 )
            Sta->sarray[Sta->evlen] = new_sample;

/* Store current data if it is a new extreme value
   ***********************************************/
         if ( Sta->next < 3 )
         {
            double adata;
            adata = fabs( Sta->rdat );
            if ( adata > Sta->tmax ) Sta->tmax = adata;
         }


/* Increment zero crossing interval counter.  Terminate
   pick if no zero crossings have occurred recently.
   ****************************************************/
         if ( ++Sta->mint > Parm->MaxMint )
         {
            Ini_eew_para(eew_Sta);
			Pick->status = 0;
            return 0;
	 }
/* Test for small zero crossing
   ****************************/
         if ( Sign( Sta->rdat, Sta->rold ) == Sta->rdat )
            continue;

/* Small zero crossing found.
   Reset zero crossing interval counter.
   ************************************/
         Sta->mint = 0;


/* Store extrema of preceeding half cycle
   **************************************/
         if ( Sta->next < 3 )
         {
            Pick->xpk[Sta->next++] = Sta->tmax;

            if ( Sta->next == 1 )
            {
               double vt3;
               vt3 = Sta->tmax / 3.;
              // Sta->rbig = ( vt3 > Sta->rbig ) ? vt3 : Sta->rbig;
            }

            Sta->tmax = 0.;
         }


/*  See if the pick is over
    ***********************/
           if ( (++Sta->m < Parm->MinSmallZC) )
		   // if ( (++Sta->m != Parm->MinSmallZC) )
            continue;                    /* It's not over */

/* A valid pick was found.
   Determine the first motion.
   ***************************/
         Pick->FirstMotion = ' ';            /* First motion unknown */

/* Pick weight calculation
   ***********************/
         xpc = ( Pick->xpk[0] > fabs( (double)Sta->sarray[0] ) ) ?
               Pick->xpk[0] : Pick->xpk[1];
         xon = fabs( (double)Sta->xdot / Sta->xfrz );
         xp0 = Pick->xpk[0] / Sta->xfrz;
         xp1 = Pick->xpk[1] / Sta->xfrz;
         xp2 = Pick->xpk[2] / Sta->xfrz;

         Pick->weight = 5;
		 	 
		 
         if ( (xp0*xpc > 90.) && (xon > .5) )
            Pick->weight = 4;			 
		 
         if ( (xp0 > 7.) && (xon > .5) && (xpc > 10.) )
            Pick->weight = 3;		 
		 
         if ( (xp0 > 2.) && (xon > .5) && (xpc > 25.) )
            Pick->weight = 2;

         if ( (xp0 > 3.) && ((xp1 > 3.) || (xp2 > 3.)) && (xon > .5)
         && (xpc > 100.) )
            Pick->weight = 1;

         if ( (xp0 > 4.) && ((xp1 > 6.) || (xp2 > 6.)) && (xon > .5)
         && (xpc > 200.) )
            Pick->weight = 0;

         if ( Pick->weight >= 4 ) 
         {
         	Ini_eew_para(eew_Sta);	
			Pick->status = 0;			
         	return 0;
         }			
			
         Pick->status = 2;               /* Pick calculated but not reported */
/* EEW
   ********************/        
	  eew_Sta->weight = Pick->weight;
	  upd_sec = 0;
			    	// printf( "outmsg_P--: %s %s %s %s %d %d\n"
                                    // ,eew_Sta->sta 
                                    // ,eew_Sta->chan
                                    // ,eew_Sta->net
                                    // ,eew_Sta->loc
									// ,eew_Sta->weight
									// ,upd_sec);		  
	  // printf("Event found ! , buf: %d , no_zero:%d \n", eew_Sta->buf, Sta->m);	 
    }
 }
   return 1;       /* Event is still active */
}


void Ini_eew_para(PEEW *eew_Sta)
{
	 eew_Sta->buf = 0;                                       
	 eew_Sta->pa  = 0.0;
	 eew_Sta->pv  = 0.0;
	 eew_Sta->pd  = 0.0;	  
	 eew_Sta->tc  = 0.0;	
	 eew_Sta->count_cpa = 0;
     eew_Sta->pa_01sec_count  = 0;		 
     eew_Sta->pa_02sec_count  = 0;	 
     eew_Sta->pa_05sec_count  = 0;
     eew_Sta->pa_1sec_count   = 0;
     eew_Sta->pa_15sec_count  = 0; 	
}

