
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: sign.c,v 1.1 2000/02/14 19:06:49 lucky Exp $
 *
 *    Revision history:
 *     $Log: sign.c,v $
 *     Revision 1.1  2000/02/14 19:06:49  lucky
 *     Initial revision
 *
 *
 */


    /******************************************************************
     *                              sign.c                            *
     *                                                                *
     *  Returns: |x| * sign( y )                                       *
     ******************************************************************/


double Sign( double x, double y )
{
   if ( x == 0. )
      return 0.;

   if ( x < 0. )
   {
      if ( y < 0. )
         return x;
      else
         return -x;
   }
   else
   {
      if ( y < 0. )
         return -x;
      else
         return x;
   }
}
