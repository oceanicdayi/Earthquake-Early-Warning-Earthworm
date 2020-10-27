

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <sys/timeb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <ctype.h>

typedef struct 
{
    char   sta[8];
    char   chan[8];
    char   net[8];     
    char   loc[8];    
    int    inst;   // for instrument type 1-Acc 2-BB 3-Short Period does not use for magnitude determination
    int    srate;   
    double gain;
    double lat;
    double lon;        
    
    double dt;
    
    double X1;
    double X2;
    double Y1;
    double Y2;

    double XX1;
    double XX2;
    double YY1;
    double YY2;
    
    double x1;
    double x2;
    double y1;
    double y2;    
    
    
    double ave;
    double ave0;
    double ave1;
    double acc0;    
    double vel0;
    
    double acc0_vel;    
    double vel0_dis;     
    
    double vvsum;
    double ddsum;
    
    double a;
    double v;
    double d;
    double ddv;

    
    double pa;
    double pv;
    double pd;
    double tc;
    
    double ptime;             /* Pick time */     
     
    double avd[3];
    
    int flag;
    double report_time;   // in seconds. epoch seconds.
    int eew_flag;
    
    
    int buf;   
	int buf_endpick;
    
    
    
    
    
    
   double rold;
   double rdat;
   int    old_sample;
   double esta;
   double elta;  
   double eref; 
   double eabs;   
    
   int weight;
    
    
} PEEW;

   
int sniff_eew( int LongSample, PEEW *eew_Sta)
{

   //int             i;

   double    B0;
   double    B1;
   double    B2;
   double    A1;
   double    A2;
 
//-- For TcPd analysis 

   double    OUTPUT,acc,acc_vel,ddv;

   //double delay;


   // Initializing 0.075Hz two poles Butterworth high pass filter


   if(eew_Sta->srate == 100)
   {
      B0 = 0.9966734;
      B1 = -1.993347;
      B2 = 0.9966734;
      A1 = -1.993336;
      A2 = 0.9933579;
   }     
   if(eew_Sta->srate == 50)
   {
      B0 = 0.993357897;
      B1 = -1.98671579;
      B2 = 0.993357897;
      A1 = -1.98667157;
      A2 = 0.986759841;
   }	
   if(eew_Sta->srate == 40)
   {
      B0 = 0.9917042;
      B1 = -1.983408;
      B2 = 0.9917042;
      A1 = -1.983340;
      A2 = 0.9834772;
   }	
   if(eew_Sta->srate == 20)
   {
      B0 = 0.983477175;
      B1 = -1.96695435;
      B2 = 0.983477175;
      A1 = -1.96668136;
      A2 = 0.967227399;
   }
 
                              	eew_Sta->avd[0]=eew_Sta->a;
                            	eew_Sta->avd[1]=eew_Sta->v;
                           	eew_Sta->avd[2]=eew_Sta->d; 
 
               	
                            eew_Sta->v=LongSample;   // fetch trace data point by point	                                                                                                                                                                                                                         
                            eew_Sta->v=eew_Sta->v/eew_Sta->gain;       /*  for gain factor   */
                            eew_Sta->ave = eew_Sta->ave*999.0/1000.0+eew_Sta->v/1000.0;	// remove instrument response
                            eew_Sta->v=eew_Sta->v-(eew_Sta->ave);				// remove offset                             
                                        
                           	/*--------------For Recursive Filter  high pass 2 poles at 0.075 Hz */  
                           	OUTPUT = B0*eew_Sta->v + B1*eew_Sta->X1 + B2*eew_Sta->X2;
                           	OUTPUT = OUTPUT - ( A1*eew_Sta->Y1 + A2*eew_Sta->Y2 );
            	           	eew_Sta->Y2 = eew_Sta->Y1;
                           	eew_Sta->Y1 = OUTPUT;
                           	eew_Sta->X2 = eew_Sta->X1;
                           	eew_Sta->X1 = eew_Sta->v;
                           	eew_Sta->v = OUTPUT;
                           	/*--------------End of Recursive Filter*/                                        
                                        
                            //-- for Acc to Vel
                            if(eew_Sta->inst==1)
                            {
                                 acc=eew_Sta->v;
                                 eew_Sta->v=(acc+eew_Sta->acc0)*eew_Sta->dt/2.0+eew_Sta->vel0;
                                 eew_Sta->acc0=acc;
                                 eew_Sta->vel0=eew_Sta->v;
                                 eew_Sta->ave0 = eew_Sta->ave0*(10.-eew_Sta->dt)/10.0+eew_Sta->v*eew_Sta->dt/10.0;
                                 eew_Sta->v=eew_Sta->v-eew_Sta->ave0;     
                                 
                           	/*--------------For Recursive Filter  high pass 2 poles at 0.075 Hz */  
                           	OUTPUT = B0*eew_Sta->v + B1*eew_Sta->XX1 + B2*eew_Sta->XX2;
                           	OUTPUT = OUTPUT - ( A1*eew_Sta->YY1 + A2*eew_Sta->YY2 );
            	           	eew_Sta->YY2 = eew_Sta->YY1;
                           	eew_Sta->YY1 = OUTPUT;
                           	eew_Sta->XX2 = eew_Sta->XX1;
                           	eew_Sta->XX1 = eew_Sta->v;
                           	eew_Sta->v = OUTPUT;
                           	/*--------------End of Recursive Filter*/                                                                                               
                            }                           
 //--------------------------------------------------------------------------------------------                                                                                                                                 
                            eew_Sta->a=(eew_Sta->v - eew_Sta->avd[1])/eew_Sta->dt;
                            
                                 acc_vel = eew_Sta->v;
                                 eew_Sta->d = (acc_vel+eew_Sta->acc0_vel)*eew_Sta->dt/2.0+eew_Sta->vel0_dis;
                                 eew_Sta->acc0_vel = acc_vel;
                                 eew_Sta->vel0_dis = eew_Sta->d;
                                 eew_Sta->ave1 = eew_Sta->ave1*(10.-eew_Sta->dt)/10.0+eew_Sta->d*eew_Sta->dt/10.0;
                                 eew_Sta->d=eew_Sta->d-eew_Sta->ave1;                           
                             
                            /*--------------For Recursive Filter  high pass 2 poles at 0.075 Hz */  
                            OUTPUT = B0*eew_Sta->d + B1*eew_Sta->x1 + B2*eew_Sta->x2;
                            OUTPUT = OUTPUT - ( A1*eew_Sta->y1 + A2*eew_Sta->y2 );
            	            eew_Sta->y2 = eew_Sta->y1;
                            eew_Sta->y1 = OUTPUT;
                            eew_Sta->x2 = eew_Sta->x1;
                            eew_Sta->x1 = eew_Sta->d;
                            eew_Sta->d = OUTPUT;
                            /*--------------End of Recursive Filter*/    
                                 ddv = (eew_Sta->d- eew_Sta->avd[2] )/eew_Sta->dt;
                                 
                                 eew_Sta->ddsum += eew_Sta->d*eew_Sta->d;
                                 eew_Sta->vvsum += ddv * ddv;      

                                 eew_Sta->tc=2.*3.141592654/sqrt((eew_Sta->vvsum)/(eew_Sta->ddsum));                                                                                                                        
 //--------------------------------------------------------------------------------------------                           
   


  return 1;
}



