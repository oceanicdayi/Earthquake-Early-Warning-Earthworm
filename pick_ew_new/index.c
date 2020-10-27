
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: index.c,v 1.5 2008/03/28 18:20:31 paulf Exp $
 *
 *    Revision history:
 *     $Log: index.c,v $
 *     Revision 1.5  2008/03/28 18:20:31  paulf
 *     added in PickIndexDir option to specify where pick indexes get stuffed
 *
 *     Revision 1.4  2006/11/17 19:45:27  dietz
 *     Bug fix: Moved creation of index file name outside of all loops.
 *
 *     Revision 1.3  2006/11/17 18:24:02  dietz
 *     Changed index file name from pick_ew.ndx to pick_ew_MMM.ndx where
 *     MMM is the moduleid of pick_ew. This will allow multiple instances
 *     of pick_ew to run without competing for the same index file.
 *
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

#include <stdio.h>
#include <string.h>
#include <earthworm.h>

  /***************************************************************
   *                         GetPickIndex()                      *
   *                                                             *
   *           Get initial pick index number from disk file      *
   ***************************************************************/

int GetPickIndex( unsigned char modid , char *dir)
{
   FILE      *fpIndex;
   char       fname[1024];        /* Name of pick index file */
   static int PickIndex = -1;

/* Build name of pick index file
   *****************************/
   if (dir == NULL) 
   {
      sprintf( fname, "pick_ew_%03d.ndx", (int) modid );
   } else {
      sprintf( fname, "%s/pick_ew_%03d.ndx", dir, (int) modid );
   }

/* Get initial pick index from file
   ********************************/
   if ( PickIndex == -1 )
   {
      fpIndex = fopen( fname, "r" );        /* Fails if file doesn't exist */

      if ( fpIndex != NULL )
      {
         fscanf( fpIndex, "%d", &PickIndex );
         fclose( fpIndex );
      }
   }

/* Update the pick index
   *********************/
   if ( ++PickIndex > 999999 ) PickIndex = 0;

/* Write the pick index to disk
   ****************************/
   fpIndex = fopen( fname, "w" );
   fprintf( fpIndex, "%4d\n", PickIndex );
   fclose( fpIndex );

   return PickIndex;
}
