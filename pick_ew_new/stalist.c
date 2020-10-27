
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: stalist.c,v 1.6 2009/10/22 08:55:31 quintiliani Exp $
 *
 *    Revision history:
 *     $Log: stalist.c,v $
 *     Revision 1.6  2009/10/22 08:55:31  quintiliani
 *     Increased string length to MAX_LEN_STRING_STALIST, now set to 512
 *
 *     Revision 1.5  2006/09/20 22:44:07  dietz
 *     Modified to be able to process multiple "StaFile" commands for setting
 *     per-channel picking parameters.
 *
 *     Revision 1.4  2005/04/05 21:26:13  dietz
 *     increased string to 200 chars
 *
 *     Revision 1.3  2004/04/29 22:44:52  kohler
 *     Pick_ew now produces new TYPE_PICK_SCNL and TYPE_CODA_SCNL messages.
 *     The station list file now contains SCNLs, rather than SCNs.
 *     Input waveform messages may be of either TYPE_TRACEBUF or TYPE_TRACEBUF2.
 *     If the input waveform message is of TYPE_TRACEBUF (without a location code),
 *     the location code is assumed to be "--".  WMK 4/29/04
 *
 *     Revision 1.2  2003/03/24 22:19:46  lombard
 *     Nsta erroneously set to sta; should be set to i to get correct
 *     number of picked stations.
 *
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <earthworm.h>
#include <transport.h>
#include "pick_ew.h"

#define MAX_LEN_STRING_STALIST 512

/* Function prototype
   ******************/
void InitVar( STATION * );
int  IsComment( char [] );


  /***************************************************************
   *                         GetStaList()                        *
   *                                                             *
   *                     Read the station list                   *
   *                                                             *
   *  Returns -1 if an error is encountered.                     *
   ***************************************************************/

int GetStaList( STATION **Sta, int *Nsta, GPARM *Gparm )
{
   char    string[MAX_LEN_STRING_STALIST];
   int     i,ifile;
   int     nstanew;
   STATION *sta;
   FILE    *fp;

/* Loop thru the station list file(s)
   **********************************/
   for( ifile=0; ifile<Gparm->nStaFile; ifile++ )
   {
      if ( ( fp = fopen( Gparm->StaFile[ifile].name, "r") ) == NULL )
      {
         logit( "et", "pick_ew: Error opening station list file <%s>.\n",
                Gparm->StaFile[ifile].name );
         return -1;
      }

   /* Count channels in the station file.
      Ignore comment lines and lines consisting of all whitespace.
      ***********************************************************/
      nstanew = 0;
      while ( fgets( string, MAX_LEN_STRING_STALIST, fp ) != NULL )
         if ( !IsComment( string ) ) nstanew++;

      rewind( fp );

   /* Re-allocate the station list
      ****************************/
      sta = (STATION *) realloc( *Sta, (*Nsta+nstanew)*sizeof(STATION) );
      if ( sta == NULL )
      {
         logit( "et", "pick_ew: Cannot reallocate the station array\n" );
         return -1;
      }
      *Sta = sta;           /* point to newly realloc'd space */
      sta  = *Sta + *Nsta;  /* point to next empty slot */

   /* Initialize internal variables in station list
      *********************************************/
      for ( i = 0; i < nstanew; i++ ) InitVar( &sta[i] );
      
      //for ( i = 0; i < nstanew; i++ ) 
      //{
      //		printf("Pick->status: %d \n", sta[i].Pick.status);
      //}     

   /* Read stations from the station list file into the station
      array, including parameters used by the picking algorithm
      *********************************************************/
      i = 0;
      while ( fgets( string, MAX_LEN_STRING_STALIST, fp ) != NULL )
      {
         int ndecoded;
         int pickflag;
         int pin;
//printf("---1 \n");
         if ( IsComment( string ) ) continue;
         ndecoded = sscanf( string,
                    "%d%d%s%s%s%s%d%d%lf%lf%lf%lf%lf%lf%lf%lf%lf",
                 &pickflag,
                 &pin,
                  sta[i].sta,
                  sta[i].chan,
                  sta[i].net,
                  sta[i].loc,

                 &sta[i].Parm.MinSmallZC,
                 &sta[i].Parm.MaxMint,

                 &sta[i].Parm.RawDataFilt,
                 &sta[i].Parm.CharFuncFilt,
                 &sta[i].Parm.StaFilt,
                 &sta[i].Parm.LtaFilt,
                 &sta[i].Parm.EventThresh,
                 &sta[i].Parm.RmavFilt,
                 &sta[i].Parm.DeadSta,
				 &sta[i].Parm.MinPa,
				 &sta[i].Parm.MinPv				 
				);
                 
                 //printf("string: %s \n", string);
 //printf("---2 , ndecoded: %d\n",ndecoded);                
         if ( ndecoded < 16 )
         {
		 printf("-------error\n");
            logit( "et", "pick_ew: Error decoding station file.\n" );
            logit( "e", "ndecoded: %d\n", ndecoded );
            logit( "e", "Offending line:\n" );
            logit( "e", "%s\n", string );
            return -1;
         }
         if ( pickflag == 0 ) continue;
         i++;
      }
      fclose( fp );
      logit( "", "pick_ew: Loaded %d channels from station list file:  %s\n",
             i, Gparm->StaFile[ifile].name);
      Gparm->StaFile[ifile].nsta = i;
      *Nsta += i;
   } /* end for over all StaFiles */
   return 0;
}


 /***********************************************************************
  *                             LogStaList()                            *
  *                                                                     *
  *                         Log the station list                        *
  ***********************************************************************/

void LogStaList( STATION *Sta, int Nsta )
{
   int i;

   logit( "", "\nPicking %d channel", Nsta );
   if ( Nsta != 1 ) logit( "", "s" );
   logit( "", " total:\n" );

   for ( i = 0; i < Nsta; i++ )
   {
      logit( "", "%-5s",     Sta[i].sta );
      logit( "", " %-3s",    Sta[i].chan );
      logit( "", " %-2s",    Sta[i].net );
      logit( "", " %-2s",    Sta[i].loc );

      logit( "", "  %2d",    Sta[i].Parm.MinSmallZC );
      logit( "", "  %1d",    Sta[i].Parm.MinBigZC );
      logit( "", "  %2ld",   Sta[i].Parm.MinPeakSize );
      logit( "", "  %3d",    Sta[i].Parm.MaxMint );
      logit( "", "  %5.3lf", Sta[i].Parm.RawDataFilt );
      logit( "", "  %3.1lf", Sta[i].Parm.CharFuncFilt );
      logit( "", "  %3.1lf", Sta[i].Parm.StaFilt );
      logit( "", "  %4.2lf", Sta[i].Parm.LtaFilt );
      logit( "", "  %3.1lf", Sta[i].Parm.EventThresh );
      logit( "", "  %5.3lf", Sta[i].Parm.RmavFilt );
      logit( "", "  %4.0lf", Sta[i].Parm.DeadSta );
      logit( "", "\n" );
   }
   logit( "", "\n" );
}


    /*********************************************************************
     *                             IsComment()                           *
     *                                                                   *
     *  Accepts: String containing one line from a pick_ew station list  *
     *  Returns: 1 if it's a comment line                                *
     *           0 if it's not a comment line                            *
     *********************************************************************/

int IsComment( char string[] )
{
   int i;

   for ( i = 0; i < (int)strlen( string ); i++ )
   {
      char test = string[i];

      if ( test!=' ' && test!='\t' && test!='\n' )
      {
         if ( test == '#'  )
            return 1;          /* It's a comment line */
         else
            return 0;          /* It's not a comment line */
      }
   }
   return 1;                   /* It contains only whitespace */
}
