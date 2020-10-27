/*
 * tcpd.c:  
 *              1) reads a configuration file using kom.c routines 
 *                 (tcpd_config).
 *              2) looks up shared memory keys, installation ids, 
 *                 module ids, message types from earthworm.h tables 
 *                 using getutil.c functions (tcpd_lookup).
 *              3) attaches to one public shared memory region for
 *                 input and output using transport.c functions.
 *              4) processes hard-wired message types from configuration-
 *                 file-given installations & module ids (This source
 *                 code expects to process TYPE_HINVARC & TYPE_H71SUM
 *                 messages).
 *              5) sends heartbeats and error messages back to the
 *                 shared memory region (tcpd_status).
 *              6) writes to a log file using logit.c functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <earthworm.h>
#include <kom.h>
#include <transport.h>
#include <lockfile.h>
#include <time_ew.h>
#include <chron3.h>
#include <math.h>

#include "platform.h"
#include "dayi_time.h"
#include "locate.h"

#define number 1000
#define DEGRAD 0.01745329
#define LINK_SIZE 1000


/* Functions in this source file 
 *******************************/
void   tcpd_config  ( char * );
int    tcpd_lookup  ( void );
void   tcpd_status  ( unsigned char, short, char * );
void   tcpd_h71sum  ( void );
int    split_c(char **out_ss, char *in_ss);
void   ReportEEW( SHM_INFO Region, MSG_LOGO reclogo_out, char *outmsg );
void   ReportEEW_record( SHM_INFO Region, MSG_LOGO reclogo_out, char *outmsg );
struct tm *gmtime_ew_dayi( const time_t *epochsec, struct tm *res );
void   sort_array(double *array, int num);
void   sort_array_mag(MAG_DATA *array, int num);
void   sort_array_P_S(PEEW *array, int num);
double pa_HS(double M, double dis);
double pa_HL(double M, double dis);
double pa_HH(double M, double dis);

static  SHM_INFO  Region;      /* shared memory region to use for i/o    */
#define   MAXLOGO   3
MSG_LOGO  GetLogo[MAXLOGO];    /* array for requesting module,type,instid */
short     nLogo;
pid_t     myPid;               /* for restarts by startstop               */

static  SHM_INFO  Region_out;      /* shared memory region to use for i/o    */
#define   MAXLOGO_out   2
MSG_LOGO  GetLogo_out[MAXLOGO];    /* array for requesting module,type,instid */
short     nLogo_out;
pid_t     myPid_out;               /* for restarts by startstop               */
MSG_LOGO  reclogo_out;          /* logo of retrieved message     */

#define BUF_SIZE 60000          /* define maximum size for an event msg   */
static char Buffer[BUF_SIZE];   /* character string to hold event message */
static char Buffer_out[BUF_SIZE];   /* character string to hold event message */
        
/* Things to read or derive from configuration file
 **************************************************/
static char    RingName[MAX_RING_STR];        /* name of transport ring for i/o    */
static char    RingName_out[MAX_RING_STR];        /* name of transport ring for i/o    */
static char    MyModName[MAX_MOD_STR];       /* speak as this module name/id      */
static int     LogSwitch;           /* 0 if no logfile should be written */
static int     Ignore_weight_P;		// Ignore bad P picks
static int     Ignore_weight_S;		// Ignore bad S picks
static long    HeartBeatInterval;   /* seconds between heartbeats        */
static double   MagMax;
static double   MagMin;

static double   Trig_tm_win;
static double   Trig_dis_win;
static double   Active_parr_win;

static int      Report_Limit = 1; // was  hardcoded at 10, limit the number of reports to this value

static int      Term_num = 15;    // The last report should be less than this number.
static double   Boundary_P;    // boundary of shallow and deep layers
static double   SwP_V;       // initial velocity in shallow layer
static double   SwP_VG;      // gradient velocity in shallow layer
static double   DpP_V;       // initial velocity in deep layer
static double   DpP_VG;      // gradient velocity in deep layer
static double   Boundary_S;    // boundary of shallow and deep layers
static double   SwS_V;       // initial velocity in shallow layer
static double   SwS_VG;      // gradient velocity in shallow layer
static double   DpS_V;       // initial velocity in deep layer
static double   DpS_VG;      // gradient velocity in deep layer

static double   Show_Report;      // 0: Disable, 1:enable

static char    Mark[50];        /* name of transport ring for i/o    */


SCNL 		MagReject[60];
int		num_mag_reject=0;

/* Things to look up in the earthworm.h tables with getutil.c functions
 **********************************************************************/
static long          RingKey;       /* key of transport ring for i/o     */
static long          RingKey_out;       /* key of transport ring for i/o     */
static unsigned char InstId;        /* local installation id             */
static unsigned char MyModId;       /* Module Id for this program        */
static unsigned char TypeHeartBeat; 
static unsigned char TypeError;
static unsigned char TypeEEW;
static unsigned char Type_EEW_record;

/* Error messages used by tcpd 
 *********************************/
#define  ERR_MISSMSG       0   /* message missed in transport ring       */
#define  ERR_TOOBIG        1   /* retreived msg too large for buffer     */
#define  ERR_NOTRACK       2   /* msg retreived; tracking limit exceeded */
static char  Text[150];        /* string for log/error messages          */

//------------------------------------------------------------------------------EEW

	int  count;
	FILE *fp;
	
	int max_sta=0;
	int rep_fin=0;
	

int G_Q, G_n, pre_G_n=-1, G_sta_num, G_n_mag, pre_G_sta_num=-1, pre_G_n_mag;
double pre_proc_time, proc_time;
double G_averr, G_con[10], G_x[10], G_y[10], G_z[10], G_t[10];
double GAP, pre_GAP=-1.0;

FILE *faa;
int num_eew=0;

double first_rp_time;

//---------------------------------------------------------------------------------
main( int argc, char **argv )
{
   time_t      timeNow;          /* current time                  */       
   time_t      timeLastBeat;     /* time last heartbeat was sent  */
   long      recsize;          /* size of retrieved message     */
   MSG_LOGO  reclogo;          /* logo of retrieved message     */
   int       res;
 
   FILE *fp;
 
   char * lockfile;
   int lockfile_fd;
   
	char **out_ss;
	int num_split;	
	
	PEEW ptr[number], vsn_ntri[number] ;
    int vsn_trigger=0;	
	int count_max=0;	/* a threshold value */
	double now_time; // used for check alive picks
	int i,j,k,jj;
	double dis_2sta;	
	struct timeb tp;	
	char tmp[120];
	
	int type_error;
	int pk_wei;
	
	char pptime[300], nntime[300];
	
	double avg_lon, avg_lat, sum_lon, sum_lat;
	double avg_ptime, sum_ptime, dif_time;
	int num_tt;
	
	char ssta[10];
	int upd_sec;
	// int ind;
/* Check EEW number 
 *******************************************/
 fp=fopen("num_eew_status","r");
 if(fp==NULL)
 {
	num_eew = 1;
	fp=fopen("num_eew_status","w");
	fprintf(fp,"%d", num_eew);
	fclose(fp);
	printf("Does not exist ! Creating num_eew_ststus file .... \n");
 }
 else
 {
	fgets(tmp,99,fp);
	if( strlen(tmp)>4 || atoi(tmp) < 1 || atoi(tmp) > 10000 )
	{	
		num_eew = 1;
		fp=fopen("num_eew_status","w");
		fprintf(fp,"%d", num_eew);
		fclose(fp);		
		printf("Wrong File ! Creating num_eew_ststus file .... \n");		
	}
	else
	{
		num_eew = atoi(tmp);
		fclose(fp);
	}
 }	
	
/* init
   ****************************/ 
	out_ss = (char**) malloc(sizeof(char*)*20);
	
	for(i=0;i<20;i++)
	{
		out_ss[i] = (char*) malloc(sizeof(char)*25);
	}   
   
    for(i=0;i<number;i++)	
	{
		ptr[i].flag=0;       
	//	ptr[i].pin=-1;		
	}

   MagMax = 10;
   MagMin = 0;
   
   Trig_tm_win = 8.0;
   Trig_dis_win = 60.0;          
   Active_parr_win = 60;
   
   Show_Report = 1;
   Term_num    = 15   ;
   Boundary_P    = 35.0 ;
   SwP_V       = 4.5  ;
   SwP_VG      = 0.085;
   DpP_V       = 6.0  ;
   DpP_VG      = 0.023;       

   Boundary_S    = 35.0 ;
   SwS_V       = 4.5  ;
   SwS_VG      = 0.085;
   DpS_V       = 6.0  ;
   DpS_VG      = 0.023;     
               
/* Check command line arguments 
 ******************************/
   if ( argc != 2 )
   {          
        fprintf( stderr, "Usage: tcpd <configfile>\n" );
        exit( 0 );
   }
/* Initialize name of log-file & open it 
 ***************************************/
   logit_init( argv[1], 0, 256, 1 );
   
/* Read the configuration file(s)
 ********************************/
   tcpd_config( argv[1] );
   logit( "" , "%s: Read command file <%s>\n", argv[0], argv[1] );
   
   printf("P-wave Velocity Model-------------\n%d %f %f %f %f %f \n"
            ,Term_num 
            ,Boundary_P 
            ,SwP_V      
            ,SwP_VG     
            ,DpP_V      
            ,DpP_VG);
   printf("S-wave Velocity Model-------------\n%d %f %f %f %f %f \n"
            ,Term_num 
            ,Boundary_S 
            ,SwS_V      
            ,SwS_VG     
            ,DpS_V      
            ,DpS_VG);			
//--------------------------------------------------------------------------------  

/* Look up important info from earthworm.h tables
 ************************************************/
   type_error = tcpd_lookup();
   
   if(type_error==-2)
   {
	printf("\n --->Please define Message Type: TYPE_EEW in Earthworm.d. \n\n");
	return;
   }
   else if(type_error==-3)
   {
	printf("\n --->Please define Message Type: Type_EEW_record in Earthworm.d. \n\n");
	return;
   }   
 
/* Reinitialize logit to desired logging level 
 **********************************************/
   logit_init( argv[1], 0, 256, LogSwitch );


   lockfile = ew_lockfile_path(argv[1]); 
   if ( (lockfile_fd = ew_lockfile(lockfile) ) == -1) {
	fprintf(stderr, "one  instance of %s is already running, exiting\n", argv[0]);
	exit(-1);
   }
/* Get process ID for heartbeat messages */
   myPid = getpid();
   if( myPid == -1 )
   {
     logit("e","tcpd: Cannot get pid. Exiting.\n");
     exit (-1);
   }

/* Attach to Input shared memory ring 
 *******************************************/
   tport_attach( &Region, RingKey );
   logit( "", "tcpd: Attached to public memory region %s: %d\n", 
          RingName, RingKey );
		  
/* Attach to Output shared memory ring 
 *******************************************/		  
   tport_attach( &Region_out, RingKey_out );
   logit( "", "tcpd: Attached to public memory region %s: %d\n", 
          RingName_out, RingKey_out );		  

/* Flush the transport ring */
   while( tport_getmsg( &Region, GetLogo, nLogo, &reclogo, &recsize, Buffer, 
			sizeof(Buffer)-1 ) != GET_NONE );
			
   while( tport_getmsg( &Region_out, GetLogo_out, nLogo_out, &reclogo_out, &recsize, Buffer_out, 
			sizeof(Buffer_out)-1 ) != GET_NONE );

   reclogo_out.type   = reclogo.type;  
   reclogo_out.mod    = reclogo.mod;   
   reclogo_out.instid = reclogo.instid;
			
/* Force a heartbeat to be issued in first pass thru main loop
 *************************************************************/
   timeLastBeat = time(&timeNow) - HeartBeatInterval - 1;

/*----------------------- setup done; start main loop -------------------------*/


     while(1)
    {
     /* send tcpd's heartbeat
      ***************************/
        if  ( time(&timeNow) - timeLastBeat  >=  HeartBeatInterval ) 
        {
            timeLastBeat = timeNow;
            tcpd_status( TypeHeartBeat, 0, "" ); 
        }
            	        	     
     /* Process all new messages    
      **************************/
        do
        {
        /* see if a termination has been requested 
         *****************************************/
           if ( tport_getflag( &Region ) == TERMINATE ||
                tport_getflag( &Region ) == myPid )
           {
			/* detach from shared memory */
                tport_detach( &Region ); 
                tport_detach( &Region_out ); 				
			/* write a termination msg to log file */
                logit( "t", "tcpd: Termination requested; exiting!\n" );
                fflush( stdout );
			/* should check the return of these if we really care */
				ew_unlockfile(lockfile_fd);
				ew_unlink_lockfile(lockfile);
                exit( 0 );
           }

        /* Get msg & check the return code from transport
         ************************************************/
           res = tport_getmsg( &Region, GetLogo, nLogo,
                               &reclogo, &recsize, Buffer, sizeof(Buffer)-1 );

           if( res == GET_NONE )          /* no more new messages     */
           {
           	//printf("get none ------- \n");
                break;
           }
           else if( res == GET_TOOBIG )   /* next message was too big */
           {                              /* complain and try again   */
                sprintf(Text, 
                        "Retrieved msg[%ld] (i%u m%u t%u) too big for Buffer[%ld]",
                        recsize, reclogo.instid, reclogo.mod, reclogo.type, 
                        sizeof(Buffer)-1 );
                tcpd_status( TypeError, ERR_TOOBIG, Text );
				printf("GET_TOOBIG \n");
                continue;
           }
           else if( res == GET_MISS )     /* got a msg, but missed some */
           {
                sprintf( Text,
                        "Missed msg(s)  i%u m%u t%u  %s.",
                         reclogo.instid, reclogo.mod, reclogo.type, RingName );
                tcpd_status( TypeError, ERR_MISSMSG, Text );
				printf("GET_MISS \n");				
           }
           else if( res == GET_NOTRACK ) /* got a msg, but can't tell */
           {                             /* if any were missed        */
                sprintf( Text,
                         "Msg received (i%u m%u t%u); transport.h NTRACK_GET exceeded",
                          reclogo.instid, reclogo.mod, reclogo.type );
                tcpd_status( TypeError, ERR_NOTRACK, Text );
				printf("GET_NOTRACK \n");					
           }

        /* Process the message 
         *********************/
           Buffer[recsize] = '\0';      /*null terminate the message*/
			//printf("Buffer:%s\n", Buffer);
           if( reclogo.type == TypeEEW ) 
           {
        		//YULB HHZ TW 01 121.297100 23.392400 0.006514 0.000187 0.000074 4.297026 1328085502.95800
        		        			
        		num_split = split_c(out_ss, Buffer);    
				
        		//printf("num_split: %d \n", num_split);
        		if (num_split!=14) 
        		{
        			printf("Read Buffer Error !");
        			return 0;
        		}		   
				
				pk_wei = atoi(out_ss[11]);
				sprintf(ssta,"%s",out_ss[0]);
				
				if( out_ss[1][2]!='Z') continue;
				// printf("----aa1 \n");
				if 
				(
					!strncmp(ssta,"YOJ",3)  ||
				    !strncmp(ssta,"EOS2",4) ||
				    !strncmp(ssta,"EOS3",4) ||					
				    !strncmp(ssta,"EOS4",4) 								
				)
				{
					if( pk_wei >= 3 )	continue;
				}
				else 
				{	
					if( pk_wei >= Ignore_weight_P )		continue;					
				}				
				
				//	flag = 0 closed
				//	flag = 1 used
				//	flag = 2 used, but not used for magnitude estimation
				//	ptr[j].Pd[0] to be used in magnitude estimation
				//	ptr[j].Pd[upd_sec] in which stored Pd with upd_sec
				
		        for(j=0;j<number;j++)
		        {		  
					if(ptr[j].flag==0)
					{        			        			
						sprintf(ptr[j].stn_name,"%s",out_ss[0]);
						sprintf(ptr[j].stn_Comp,"%s",out_ss[1]);
						sprintf(ptr[j].stn_Net ,"%s",out_ss[2]);
						sprintf(ptr[j].stn_Loc ,"%s",out_ss[3]);
 			
						upd_sec = atoi(out_ss[13]);
			
						ptr[j].longitude    = atof(out_ss[4]);
						ptr[j].latitude     = atof(out_ss[5]);
						ptr[j].Pa  	        = atof(out_ss[6]);
						ptr[j].Pv   	    = atof(out_ss[7]);
						ptr[j].Pd[upd_sec]  = atof(out_ss[8]);
						ptr[j].Tc   	    = atof(out_ss[9]);
						ptr[j].P    	    = atof(out_ss[10]);
						ptr[j].weight       = atoi(out_ss[11]);
						ptr[j].inst         = atoi(out_ss[12]);
						ptr[j].upd_sec      = upd_sec;					 
					
						ptr[j].flag=1;
						 
							// printf("Read %s.%s.%s.%s -- %d , weight --> %d, flag: %d \n"
                                  	   // , ptr[j].stn_name
                                  	   // , ptr[j].stn_Comp 
                                  	   // , ptr[j].stn_Net  
                                  	   // , ptr[j].stn_Loc  
									   // , j 
									   // , ptr[j].weight
									   // , ptr[j].flag);  							
               			 break;           			 
					}  //if(ptr[j].flag==0)
                }
           	                   

							  

				ftime(&tp);
				now_time = (double)tp.time+(double)tp.millitm/1000; 
				
                j=0;
                for(i=0;i<number;i++)
                {
						/* PaulF: this next loop delete's triggers that are not within an Active P arrival window number of seconds */
					if( ptr[i].flag>0)
					{
						datestr23 (ptr[i].P,           pptime, 256);
						datestr23 (now_time,           nntime, 256);						
						if(fabs(now_time-ptr[i].P) > Active_parr_win)
						{
								ptr[i].flag=0;
									// printf("Parr Remove %s.%s.%s.%s -- %d , weight --> %d, flag: %d -- dif: %f -- %s %s -- %.0f\n"
                                  	   // , ptr[i].stn_name
                                  	   // , ptr[i].stn_Comp 
                                  	   // , ptr[i].stn_Net  
                                  	   // , ptr[i].stn_Loc  
									   // , i 
									   // , ptr[i].weight
									   // , ptr[i].flag
									   // , fabs(now_time-ptr[i].P)
									   // , pptime
									   // , nntime
									   // , Active_parr_win); 
						}					  
						
                    	if(ptr[i].flag>0)
                    	{
                            j++;                                   
                        	for(k=0;k<number;k++)                
                 		       if(i!=k)                     
								if(  ptr[k].flag==1 )
                                  if( !strcmp( ptr[i].stn_name, ptr[k].stn_name ) && 
                                  	      !strcmp( ptr[i].stn_Comp, ptr[k].stn_Comp ) &&
                                  	      !strcmp( ptr[i].stn_Net , ptr[k].stn_Net  ) &&
                                  	      !strcmp( ptr[i].stn_Loc , ptr[k].stn_Loc  ) &&
										           ptr[i].P == ptr[k].P
                                  	)                                      	
									  if( ptr[i].upd_sec > ptr[k].upd_sec)
										{										
											for(jj=2;jj<=ptr[k].upd_sec;jj++)
											  ptr[i].Pd[jj]=ptr[k].Pd[jj];											
											
											ptr[k].flag=0; 
										}
                        }
					}					 
                }
					 
  		  //   printf("\n How many data? %d \n",j);  
                                             
	      			              
		        vsn_trigger=0;	/* current counter for number of station triggers  within distance and trigger time windows specified in .d file */
				avg_lon=0.0;
				avg_lat=0.0;
				sum_lon=0.0;
				sum_lat=0.0;
				avg_ptime=0.0;
				sum_ptime=0.0;
				num_tt = 0;
				for(i=0;i<number;i++)
				{
					if( ptr[i].flag>0 )
					{
						sum_lon += ptr[i].longitude;
						sum_lat += ptr[i].latitude;
						sum_ptime += ptr[i].P;
						num_tt++;
					}
				}
				avg_lon = sum_lon / (float)num_tt;
				avg_lat = sum_lat / (float)num_tt;
				avg_ptime = sum_ptime / (float)num_tt;
				
				for(i=0;i<number;i++)
		        {     
     		         if( ptr[i].flag > 0 )
					 // if( ptr[i].flag==1 || ptr[i].flag==2 )
				    {
		           	    k=2;
						dif_time=fabs(ptr[i].P-avg_ptime);
		            	dis_2sta=delaz(ptr[i].latitude,ptr[i].longitude,avg_lat,avg_lon);
		            													
						if ( 
						     !strncmp(ptr[i].stn_name,"EOS",3) ||						
						     !strncmp(ptr[i].stn_name,"YOJ",3) ||
							 !strncmp(ptr[i].stn_name,"JMJ",3) ||
						     !strncmp(ptr[i].stn_name,"PCY",3) ||
						     !strncmp(ptr[i].stn_name,"LAY",3) ||
						     !strncmp(ptr[i].stn_name,"TWH",3) ||							
						     !strncmp(ptr[i].stn_name,"PNG",3) ||								 
						     !strncmp(ptr[i].stn_name,"PHU",3) ||	
							 !strncmp(ptr[i].stn_name,"PTM",3) ||							 
							 !strncmp(ptr[i].stn_name,"PTT",3) ||								 
						     !strncmp(ptr[i].stn_name,"VCH",3) ||							 
						     !strncmp(ptr[i].stn_name,"VWU",3) ||							 							 
						     !strncmp(ptr[i].stn_name,"WLC",3) 								   
						    )
							{								
								// printf("==========Preserve %s -- \n", ptr[i].stn_name);
							}
							else
						    {
								if( dis_2sta>Trig_dis_win || dif_time>Trig_tm_win )
		            	        //&& dis_2sta>3.0 
		            	        //&& fabs(ptr[i].P-ptr[j].P)< Trig_tm_win
								{
									k--;
									if(k==1)
									{
										// printf("==========Remove %s -- \n", ptr[i].stn_name);									
										// printf("dis_2sta: %.0f , Trig_dis_win: %.0f , Trig_tm_win: %.0f -- %.0f -- %s\n"
										// , dis_2sta, Trig_dis_win, Trig_tm_win
										// ,dif_time
										// ,ptr[i].stn_name);									
										continue;
									}
								}
							}

						if(k >1)
						{																		
					// this station is close to at least 2 other triggers by Trig_tm_win time and Trig_dis_win distance and not closer than 3km 
							vsn_ntri[vsn_trigger].flag                   =ptr[i].flag;						
		             		vsn_ntri[vsn_trigger].latitude               =ptr[i].latitude;
		             		vsn_ntri[vsn_trigger].longitude              =ptr[i].longitude;
		             		vsn_ntri[vsn_trigger].P                      =ptr[i].P;      		
		             		vsn_ntri[vsn_trigger].Tc                     =ptr[i].Tc;
						for(jj=2;jj<=ptr[i].upd_sec;jj++)
							vsn_ntri[vsn_trigger].Pd[jj]               	 =ptr[i].Pd[jj];
		             		vsn_ntri[vsn_trigger].Pa                     =ptr[i].Pa;
		             		vsn_ntri[vsn_trigger].Pv                     =ptr[i].Pv;
		             		vsn_ntri[vsn_trigger].report_time            =ptr[i].report_time;
		             		vsn_ntri[vsn_trigger].npoints                =ptr[i].npoints;
							vsn_ntri[vsn_trigger].weight				 =ptr[i].weight;
		             		vsn_ntri[vsn_trigger].inst  				 =ptr[i].inst;
							vsn_ntri[vsn_trigger].upd_sec			     =ptr[i].upd_sec;
																								
							sprintf(vsn_ntri[vsn_trigger].stn_name ,"%s",ptr[i].stn_name); 
		             		sprintf(vsn_ntri[vsn_trigger].stn_Loc  ,"%s",ptr[i].stn_Loc); 
							sprintf(vsn_ntri[vsn_trigger].stn_Comp ,"%s",ptr[i].stn_Comp);
							sprintf(vsn_ntri[vsn_trigger].stn_Net  ,"%s",ptr[i].stn_Net);
													
		             		vsn_trigger++;
						}
					}
		        } // for(i=0;i<ntrigger;i++)
   
   	        if(vsn_trigger > 4)
		    { 	
		        if(count_max > Report_Limit)
		        {
 				/* re-initialize counter we have gone above number of sequential reports for this earthquake, wait for next one */
		        	// printf("count_max: %d  > %d \n", count_max, Report_Limit);	
		        	count_max=0;
					
					pre_G_n = -1;
					pre_G_sta_num=-1;
					pre_proc_time = -1;
					pre_GAP = -1.0;
					pre_G_n_mag = -1;
		        		        	
		           	max_sta=0;
		           //	for(i=0;i<number;i++) {ptr[i].flag=0;}
					
					for(i=0;i<number;i++)	
					{
						ptr[i].flag=0;       
						//ptr[i].pin=-1;		
					}
			
		           	rep_fin=0;
		           	         	
					//------------------------------- Update num_eew_ststus
					num_eew += 1;
					if(num_eew>10000)
					{
						num_eew = 1;
					}
					fp=fopen("num_eew_status","w");
					fprintf(fp,"%d", num_eew);
					fclose(fp);								
		           	
		           	continue;		        	
		        }   
   
		        if(vsn_trigger == max_sta) 	/* no new VALID triggers arrived */
		        {
		        	//count_max ++;
		        	//printf("Trigger: %d = max: %d , count_max: %d  \n", vsn_trigger, max_sta, count_max);  
		        	continue;
		        }
		        else if(vsn_trigger > max_sta) /* a new trigger arrived */
		        {
		        	count_max=0;		        	
		        	//printf("Trigger: %d > max: %d \n", vsn_trigger, max_sta);		        	
		        }
		        else if(vsn_trigger < max_sta)  /* no new VALID triggers arrived since last report */
		        {   
		        	//printf("Trigger: %d < max: %d , count_max: %d \n", vsn_trigger, max_sta, count_max);		        	
		        	count_max ++;		        	
		           	continue;
		        }
		         	     		       	       
				
				if( vsn_trigger > max_sta )	 /* we have a new trigger for this quake, report it */
				{
					max_sta=vsn_trigger; 
					printf("Running processTrigger .. \n");	  	     
					processTrigger( vsn_trigger, vsn_ntri);	
				}
		    }   //  if(vsn_trigger > 4)            
		    else
			{
		        	count_max=0;        		        	      		                     
			}
          }// 	         if( reclogo.type == TypeEEW )      
        } while( res != GET_NONE );  /*end of message-processing-loop */  // do        
		sleep_ew( 100 );  /* no more messages; wait for new ones to arrive */
	   
	   	ftime(&tp);
		now_time = (double)tp.time+(double)tp.millitm/1000;
		
		if( rep_fin>0 && now_time - first_rp_time > 60 )
		{
		    printf("Listen for a new event ... \n");	
		    count_max=0;		
			pre_G_n = -1;
			pre_G_sta_num=-1;
			pre_proc_time = -1;	
			pre_GAP = -1.0;
			pre_G_n_mag = -1;
		    max_sta=0;
		
			for(i=0;i<number;i++)	
				ptr[i].flag=0;       
			
		    rep_fin=0;
		        	         	
			//------------------------------- Update num_eew_ststus
			num_eew += 1;
			if(num_eew>10000)
			{
				num_eew = 1;
			}			
			fp=fopen("num_eew_status","w");
			fprintf(fp,"%d", num_eew);
			fclose(fp);										        		        	
		}		
	   //printf("==================================== Hello Earthworm \n");
    }  //    while(1)       
   

   
} // main()

/******************************************************************************
 *  tcpd_config() processes command file(s) using kom.c functions;        *
 *                    exits if any errors are encountered.                    *
 ******************************************************************************/
void tcpd_config( char *configfile )
{
   int      ncommand;     /* # of required commands you expect to process   */ 
   char     init[10];     /* init flags, one byte for each required command */
   int      nmiss;        /* number of required commands that were missed   */
   char    *com;
   char    *str;
   int      nfiles;
   int      success;
   int      i;

/* Set to zero one init flag for each required command 
 *****************************************************/   
   ncommand = 5;
   for( i=0; i<ncommand; i++ )  init[i] = 0;
   nLogo = 0;

/* Open the main configuration file 
 **********************************/
   nfiles = k_open( configfile ); 
   if ( nfiles == 0 ) {
        logit( "e",
                "tcpd: Error opening command file <%s>; exiting!\n", 
                 configfile );
        exit( -1 );
   }

/* Process all command files
 ***************************/
   while(nfiles > 0)   /* While there are command files open */
   {
        while(k_rd())        /* Read next line from active file  */
        {  
            com = k_str();         /* Get the first token from line */

        /* Ignore blank lines & comments
         *******************************/
            if( !com )           continue;
            if( com[0] == '#' )  continue;

        /* Open a nested configuration file 
         **********************************/
            if( com[0] == '@' ) {
               success = nfiles+1;
               nfiles  = k_open(&com[1]);
               if ( nfiles != success ) {
                  logit( "e", 
                          "tcpd: Error opening command file <%s>; exiting!\n",
                           &com[1] );
                  exit( -1 );
               }
               continue;
            }

        /* Process anything else as a command 
         ************************************/
  /*0*/     if( k_its("LogFile") ) {
                LogSwitch = k_int();
                init[0] = 1;
            }
  /*0*/     else if( k_its("Ignore_weight_P") ) {
                Ignore_weight_P = k_int();
                init[0] = 1;
            }
  /*0*/     else if( k_its("Ignore_weight_S") ) {
                Ignore_weight_S = k_int();
                init[0] = 1;
            }
  /*1*/     else if( k_its("MyModuleId") ) {
                str = k_str();
                if(str) strcpy( MyModName, str );
                init[1] = 1;
            }
  /*2*/     else if( k_its("RingName") ) {
                str = k_str();
                if(str) strcpy( RingName, str );
                init[2] = 1;
            }
  /*2*/     else if( k_its("RingName_out") ) {
                str = k_str();
                if(str) strcpy( RingName_out, str );
                init[2] = 1;
            }     
  /*2*/     else if( k_its("Mark") ) {
                str = k_str();
                if(str) strcpy( Mark, str );
                init[2] = 1;
            }    						
  /*3*/     else if( k_its("HeartBeatInterval") ) {
                HeartBeatInterval = k_long();
                init[3] = 1;
            }

  /*3-1*/     else if( k_its("MagMin") ) {
                MagMin = k_val();
                init[3] = 1;
            }
  /*3-2*/     else if( k_its("MagMax") ) {
                MagMax = k_val();
                init[3] = 1;
            }      
  /*3-1*/     else if( k_its("Trig_tm_win") ) {
                Trig_tm_win = k_val();
                init[3] = 1;
            }
  /*3-2*/     else if( k_its("Trig_dis_win") ) {
                Trig_dis_win = k_val();
                init[3] = 1;
            }       
  /*3-2*/     else if( k_its("Active_parr_win") ) {
                Active_parr_win = k_val();
                init[3] = 1;
            }                           
            else if( k_its("ReportLimitNumber") ) {
                Report_Limit = k_int();
            } 
  /*3-2*/     else if( k_its("Term_num") ) {
                Term_num = k_int();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("Show_Report") ) {
                Show_Report = k_int();
                init[3] = 1;
            }			
  /*3-2*/     else if( k_its("Boundary_P") ) {
                Boundary_P = k_val();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("Boundary_S") ) {
                Boundary_S = k_val();
                init[3] = 1;
            }			
  /*3-2*/     else if( k_its("SwP_V") ) {
                SwP_V = k_val();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("SwP_VG") ) {
                SwP_VG = k_val();
                init[3] = 1;
            }                                     
  /*3-2*/     else if( k_its("DpP_V") ) {
                DpP_V = k_val();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("DpP_VG") ) {
                DpP_VG = k_val();
                init[3] = 1;
            }        
  /*3-2*/     else if( k_its("SwS_V") ) {
                SwS_V = k_val();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("SwS_VG") ) {
                SwS_VG = k_val();
                init[3] = 1;
            }                                     
  /*3-2*/     else if( k_its("DpS_V") ) {
                DpS_V = k_val();
                init[3] = 1;
            } 
  /*3-2*/     else if( k_its("DpS_VG") ) {
                DpS_VG = k_val();
                init[3] = 1;
            }   			
/*  */    else if (k_its ("MagReject")) { 
			if (num_mag_reject >= 50) {
			    logit ("e",
				   "Too many <MagReject> commands in <%s>",
				   configfile);
			    logit ("e", "; max = %d; exitting!\n", (int) 50);
			    return;
			}

			if ((str = k_str ()))  
			    strcpy (MagReject[num_mag_reject].sta, str);
			if ((str = k_str()))  
			    strcpy (MagReject[num_mag_reject].chn, str);
			if ((str = k_str()))  
			    strcpy (MagReject[num_mag_reject].net, str);
			if ((str = k_str()))  
			    strcpy (MagReject[num_mag_reject].loc, str);
			num_mag_reject++;
	  }                     
                                                  
         /* Enter installation & module to get event messages from
          ********************************************************/
  /*4*/     else if( k_its("GetEventsFrom") ) {
                if ( nLogo+1 >= MAXLOGO ) {
                    logit( "e", 
                            "tcpd: Too many <GetEventsFrom> commands in <%s>", 
                             configfile );
                    logit( "e", "; max=%d; exiting!\n", (int) MAXLOGO/2 );
                    exit( -1 );
                }
                if( ( str=k_str() ) ) {
                   if( GetInst( str, &GetLogo[nLogo].instid ) != 0 ) {
                       logit( "e", 
                               "tcpd: Invalid installation name <%s>", str ); 
                       logit( "e", " in <GetEventsFrom> cmd; exiting!\n" );
                       exit( -1 );
                   }
                   GetLogo[nLogo+1].instid = GetLogo[nLogo].instid;
                }
                if( ( str=k_str() ) ) {
                   if( GetModId( str, &GetLogo[nLogo].mod ) != 0 ) {
                       logit( "e", 
                               "tcpd: Invalid module name <%s>", str ); 
                       logit( "e", " in <GetEventsFrom> cmd; exiting!\n" );
                       exit( -1 );
                   }
                   GetLogo[nLogo+1].mod = GetLogo[nLogo].mod;
                }
                if( ( str=k_str() ) ) {
                   if( GetType( str, &GetLogo[nLogo].type ) != 0 ) {
                       logit( "e", 
                               "tcpd: Invalid module name <%s>", str ); 
                       logit( "e", " in <GetEventsFrom> cmd; exiting!\n" );
                       exit( -1 );
                   }
                   GetLogo[nLogo+1].type = GetLogo[nLogo].type;
                }
                if( GetType( "TYPE_EEW", &TypeEEW ) != 0 ) {
                        logit( "e", "pick_ew: Invalid message type <%s>\n", "TYPE_EEW" );
                        exit( -1 );
                     }					

                nLogo  += 1;
                init[4] = 1;
                
                printf("nLogo: %d \n", nLogo);
                printf("GetLogo[0].type:   %d    \n", GetLogo[0].type);
                printf("GetLogo[0].mod:    %d    \n", GetLogo[0].mod);
                printf("GetLogo[0].instid: %d    \n", GetLogo[0].instid);
                
                printf("MagMin:    %f    \n", MagMin);
                printf("MagMax:    %f    \n", MagMax);    

				
				printf("LogSwitch:    %d    \n", LogSwitch);				
				printf("Ignore_weight_P:    %d    \n", Ignore_weight_P);
                printf("Ignore_weight_S:    %d    \n", Ignore_weight_S);  
                
                printf("Trig_dis_win:     %f    \n", Trig_dis_win);
                printf("Trig_tm_win:        %f    \n", Trig_tm_win);                   
                printf("Active_parr_win:    %f    \n", Active_parr_win);                         
                                        
                                        
            }

         /* Unknown command
          *****************/ 
            else {
                logit( "e", "tcpd: <%s> Unknown command in <%s>.\n", 
                         com, configfile );
                continue;
            }

        /* See if there were any errors processing the command 
         *****************************************************/
            if( k_err() ) {
               logit( "e", 
                       "tcpd: Bad <%s> command in <%s>; exiting!\n",
                        com, configfile );
               exit( -1 );
            }
        }
        nfiles = k_close();
   }

/* After all files are closed, check init flags for missed commands
 ******************************************************************/
   nmiss = 0;
   for ( i=0; i<ncommand; i++ )  if( !init[i] ) nmiss++;
   if ( nmiss ) {
       logit( "e", "tcpd: ERROR, no " );
       if ( !init[0] )  logit( "e", "<LogFile> "           );
       if ( !init[1] )  logit( "e", "<MyModuleId> "        );
       if ( !init[2] )  logit( "e", "<RingName> "          );
       if ( !init[3] )  logit( "e", "<HeartBeatInterval> " );
       if ( !init[4] )  logit( "e", "<GetEventsFrom> "     );
       logit( "e", "command(s) in <%s>; exiting!\n", configfile );
       exit( -1 );
   }

   return;
}

/******************************************************************************
 *  tcpd_lookup( )   Look up important info from earthworm.h tables       *
 ******************************************************************************/
int tcpd_lookup( void )
{
/* Look up keys to shared memory regions
   *************************************/
   if( ( RingKey = GetKey(RingName) ) == -1 ) {
        fprintf( stderr,
                "tcpd:  Invalid ring name <%s>; exiting!\n", RingName);
        exit( -1 );
   }
   if( ( RingKey_out = GetKey(RingName_out) ) == -1 ) {
        fprintf( stderr,
                "tcpd:  Invalid ring name <%s>; exiting!\n", RingName_out);
        exit( -1 );
   }
/* Look up installations of interest
   *********************************/
   if ( GetLocalInst( &InstId ) != 0 ) {
      fprintf( stderr, 
              "tcpd: error getting local installation id; exiting!\n" );
      exit( -1 );
   }

/* Look up modules of interest
   ***************************/
   if ( GetModId( MyModName, &MyModId ) != 0 ) {
      fprintf( stderr, 
              "tcpd: Invalid module name <%s>; exiting!\n", MyModName );
      exit( -1 );
   }

/* Look up message types of interest
   *********************************/
   if ( GetType( "TYPE_HEARTBEAT", &TypeHeartBeat ) != 0 ) {
      fprintf( stderr, 
              "tcpd: Invalid message type <TYPE_HEARTBEAT>; exiting!\n" );
      exit( -1 );
   }
   if ( GetType( "TYPE_ERROR", &TypeError ) != 0 ) {
      fprintf( stderr, 
              "tcpd: Invalid message type <TYPE_ERROR>; exiting!\n" );
      exit( -1 );
   }
   if ( GetType( "TYPE_EEW", &TypeEEW ) != 0 ) {
      printf("\n --->Please define Message Type: TYPE_EEW in Earthworm.d. \n\n");
      return -2;
   }
   if ( GetType( "Type_EEW_record", &Type_EEW_record ) != 0 ) {
      printf("\n --->Please define Message Type: Type_EEW_record in Earthworm.d. \n\n");
      return -3;
   }   
   
   return 0;
} 

/******************************************************************************
 * tcpd_status() builds a heartbeat or error message & puts it into       *
 *                   shared memory.  Writes errors to log file & screen.      *
 ******************************************************************************/
void tcpd_status( unsigned char type, short ierr, char *note )
{
   MSG_LOGO    logo;
   char        msg[256];
   long        size;
   time_t        t;
 
/* Build the message
 *******************/ 
   logo.instid = InstId;
   logo.mod    = MyModId;
   logo.type   = type;

   time( &t );

   if( type == TypeHeartBeat )
   {
        sprintf( msg, "%ld %ld\n", (long) t, (long) myPid);
   }
   else if( type == TypeError )
   {
        sprintf( msg, "%ld %hd %s\n", (long) t, ierr, note);
        logit( "et", "tcpd: %s\n", note );
   }

   size = strlen( msg );   /* don't include the null byte in the message */     

/* Write the message to shared memory
 ************************************/
   if( tport_putmsg( &Region, &logo, size, msg ) != PUT_OK )
   {
        if( type == TypeHeartBeat ) {
           logit("et","tcpd:  Error sending heartbeat.\n" );
        }
        else if( type == TypeError ) {
           logit("et","tcpd:  Error sending error:%d.\n", ierr );
        }
   }

   return;
}



//----- Professor Yih-Min Wu Subroutine from here

void locaeq(PEEW *ptr,int nsta, HYP *hyp)
{   
  double x[4][1000],y[4],b[4][4],a[4][4],c[4],trv[1000],dist[1000],ngap[1000];
  //double perr[1000];
  int itr,i,j,k, kk, dd;
  double v0=5.75,vg=0.013,dis=0.0,pi=3.141592654,xc,zc,ang1,ang2;
  double dlat,dlon,errsum=0.0;
  double ptime;		// P-wave travel time
  double stime;		// S-wave travel time  
  double x0,y0,z0,t0,x_ini,y_ini,z_ini,t_ini;
  int iq=0;
  double ave_parr;
  double avwt=0.0;
  double sum2=0.0; 
  double gap;

  double sum_Parr;
  double cc_parr;
  double dd_err[10], min_err;
  
  // double sum_wei;
   
   	
  hyp->gap    = 0.0;
  hyp->xla0   = 0.0;
  hyp->xlo0   = 0.0;
  hyp->depth0 = 0.0;
  hyp->time0  = 0.0;
  hyp->Q      = 0;
  hyp->averr  = 0.0;    
  hyp->avwei = 0.0;	
	
  z0=10.0;
  ave_parr=0.0;
  sum_Parr=0.0;
  cc_parr=0.0;
  for(i=0;i<nsta;i++)        //To calculate P-wave arrivals in average
  {
	if(ptr[i].stn_Comp[2]=='Z') // Confirm if it comes from P-wave
	{
		sum_Parr += ptr[i].P;
		cc_parr++;
	}
    ptr[i].wei=1.0;           //weiting
    ptr[i].perr=0.0;
  }
    ave_parr=sum_Parr/cc_parr;   
  
  j=0;
  t0=0.0;
  x0=0.0;
  y0=0.0;
  for(i=0;i<nsta;i++)
  {
	if(ptr[i].stn_Comp[2]=='Z')
		if(ptr[i].P<(ave_parr-0.5)) 
		{      
			t0=t0+ptr[i].P;		   
			x0=x0+ptr[i].longitude;
			y0=y0+ptr[i].latitude; 
			j++; 
		}
  }
  
  if(j>0){                           
    t0=t0/(1.0*j)-2.0;               
    x0=x0/(1.0*j);
    y0=y0/(1.0*j);
  } 
  else                  
  {
    z0=10.0; 			 
    t0=ptr[0].P;
    x0=ptr[0].longitude;
    y0=ptr[0].latitude;

    for(i=0;i<nsta;i++)
    {
		if(ptr[i].stn_Comp[2]=='Z')
		{
			ptr[i].wei=1.0;
			if(ptr[i].P < t0)   
			{       
				t0=ptr[i].P-2.0;         
				x0=ptr[i].longitude+0.01;   
				y0=ptr[i].latitude+0.01;
			}
		}
    }
  }
      
  t_ini=t0;		
  x_ini=x0;
  y_ini=y0;
  z_ini=z0;
  
  dlat=delaz(y0-0.5,x0,y0+0.5,x0);	//convert degree to kilometer
  dlon=delaz(y0,x0-0.5,y0,x0+0.5);
  
  
  //-- location iteration for 10 times   
  for(itr=0;itr<10;itr++)
  {
            errsum=0.0;

            for(i=0;i<nsta;i++)
            {
	        
	        	if(z0 < Boundary_P) {		// 35.0
	        		v0=SwP_V;		        // 4.5
	        		vg=SwP_VG;	        // 0.085
	        	}
	        	else {
	        		v0=DpP_V;			// 6.0
	        		vg=DpP_VG;		        // 0.023
	        	}	
	        
	        	if(ptr[i].stn_Comp[2]=='N')
	        	{		
					if(z0 < Boundary_S) {		// 35.0
						v0=SwS_V;		        // 4.5
						vg=SwS_VG;	        // 0.085
					}
					else {
						v0=DpS_V;			// 6.0
						vg=DpS_VG;		        // 0.023
					}	
	        	}
	        
                dis=sqrt( (ptr[i].latitude-y0) *dlat*(ptr[i].latitude-y0) *dlat + 
                          (ptr[i].longitude-x0)*dlon*(ptr[i].longitude-x0)*dlon +0.000000001 );
                xc=(dis*dis-2.*v0/vg*z0-z0*z0) /(2. * dis);
                zc=-1.*v0/vg;
                ang1=atan((z0-zc)/xc);
                if(ang1 <0.0)ang1=ang1+pi;  
                ang1=pi-ang1; 		
                ang2=atan(-1.*zc / (dis-xc) );	
                ptime=(-1./vg)*log(fabs(tan(ang2/2.)/tan(ang1/2.)));	//travel time
           
                x[0][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].longitude-x0)*dlon/dis)); //spatial derivative of T
                x[1][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].latitude-y0)*dlat/dis));	 //spatial derivative of T
                x[2][i]=(-1.*cos(ang1)/(v0+vg*z0));		   						     //spatial derivative of T
                x[3][i]=1.0;		
                trv[i]=ptime;
                dist[i]=dis;
                ptr[i].perr=ptr[i].P-t0-ptime;		

				
				
               // errsum=errsum+fabs(ptr[i].perr);	  
				//errsum += (ptr[i].perr)*(ptr[i].perr);
				
                ptr[i].wei=wt(dist[i],ptr[i].perr,itr,z0,ptr[i].weight);
	        	//if(ptr[i].stn_Comp[2]=='N') ptr[i].wei*=1.5;
	        	//if(ptr[i].stn_Comp[2]=='N') ptr[i].perr *= 0.5;
            }
	        kk=nsta;
	        for(i=0;i<nsta;i++)
	        {
	        	for(j=0;j<nsta;j++)
	        	{
	        		if(i!=j)
	        		{
	        			x[0][kk]=x[0][i]-x[0][j];
	        			x[1][kk]=x[1][i]-x[1][j];
	        			x[2][kk]=x[2][i]-x[2][j];
	        			x[3][kk]=0.0;
	        		}
	        	}
	        }
	        
           // errsum=errsum/(1.0*(double)nsta);	
		   //errsum = sqrt(errsum/(1.0*(double)nsta));	
            
            avwt=0.0;                           
            sum2=0.0;
            
            for(i=0;i<nsta;i++)
            {
              avwt=avwt+ptr[i].wei;
              sum2=sum2+ptr[i].perr*ptr[i].wei;
            }
            if(avwt > 0.0)
            {
              sum2=sum2/avwt;
              t0=t0+sum2;
            }
            

           
            for(i=0;i<4;i++)
            {
              y[i]=0.0;
              for(j=0;j<4;j++) b[i][j]=0.0;
            }

            avwt=0.0;
            sum2=0.0;            
            for(i=0;i<nsta;i++)
            {
              ptr[i].perr=ptr[i].P-t0-trv[i];
	          //if(ptr[i].stn_Comp[2]=='N') ptr[i].perr *= 0.5;	  
              ptr[i].wei=wt(dist[i],ptr[i].perr,itr,z0,ptr[i].weight);
              avwt=avwt+ptr[i].wei;
              sum2=sum2+ptr[i].perr*ptr[i].perr*ptr[i].wei;     
              for(j=0;j<4;j++)y[j]=y[j]+x[j][i]*ptr[i].perr*ptr[i].wei;
              for(j=0;j<4;j++)
                for(k=0;k<4;k++) b[j][k]=b[j][k]+x[j][i]*x[k][i]*ptr[i].wei;
            }
                 
            avwt=avwt/(1.0*nsta);
            sum2=sum2/avwt;
            
            for(i=0;i<4;i++)
            {
              y[i]=y[i]/avwt;
              for(j=0;j<4;j++) b[i][j]=b[i][j]/avwt;
            }
            
            for(i=0;i<4;i++)
            {
              for(j=0;j<4;j++) a[i][j]=b[i][j];
              a[i][i]=b[i][i]+0.00001;
            }
            
            i=4;
            matinv(a,i);        //inverse matrix
           
            for(i=0;i<4;i++)
            {
              sum2=0.0;
              for(j=0;j<4;j++)sum2=sum2+a[i][j]*y[j];
              c[i]=sum2;
            }
            
            x0=x0+c[0]/dlon;
            y0=y0+c[1]/dlat;
            z0=z0+c[2];
            t0=t0+c[3];
            //G_con[itr] = fabs(c[0])+fabs(c[1])+fabs(c[2])+fabs(c[3]);
            //G_x[itr]=fabs(c[0]);
            //G_y[itr]=fabs(c[1]);
            //G_z[itr]=fabs(c[2]);
            //G_t[itr]=fabs(c[3]);
            
            if( (fabs(c[0])+fabs(c[1])+fabs(c[2])+fabs(c[3]) ) < 60.0)
			{
				// printf("Q--- %f \n", (fabs(c[0])+fabs(c[1])+fabs(c[2])+fabs(c[3])) );
				// iq++;   
			}
			else 
			{
				iq--;
				// printf("Q--- %f \n", (fabs(c[0])+fabs(c[1])+fabs(c[2])+fabs(c[3])) );
			}
            
            
              if(fabs(t0-t_ini) > 60.0 || errsum>6.0){        
                t0=t_ini;                                 
                x0=x_ini;
                y0=y_ini;
                z0=z_ini;
               // iq--;
              }
  }	// iteration
  //-------------------------------------------------- Grid Search for Determining Depth
	for(dd=0;dd<10;dd++) dd_err[dd] = -1.0;
//	for(i=0;i<nsta;i++) perr[i] = 0.0;
	for(dd=0;dd<10;dd++)
	{
            //z0 = ((double)dd+1.0)*5.0001 ;
			//if(dd>5) z0 += ((double)dd-5.0)*10.0001 ;
			z0 = ((double)dd+1.0)*10.0001 ;
			
           
            for(i=0;i<nsta;i++)
            {
	        
	        	if(z0 < Boundary_P) {		// 35.0
	        		v0=SwP_V;		        // 4.5
	        		vg=SwP_VG;	        // 0.085
	        	}
	        	else {
	        		v0=DpP_V;			// 6.0
	        		vg=DpP_VG;		        // 0.023
	        	}	
	        
	        	if(ptr[i].stn_Comp[2]=='N')
	        	{		
					if(z0 < Boundary_S) {		// 35.0
						v0=SwS_V;		        // 4.5
						vg=SwS_VG;	        // 0.085
					}
					else {
						v0=DpS_V;			// 6.0
						vg=DpS_VG;		        // 0.023
					}	
	        	}
	        
                dis=sqrt( (ptr[i].latitude-y0) *dlat*(ptr[i].latitude-y0) *dlat + 
                          (ptr[i].longitude-x0)*dlon*(ptr[i].longitude-x0)*dlon +0.000000001 );
                xc=(dis*dis-2.*v0/vg*z0-z0*z0) /(2. * dis);
                zc=-1.*v0/vg;
                ang1=atan((z0-zc)/xc);
                if(ang1 <0.0)ang1=ang1+pi;  
                ang1=pi-ang1; 		
                ang2=atan(-1.*zc / (dis-xc) );	
                ptime=(-1./vg)*log(fabs(tan(ang2/2.)/tan(ang1/2.)));	//travel time
           
                x[0][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].longitude-x0)*dlon/dis)); //spatial derivative of T
                x[1][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].latitude-y0)*dlat/dis));	 //spatial derivative of T
                x[2][i]=(-1.*cos(ang1)/(v0+vg*z0));		   						     //spatial derivative of T
                x[3][i]=1.0;		
                trv[i]=ptime;
                dist[i]=dis;
                //perr[i]=ptr[i].P-t0-ptime;		 
                ptr[i].perr=ptr[i].P-t0-ptime;					
				//ptr[i].wei=wt(dist[i],ptr[i].perr,itr,z0,ptr[i].weight);	
				
                dd_err[dd] += ptr[i].wei*fabs(ptr[i].perr);	        
            }  
    }
	min_err = 999999.0;
    for(dd=0;dd<10;dd++)
	{
		if ( min_err > fabs(dd_err[dd]) ) 
		{
			min_err = fabs(dd_err[dd]);
            //z0 = ((double)dd+1.0)*5.0001 ;
			//if(dd>5) z0 += ((double)dd-5.0)*10.0001 ;
			z0 = ((double)dd+1.0)*10.0001 ;			
		}
	}
  //------------------------------------Calculate relative parameters one more by correct depth
		

           avwt = 0.0;
		   errsum = 0.0;	
		   
            for(i=0;i<nsta;i++)
            {
			
                dis=sqrt( (ptr[i].latitude-y0) *dlat*(ptr[i].latitude-y0) *dlat + 
                          (ptr[i].longitude-x0)*dlon*(ptr[i].longitude-x0)*dlon +0.000000001 );			
			
 			    //--------------- For S-wave	
					if(z0 < Boundary_S) {		// 35.0
						v0=SwS_V;		        // 4.5
						vg=SwS_VG;	        // 0.085
					}
					else {
						v0=DpS_V;			// 6.0
						vg=DpS_VG;		        // 0.023
					}					
                xc=(dis*dis-2.*v0/vg*z0-z0*z0) /(2. * dis);
                zc=-1.*v0/vg;
                ang1=atan((z0-zc)/xc);
                if(ang1 <0.0)ang1=ang1+pi;  
                ang1=pi-ang1; 		
                ang2=atan(-1.*zc / (dis-xc) );	
                stime=(-1./vg)*log(fabs(tan(ang2/2.)/tan(ang1/2.)));	// S-wave travel time
				
 			    //--------------- For P-wave	        
	        	if(z0 < Boundary_P) {		// 35.0
	        		v0=SwP_V;		        // 4.5
	        		vg=SwP_VG;	        // 0.085
	        	}
	        	else {
	        		v0=DpP_V;			// 6.0
	        		vg=DpP_VG;		        // 0.023
	        	}	
	        
	        	if(ptr[i].stn_Comp[2]=='N')
	        	{		
					if(z0 < Boundary_S) {		// 35.0
						v0=SwS_V;		        // 4.5
						vg=SwS_VG;	        // 0.085
					}
					else {
						v0=DpS_V;			// 6.0
						vg=DpS_VG;		        // 0.023
					}	
	        	}
				
                xc=(dis*dis-2.*v0/vg*z0-z0*z0) /(2. * dis);
                zc=-1.*v0/vg;
                ang1=atan((z0-zc)/xc);
                if(ang1 <0.0)ang1=ang1+pi;  
                ang1=pi-ang1; 		
                ang2=atan(-1.*zc / (dis-xc) );	
                ptime=(-1./vg)*log(fabs(tan(ang2/2.)/tan(ang1/2.)));	// P-wave travel time
          
		  
				ptr[i].P_S_time = stime - ptime;
		  
		  
		  	if(ptr[i].P_S_time<0)
			{
				faa=fopen("qwer.txt","a");
				fprintf(faa,"-------------%s %s %s %s - %f \n"
							,ptr[i].stn_name
							,ptr[i].stn_Comp
							,ptr[i].stn_Net
							,ptr[i].stn_Loc
							,ptr[i].P_S_time );
				fclose(faa);
			}
		  
                x[0][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].longitude-x0)*dlon/dis)); //spatial derivative of T
                x[1][i]=(-1.*sin(ang1)/(v0+vg*z0)*((ptr[i].latitude-y0)*dlat/dis));	 //spatial derivative of T
                x[2][i]=(-1.*cos(ang1)/(v0+vg*z0));		   						     //spatial derivative of T
                x[3][i]=1.0;		
                trv[i]=ptime;
                dist[i]=dis;
                ptr[i].perr=ptr[i].P-t0-ptime;	
				ptr[i].wei=wt(dist[i],ptr[i].perr,itr,z0,ptr[i].weight);
				avwt += (ptr[i].wei)*(ptr[i].wei);	
				
               // errsum=errsum+fabs(ptr[i].perr);	  
				errsum += (ptr[i].perr)*(ptr[i].perr);				
			    //if(ptr[i].stn_Comp[2]=='N') ptr[i].perr *= 0.001;				
            }
		    errsum = sqrt( errsum/((double)nsta) );
			avwt   = sqrt( avwt/((double)nsta)   );
  //------------------------------------------------------------------------
  gap=0.0;
  for(i=0;i<nsta;i++)
  {
                ngap[i] = atan2( ptr[i].longitude-x0, ptr[i].latitude-y0 ) / DEGRAD;
  }
  qsort(ngap, nsta, sizeof(ngap[0]), hyp_cmp);
  ngap[nsta] = ngap[0] + 360.0;
  for(i=0; i<nsta; i++) 
  {
                if(ngap[i+1] - ngap[i] > gap)
                        gap = ngap[i+1] - ngap[i];
  }  
  
  //  if(z0 < 5.0)  z0=5.0;
  
  hyp->gap    = gap;
  hyp->xla0   = y0;
  hyp->xlo0   = x0;
  hyp->depth0 = z0;
  hyp->time0  = t0;
  hyp->Q      = iq;
  hyp->averr  = errsum;    
  hyp->avwei = avwt;
  
} //-- end locaeq

/*
 *  hyp_cmp()  Compare values of two variables
 */
int hyp_cmp(const void *x1, const void *x2)
{
        if(*(double *)x1 < *(double *)x2)   return -1;
        if(*(double *)x1 > *(double *)x2)   return 1;
        return 0;
}

//--------- function for calculate weight ---------------------------
double wt(double dist,double res,int iter, double depth, double weight)
{
      double xnear=100.0,xfar=600.0;
      double tres=1.0;
      double xwt=1.0;
      double H_dist=0.0;
      
      H_dist = sqrt(depth*depth+ dist*dist +0.000001 );
      
      if(H_dist>xnear)
    		  xwt=xwt*(xfar-xnear)/(9.*H_dist+xfar-10.*xnear);
      xwt=xwt*(tres/(tres+fabs(res)))*(tres/(tres+fabs(res)));
	 // xwt=xwt*(tres/(tres+fabs(res)))*(tres/(tres+log10(dist)))*(tres/(tres+weight));
      return xwt;
}
//--------------------------------------------------------------------

//---------------------------------------------------------------
//       find matrix inverse
//---------------------------------------------------------------
void  matinv(double a[4][4],int n)
{
   double v[4];
   int nm1,i,j,ii,k;
   double pivot,yy;
   nm1 = n - 1;

   for(k=0;k<n;k++) {   
     pivot = 1.0/a[0][0];
     for(i=1;i<n;i++)v[i-1]=a[0][i];
     for(i=0;i<nm1;i++) {
       yy=-1.0*v[i]*pivot;
       a[i][n-1]=yy;
       for(j=0;j<nm1;j++)a[i][j]=a[i+1][j+1]+v[j]*yy;
     }
     a[n-1][n-1]=-1.*pivot; 
   }
   
   for(i=0;i<n;i++)
     for(j=0;j<n;j++) a[i][j]=-1.*a[i][j];
   
   for(i=1;i<n;i++){
     ii=i-1;
     for(j=0;j<=ii;j++) a[i][j]=a[j][i];
   } 

}

//C-------- This sub program change Lon. Lat. to Km unit ------------
double delaz(double elat,double elon,double slat, double slon)
{
  double  delta;
  double avlat,a,b,dlat,dlon,dx,dy;

  avlat=0.5*(elat+slat);
  a=1.840708+avlat*(.0015269+avlat*(-.00034+avlat*(1.02337e-6)));
  b=1.843404+avlat*(-6.93799e-5+avlat*(8.79993e-6+avlat*(-6.47527e-8)));
  dlat=slat-elat;
  dlon=slon-elon;
  dx=a*dlon*60.0;
  dy=b*dlat*60.0;
  delta=sqrt(dx*dx+dy*dy);

  return delta;
}

// processTriger return 1 ---> reset, all ptr[].flag will be set into 0 

void processTrigger( int vsn_trigger, PEEW *vsn_ntri)
{	
      int i,j, k, jj;
      int  n_trigger=0;
         
      HYP hyp;
      PEEW  new_ntri[number] ;         
      
      double dif;

      MAG mag;
      double pPgv;
      int ntc;   
	  double intersta_dis;
           
      int QQ;
	  int flag_m;
      
	  
      //--  Earthquake Location
	
		locaeq(vsn_ntri,vsn_trigger,&hyp);
		// printf("------------ aaa1 \n");
		// locaeq_grid(vsn_ntri,vsn_trigger,&hyp);
		// printf("------------ aaa2 \n");   
   
      	while(1)
      	{            

			if(hyp.averr <= 0.8 && hyp.averr >= 0.1 && hyp.avwei >= 0.3 )	break;

 	
        		dif=0.0;
        		QQ=-1; 
        		for(i=0;i<vsn_trigger;i++) 
				{
         			if(vsn_ntri[i].flag>0 )
            				if(fabs(vsn_ntri[i].perr) > dif)
            				{       
            					dif = fabs(vsn_ntri[i].perr);
            					QQ=i;                					                              	
            				}  
				}
				if(QQ < 0)
				{
        			printf("--------ERROR----------QQ < 0\n");
        			return;
        		}
				               
          		vsn_ntri[QQ].flag=0;   		
				
				// printf("BYE*******  %s %s %s %s  ********* \n"
					// ,vsn_ntri[QQ].stn_name,vsn_ntri[QQ].stn_Loc,vsn_ntri[QQ].stn_Comp,vsn_ntri[QQ].stn_Net);

				
          		n_trigger=0;
				for(k=0;k<vsn_trigger;k++)
				{            
					if(vsn_ntri[k].flag>0)
					{	        
	  	                new_ntri[n_trigger].flag             =vsn_ntri[k].flag;
        		        new_ntri[n_trigger].latitude         =vsn_ntri[k].latitude;
        		        new_ntri[n_trigger].longitude        =vsn_ntri[k].longitude;
        		        new_ntri[n_trigger].P                =vsn_ntri[k].P;      		
        		        new_ntri[n_trigger].Tc               =vsn_ntri[k].Tc;
					   for(jj=2;jj<=vsn_ntri[k].upd_sec;jj++)
						new_ntri[n_trigger].Pd[jj]       	 =vsn_ntri[k].Pd[jj];						
        		        new_ntri[n_trigger].Pa               =vsn_ntri[k].Pa;
        		        new_ntri[n_trigger].Pv               =vsn_ntri[k].Pv;
        		        new_ntri[n_trigger].report_time      =vsn_ntri[k].report_time;
        		        new_ntri[n_trigger].npoints          =vsn_ntri[k].npoints;
						new_ntri[n_trigger].weight		     =vsn_ntri[k].weight;
						new_ntri[n_trigger].inst 		     =vsn_ntri[k].inst;
						new_ntri[n_trigger].P_S_time		 =vsn_ntri[k].P_S_time;
							
						new_ntri[n_trigger].perr 		     =vsn_ntri[k].perr;
						new_ntri[n_trigger].wei 		   	 =vsn_ntri[k].wei;								  
						new_ntri[n_trigger].upd_sec		     =vsn_ntri[k].upd_sec;
																		
  		                sprintf(new_ntri[n_trigger].stn_name ,"%s",vsn_ntri[k].stn_name);
                        sprintf(new_ntri[n_trigger].stn_Loc  ,"%s",vsn_ntri[k].stn_Loc);
                        sprintf(new_ntri[n_trigger].stn_Comp ,"%s",vsn_ntri[k].stn_Comp);   		           
                        sprintf(new_ntri[n_trigger].stn_Net  ,"%s",vsn_ntri[k].stn_Net);	  
                                          				  
						n_trigger++;                                                                                   						  					   			
					}
				}
				if(n_trigger < 5 )
				{
					printf("##### No REPORT ##### n_trigger < 3   -------\n averr: %.2f , gap: %.0f, Q: %d\n"
        			 	, hyp.averr, hyp.gap, hyp.Q); 
     				 
      				 return;
				}	  			
				locaeq(new_ntri,n_trigger,&hyp); 
				// locaeq_grid(new_ntri,n_trigger,&hyp); 
				// printf("locaeq  ---> n_trigger: %d , averr:%.2f , avwei:%.2f , Q: %d\n"
					// , n_trigger, hyp.averr, hyp.avwei, hyp.Q);
   					 				   				      			
				// if(hyp.averr <= 0.8 && hyp.averr >= 0.1 && hyp.avwei >= 0.3 && hyp.Q > -10 )	break;
				
				
				
    	} // end of while



//------------------------------------------------------------------ Number of Non-co-site stations
	G_sta_num=0;
	for(j=0;j<n_trigger;j++)
	{
	  if( new_ntri[j].flag>0 )
	  {
	    flag_m = 0;
		for(i=0;i<n_trigger;i++)
		{
			if( i!=j && new_ntri[i].flag>0)
			{
				intersta_dis =delaz(new_ntri[i].latitude ,new_ntri[i].longitude ,new_ntri[j].latitude ,new_ntri[j].longitude);
				if(intersta_dis<0.5) {new_ntri[j].flag=0; flag_m=1; break;}
			}
		}
	    if(flag_m==0)
			G_sta_num++;	  
	  }
	}
	
//-------------------------------------------------------------------------------------------
	G_Q = hyp.Q;        
    G_averr = hyp.averr;
	G_n = n_trigger;	
	GAP = hyp.gap; 
	
	if(G_sta_num < 5)
	{
		printf("##### No REPORT ##### G_sta_num < 4 , G_sta_num: %d , n_trigger: %d\n"
				,G_sta_num, n_trigger);
		return;
	}
	//------------------------------------- co-site number
	if(G_sta_num <= pre_G_sta_num)
	{
		printf("##### No REPORT #####  Number of co-site stations less than previous! , a->%d : b->%d \n"
				, G_sta_num, pre_G_sta_num);
		return;
	}		
	
    printf("--- Running MAgnitude() , n_trigger: %d \n", n_trigger);
	Magnitude(new_ntri,n_trigger, hyp, &mag, &pPgv, &ntc);

		if( mag.xMpd > MagMax || mag.xMpd < MagMin )
		{
      			printf("##### No REPORT ##### mag.xMpd = %.2f  \n", mag.xMpd);
        		return; 
        }          
	
	
	//------------------------------------- number	
	// if(G_n <= pre_G_n)
	// {
		// printf("##### No REPORT #####  Number of stations less than previous! \n");
		// return;
	// }	
//	//------------------------------------- mag number
//	if(G_n_mag <= pre_G_n_mag)
//	{
//		printf("##### No REPORT #####  Number of stations for magnitude less than previous! , a->%d : b->%d \n"
//				, G_n_mag, pre_G_n_mag);
//		return;
//	}		
//	
//	//------------------------------------- GAP	
//   if( (GAP - pre_GAP)>0 )
//	if( pre_GAP>0.0 && fabs(GAP - pre_GAP)>100.0  )
//	{
//		printf("##### No REPORT #####  GAP greater than previous! %f %f \n", GAP, pre_GAP);
//		return;
//	}		
	
	Report_seq( new_ntri, n_trigger, hyp, mag, ntc);    	
	 	
}


void Magnitude( PEEW *vsn_ntri, int qtrigger, HYP hyp, MAG *mag, double *pPgv, int *ntc)
{
      int nntc=0, i, k, j, l, vv=0, dd=0, tt=0, bb=0;      
      double dis, ddis, z, sum_wei;
	  int s_num=0;

      stat  mpd, mtc, mall;
      
      MAG_DATA  *Mpd, *Mtc;
	  MAG_DATA  *zMpd, *zMtc;
	  
	  double *Theo_pa_ratio, Padj, Theo_pa_ratio_avg, Theo_pa_ratio_std;
	  double *Padj_wei, Padj_sum_wei;
	  double *zTheo_pa_ratio, *zPadj_wei;
	  
	  int ind;
	  // int choose;
	  // double diff = 10000.0, discrepancy;
      

	  // printf("-----------1-1\n");
	  Mpd = (MAG_DATA*) malloc(1000*sizeof(MAG_DATA));
	  Mtc = (MAG_DATA*) malloc(1000*sizeof(MAG_DATA));
	  
	  zMpd = (MAG_DATA*) malloc(1000*sizeof(MAG_DATA));
	  zMtc = (MAG_DATA*) malloc(1000*sizeof(MAG_DATA));	  
	  
      Ini_MAG( mag);
	  	  
      Ini_stat( &mpd);
      Ini_stat( &mtc);	
      Ini_stat( &mall);      

      for(i=0;i<num_mag_reject;i++)
      {			  
            for(k=0;k<qtrigger;k++) 
			 if(vsn_ntri[k].flag==1)
			  if(i!=k)
				if( !strcmp( MagReject[i].sta, vsn_ntri[k].stn_name ) && 
					!strcmp( MagReject[i].chn, vsn_ntri[k].stn_Comp ) &&
					!strcmp( MagReject[i].net, vsn_ntri[k].stn_Net  ) &&
					!strcmp( MagReject[i].loc, vsn_ntri[k].stn_Loc  )                                 	                                      	    
				  )    
                 	vsn_ntri[k].flag = 2; // it means we do not use this station for magnitude estimation              	
      }
	  for(k=0;k<qtrigger;k++)
	  {
            //if(vsn_ntri[k].inst==3 ) continue;
            //if(vsn_ntri[k].stn_Loc[1] =='2') continue; 
		    if( !strncmp(vsn_ntri[k].stn_Net,"BH",2) ) vsn_ntri[k].flag = 2; 			
		    if(vsn_ntri[k].stn_Comp[2] !='Z')		   vsn_ntri[k].flag = 2;
			if(vsn_ntri[k].Tc < 0.0) 				   vsn_ntri[k].flag = 2;
			if(vsn_ntri[k].P_S_time < 2.0) 			   vsn_ntri[k].flag = 2;	
			if(vsn_ntri[k].upd_sec < 1.999) 	       vsn_ntri[k].flag = 2;		
	  }
	    
      j=0;
      l=0;	
      nntc=0; 
      vv=0;
      dd=0;
      tt=0;

      for(k=0;k<qtrigger;k++)
		if(vsn_ntri[k].flag==1)
        {		
			nntc++; 
			
			// Pd[0] is the sutible Pd for magnitude estimation
			// according to P_S_time
			// Avoid the S Wave pollute the Pd
			vsn_ntri[k].usd_sec = 0;
			if(vsn_ntri[k].P_S_time-vsn_ntri[k].upd_sec < 0)
			{
			   ind = (int) ( floor(vsn_ntri[k].P_S_time) );
			   vsn_ntri[k].usd_sec = ind;
			   vsn_ntri[k].Pd[0] = vsn_ntri[k].Pd[ind];
			}
			else
			{
			   ind = vsn_ntri[k].upd_sec;
			   vsn_ntri[k].usd_sec = ind;			   
			   vsn_ntri[k].Pd[0] = vsn_ntri[k].Pd[ind];			
			}			
	     	
            dis=delaz(vsn_ntri[k].latitude, vsn_ntri[k].longitude, hyp.xla0, hyp.xlo0);
            ddis = sqrt(hyp.depth0*hyp.depth0+ dis*dis +0.000001 );   

  

//------------------------------------------------------------ Mpd
            if(vsn_ntri[k].Pd[0] > 0.0)
            {
            	if(vsn_ntri[k].inst==2 )
            	{	
					Mpd[dd].mag     = Mpd1_HH(vsn_ntri[k].Pd[0], ddis ); 
					Mpd[dd].wei		= vsn_ntri[k].wei;            	
            		dd++;
            	}    
            	if(vsn_ntri[k].inst==1 )
            	{	
					Mpd[dd].mag		= Mpd1_HL(vsn_ntri[k].Pd[0], ddis ); 
					Mpd[dd].wei		= vsn_ntri[k].wei;           	
            		dd++;
            	}
            	if(vsn_ntri[k].inst==3 )	
            	{	
					Mpd[dd].mag     = Mpd1_HS(vsn_ntri[k].Pd[0], ddis ); 
					Mpd[dd].wei		= vsn_ntri[k].wei;          	
            		dd++;
            	}  				
            }
//------------------------------------------------------------ Mtc			
            if(vsn_ntri[k].Tc > 0.0)
            {
				Mtc[tt].mag  = cal_Tc(vsn_ntri[k].Tc);
				Mtc[tt].wei  = vsn_ntri[k].wei;	
								
            	tt++;	
            }						
			// printf("-----------w1, qtrigger: %d , k: %d \n", qtrigger, k);
        }       
        if(dd <= 3)
        {      
			mag->xMpd =0.0;
		
        	// printf("magnitude: Number < 1, return ! , dd= %d\n", dd);

			mag->mtc  = 0.0;
			mag->ALL_Mag = 0.0;
			G_n_mag = 0;
			
			free(Mpd);  free(Mtc);
			free(zMpd); free(zMtc);			
			
        	 return;       
        }		
        if(nntc < 1)
        {              
        	// printf("magnitude: Number < 1, return ! \n");
			mag->xMpd = 0.0;
			mag->mtc  = 0.0;
			mag->ALL_Mag = 0.0;
			G_n_mag = 0;
			
			free(Mpd);  free(Mtc);
			free(zMpd); free(zMtc);			
			
        	 return;       
        }

		
	cal_avg_std_mag(Mpd,      dd, &mpd.avg, &mpd.std);
	cal_avg_std_mag(Mtc,      tt, &mtc.avg, &mtc.std);			
	

//----------------------------------------------------------- Mpd
	sum_wei	= 0.0;	
	mpd.new_num=0;
	for(i=0;i<dd;i++)
	{               		
		z = cal_z(Mpd[i].mag, mpd.avg, mpd.std);
		if(z < 1.0 && Mpd[i].mag>0)
		{		
			zMpd[mpd.new_num].mag = Mpd[i].mag;
			zMpd[mpd.new_num].wei = Mpd[i].wei;
			sum_wei += zMpd[mpd.new_num].wei;			
			
			mpd.new_num++;
		}
	}
	G_n_mag = mpd.new_num;
	
	mag->xMpd = 0.0;
	for(i=0;i<mpd.new_num;i++)
		mag->xMpd += (zMpd[i].wei/sum_wei)*zMpd[i].mag;

//----------------------------------------------------------- Mtc
	sum_wei	= 0.0;
	mtc.new_num = 0;
			// printf("-----------a5 \n");
	for(i=0;i<tt;i++)
	{               		
		z = cal_z(Mtc[i].mag, mtc.avg, mtc.std);
			
		if(z < 1.0 && Mtc[i].mag >0)
		{
		
			zMtc[mtc.new_num].mag = Mtc[i].mag;
			zMtc[mtc.new_num].wei = Mtc[i].wei;
			sum_wei += zMtc[mtc.new_num].wei;			
		
			mtc.new_num++;
		}
	}
		// printf("-----------a6 \n");
	mag->mtc = 0.0;
	for(i=0;i<mtc.new_num;i++)
		mag->mtc += (zMtc[i].wei/sum_wei)*zMtc[i].mag;


	
//----------------------------------------------------------- ALL_Mag
	
        if(  mag->xMpd < 0.01) 
		{
			mag->ALL_Mag = 0.0;			
			mag->xMpd = 0.0; 
			G_n_mag = 0;
		}
	
		if(mag->mtc>0.001 && mag->xMpd>0.001) 
			mag->ALL_Mag =  (mag->mtc + mag->xMpd)/2.0;
		else mag->ALL_Mag =  mag->xMpd;
		
		
        *ntc=nntc;
        // printf("--- Earthquake Report --- Sta. Number: %d \n", nntc);  

		if(mpd.new_num==0)
		{
			// printf("mpd.new_num = 0 \n");
			mag->ALL_Mag = 0.0;			
			mag->xMpd = 0.0; 	
			G_n_mag = 0;			
		}
		

      free(Mpd);  free(Mtc);
	  free(zMpd); free(zMtc);	

	//-------------------   For Padj
	// printf("--------e1\n");
	
	Theo_pa_ratio = (double*) malloc(1000*sizeof(double));
	Padj_wei = (double*) malloc(1000*sizeof(double));
	
	zTheo_pa_ratio = (double*) malloc(1000*sizeof(double));
	zPadj_wei = (double*) malloc(1000*sizeof(double));	
//------------------------------------------------------------ pa--->Padj
	dd=0;
   for(k=0;k<qtrigger;k++)
    if(vsn_ntri[k].flag==1)
	{
	
	
        dis=delaz(vsn_ntri[k].latitude, vsn_ntri[k].longitude, hyp.xla0, hyp.xlo0);
        ddis = sqrt(hyp.depth0*hyp.depth0+ dis*dis +0.000001 );	
        if(vsn_ntri[k].Pa > 0.0)
        {
        	if(vsn_ntri[k].inst==2 )
        	{	
				Theo_pa_ratio[dd] = vsn_ntri[k].Pa / pa_HH(mag->xMpd, ddis);
				Padj_wei[dd] = vsn_ntri[k].wei; 
				
				// Mpd[dd].mag     = Mpd1_HH(vsn_ntri[k].Pd, ddis ); 
				// Mpd[dd].wei		= vsn_ntri[k].wei;            	
        		dd++;
        	}    
        	if(vsn_ntri[k].inst==1 )
        	{	
				Theo_pa_ratio[dd] = vsn_ntri[k].Pa / pa_HL(mag->xMpd, ddis);
				Padj_wei[dd] = vsn_ntri[k].wei; 			
			
				// Mpd[dd].mag		= Mpd1_HL(vsn_ntri[k].Pd, ddis ); 
				// Mpd[dd].wei		= vsn_ntri[k].wei;           	
        		dd++;
        	}
        	if(vsn_ntri[k].inst==3 )	
        	{	
				Theo_pa_ratio[dd] = vsn_ntri[k].Pa / pa_HS(mag->xMpd, ddis);
				Padj_wei[dd] = vsn_ntri[k].wei; 			
			
				// Mpd[dd].mag     = Mpd1_HS(vsn_ntri[k].Pd, ddis ); 
				// Mpd[dd].wei		= vsn_ntri[k].wei;          	
        		dd++;
        	}  				
        }	
	}
	// printf("--------e2\n");

	cal_avg_std1(Theo_pa_ratio, dd, &Theo_pa_ratio_avg, &Theo_pa_ratio_std);
	
		// printf("--------w1\n");
	Padj_sum_wei=0.0;	
	bb=0;
	for(i=0;i<dd;i++)
	{               		
		z = cal_z(Theo_pa_ratio[i], Theo_pa_ratio_avg, Theo_pa_ratio_std);
		if(z < 1.0 && Theo_pa_ratio[i]>0)
		{		
			zTheo_pa_ratio[bb] = Theo_pa_ratio[i];
			zPadj_wei[bb] = Padj_wei[i];
			Padj_sum_wei += zPadj_wei[bb];						
			bb++;
		}
	}	

			// printf("--------w3\n");
	Padj = 0.0;
	for(i=0;i<bb;i++)
		Padj += (zPadj_wei[i]/Padj_sum_wei)*zTheo_pa_ratio[i];
			// printf("--------e3\n");
	free(Theo_pa_ratio);
	free(Padj_wei);	
	free(zTheo_pa_ratio);
	free(zPadj_wei);		
	// printf("--------e4\n");	
	mag->Padj = Padj;
	
}

void Report_seq( PEEW *vsn_ntri, int qtrigger, HYP hyp, MAG mag, int ntc)
{
    int k;
    time_t tp;      

    struct tm *p;
    double t_now;	
    
    double dis; 
    double pro_time;
    
    int ccount;
    int cc;
    
    struct timeb tpnow;
    int fyr,fmo,fdy,fhr,fmn; 
  	double fsec;

    int fyr1,fmo1,fdy1,fhr1,fmn1;
  	double fsec1;

    FILE *new_Event_File;    
          
    double tmp, sec1, ddis;
	 
	// char sentTime[30], oriTime[30];
	char outmsg[500];
                                   
    char ss[80];  
	char otime[300], ntime[300], Ctmp[30];
	
	char neweventfile[200];  // report filename	
		

    datestr23 (hyp.time0,           otime, 256);  //origin time of the earthquake

    sprintf(Ctmp,"%c%c%c%c",otime[0],otime[1],otime[2],otime[3]);
	fyr1=atoi(Ctmp);		
	sprintf(Ctmp,"%c%c",otime[5],otime[6]);
	fmo1=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",otime[8],otime[9]);
	fdy1=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",otime[11],otime[12]);
	fhr1=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",otime[14],otime[15]);
	fmn1=atoi(Ctmp);
	sprintf(Ctmp,"%c%c%c%c%c",otime[17],otime[18],otime[19],otime[20],otime[21]);
	fsec1=atof(Ctmp);	
        
    ftime(&tpnow);                                        	
    t_now  = (double)tpnow.time + (double)tpnow.millitm/1000;        
    sec1=t_now-hyp.time0;    // process time
		 
    datestr23 (t_now,           ntime, 256); 	 
    pro_time = sec1;
                
    if(pro_time>28800) pro_time -=28800;
    proc_time = pro_time;
		
	// if( pre_proc_time>0 )
		// if( fabs(pre_proc_time-proc_time) > 15 )
		// { 
			// printf("##### No REPORT ##### fabs(pre_proc_time-proc_time) > 15 , %d \n"
				// , fabs(pre_proc_time-proc_time));
			// return;
		// }
		 
    if(rep_fin==0) first_rp_time = t_now;
	
	    rep_fin ++;
		ccount = rep_fin;

    	cc = ccount;
    
	
    	if(ccount>Term_num)  // exceeding the Term_num's report will be removed  	
		{
			printf("##### No REPORT ##### ccount>Term_num \n");
			return;
		}
     		     			
 

    sprintf(Ctmp,"%c%c%c%c",ntime[0],ntime[1],ntime[2],ntime[3]);
	fyr=atoi(Ctmp);		
	sprintf(Ctmp,"%c%c",ntime[5],ntime[6]);
	fmo=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",ntime[8],ntime[9]);
	fdy=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",ntime[11],ntime[12]);
	fhr=atoi(Ctmp);
	sprintf(Ctmp,"%c%c",ntime[14],ntime[15]);
	fmn=atoi(Ctmp);
	sprintf(Ctmp,"%c%c%c%c%c",ntime[17],ntime[18],ntime[19],ntime[20],ntime[21]);
	fsec=atof(Ctmp);              
              	
    disp_time1 (fyr, fmo, fdy, fhr, fmn, (int)fsec, ss, 1);
		
		
		
	if(Show_Report==1)	
	{

       sprintf(neweventfile,"%s_n%d.rep",ss,ccount );      
       printf("\n Creating %s .... \n", neweventfile);	   
       new_Event_File = fopen(neweventfile,"w");
     
		fprintf(new_Event_File, "\nReporting time   %4d/%02d/%02d %02d:%02d:%05.2f  averr=%.1f Q=%d Gap=%.0f Avg_wei=%.1f n=%d n_c=%d, n_m=%d, Padj=%.1f no_eq=%d"
			,  fyr, fmo, fdy, fhr, fmn, fsec
			,  G_averr, G_Q, hyp.gap, hyp.avwei
			, G_n, G_sta_num, G_n_mag, mag.Padj, num_eew);
					
	
	fprintf(new_Event_File, "\n");	

       fprintf(new_Event_File, "year  month  day  hour min  sec      lat      lon        dep    Mall   Mpd_s  Mpv   Mpd    Mtc   process_time  first_ptime\n"); 
       fprintf(new_Event_File, "%4d %2d      %2d  %2d    %2d  %6.2f ",fyr1,fmo1,fdy1,fhr1,fmn1,fsec1);   //event origin time
       fprintf(new_Event_File, "%9.4f %10.4f %6.2f %5.2f   %5.2f  %5.2f  %5.2f  %5.2f  "
       			, hyp.xla0, hyp.xlo0, hyp.depth0, mag.ALL_Mag, 0.0, 0.0, mag.xMpd, mag.mtc);  
                         
       fprintf(new_Event_File, "%.2f      \n", pro_time );		
	fprintf(new_Event_File, "Sta     C  N  L       lat        lon        pa        pv        pd        tc   Mtc   MPv   MPd    Perr   Dis  H_Wei        Parr     Pk_wei  Upd_sec  P_S  usd_sec\n");
               
	//sort_array_P_S(vsn_ntri, qtrigger);
			   	
      for(k=0;k<qtrigger;k++)
	   if(vsn_ntri[k].flag>=1)
       {           
	   
           // if(vsn_ntri[k].inst==3 ) continue;
            // if(vsn_ntri[k].stn_Loc[1] =='2') continue;    	
		    // if(vsn_ntri[k].stn_Comp[2] !='Z') continue; 
			// if(vsn_ntri[k].Pd[0] < 0.0001) continue;
	   
              tp=(time_t)vsn_ntri[k].P;          
              p=gmtime(&tp);         
              tmp=vsn_ntri[k].report_time-vsn_ntri[k].P;                
    	       tp=(time_t)vsn_ntri[k].report_time;
              p=gmtime(&tp);
              
              dis=delaz(vsn_ntri[k].latitude, vsn_ntri[k].longitude, hyp.xla0, hyp.xlo0);          
              datestr23 (vsn_ntri[k].P,           ntime, 256);
              
              ddis = sqrt(hyp.depth0*hyp.depth0+ dis*dis +0.000001 );
              
			  
	sprintf(outmsg,"%5s %s %s %s %9.5f %10.5f %9.6f %9.6f %9.6f %9.6f  %7.4f %5.0f %d %d %.1f %.2f %d %c%c%c %d"               		  
					  ,vsn_ntri[k].stn_name
              		  ,vsn_ntri[k].stn_Comp
              		  ,vsn_ntri[k].stn_Net
              		  ,vsn_ntri[k].stn_Loc
              		  ,vsn_ntri[k].latitude
              		  ,vsn_ntri[k].longitude
              		  ,vsn_ntri[k].Pa
              		  ,vsn_ntri[k].Pv
              		  ,vsn_ntri[k].Pd[0]
              		  ,vsn_ntri[k].Tc          		  

    			  ,vsn_ntri[k].perr
    			  ,ddis
			      ,vsn_ntri[k].usd_sec	
			      ,vsn_ntri[k].weight
			      ,vsn_ntri[k].P_S_time				  
    			  ,vsn_ntri[k].P   
				  ,num_eew
				  , Mark[0], Mark[1], Mark[2]
				  ,ccount);	
	    //ReportEEW_record( Region_out, reclogo_out, outmsg );			  
			  
              if(vsn_ntri[k].inst==2)
              	fprintf(new_Event_File,"%5s %s %s %s %9.5f %10.5f %9.6f %9.6f %9.6f %9.6f %5.2f %5.2f %5.2f %7.4f %5.0f %.2f %s %d %02d %.2f %02d\n"               		  ,vsn_ntri[k].stn_name
              		  ,vsn_ntri[k].stn_Comp
              		  ,vsn_ntri[k].stn_Net
              		  ,vsn_ntri[k].stn_Loc
              		  ,vsn_ntri[k].latitude
              		  ,vsn_ntri[k].longitude
              		  ,vsn_ntri[k].Pa
              		  ,vsn_ntri[k].Pv
              		  ,vsn_ntri[k].Pd[0]
              		  ,vsn_ntri[k].Tc          		  
              		  ,cal_Tc(vsn_ntri[k].Tc)
              		  ,0.0
              		  ,Mpd1_HH(vsn_ntri[k].Pd[0], ddis)
    			  ,vsn_ntri[k].perr
    			  ,ddis
    			  ,vsn_ntri[k].wei
    			  ,ntime     			  
			  ,vsn_ntri[k].weight
			  ,vsn_ntri[k].upd_sec
			  ,vsn_ntri[k].P_S_time
			  ,vsn_ntri[k].usd_sec);	 	
    	       else	if(vsn_ntri[k].inst==1)
              	fprintf(new_Event_File,"%5s %s %s %s %9.5f %10.5f %9.6f %9.6f %9.6f %9.6f %5.2f %5.2f %5.2f %7.4f %5.0f %.2f %s %d %02d %.2f %02d\n"
              		  ,vsn_ntri[k].stn_name
              		  ,vsn_ntri[k].stn_Comp
              		  ,vsn_ntri[k].stn_Net
              		  ,vsn_ntri[k].stn_Loc
              		  ,vsn_ntri[k].latitude
              		  ,vsn_ntri[k].longitude
              		  ,vsn_ntri[k].Pa
              		  ,vsn_ntri[k].Pv
              		  ,vsn_ntri[k].Pd[0]
              		  ,vsn_ntri[k].Tc          		  
              		  ,cal_Tc(vsn_ntri[k].Tc)
              		  ,0.0
              		  ,Mpd1_HL(vsn_ntri[k].Pd[0], ddis)
    			  ,vsn_ntri[k].perr
    			  ,ddis
    			  ,vsn_ntri[k].wei
    			  ,ntime     			 
			  ,vsn_ntri[k].weight	
			  ,vsn_ntri[k].upd_sec
			  ,vsn_ntri[k].P_S_time
			  ,vsn_ntri[k].usd_sec);	  			  
    	       else	
              	fprintf(new_Event_File,"%5s %s %s %s %9.5f %10.5f %9.6f %9.6f %9.6f %9.6f %5.2f %5.2f %5.2f %7.4f %5.0f %.2f %s %d %02d %.2f %02d\n"
              		  ,vsn_ntri[k].stn_name
              		  ,vsn_ntri[k].stn_Comp
              		  ,vsn_ntri[k].stn_Net
              		  ,vsn_ntri[k].stn_Loc
              		  ,vsn_ntri[k].latitude
              		  ,vsn_ntri[k].longitude
              		  ,vsn_ntri[k].Pa
              		  ,vsn_ntri[k].Pv
              		  ,vsn_ntri[k].Pd[0]
              		  ,vsn_ntri[k].Tc          		  
              		  ,cal_Tc(vsn_ntri[k].Tc)
              		  ,0.0
              		  ,Mpd1_HS(vsn_ntri[k].Pd[0], ddis)
    			  ,vsn_ntri[k].perr
    			  ,ddis
    			  ,vsn_ntri[k].wei
    			  ,ntime     			 
			  ,vsn_ntri[k].weight	
			  ,vsn_ntri[k].upd_sec
			  ,vsn_ntri[k].P_S_time
			  ,vsn_ntri[k].usd_sec);					  
       }  
              fclose(new_Event_File); 	
			  printf("=============Create Earthquake Report=========== %s \n", neweventfile);
	}		
	
	// sprintf(sentTime,"%4d-%02d-%02dT%02d:%02d:%02d"
			// ,fyr,fmo,fdy,fhr,fmn,(int)fsec );
	// sprintf(oriTime,"%4d-%02d-%02dT%02d:%02d:%02d"
			// ,fyr1,fmo1,fdy1,fhr1,fmn1,(int)fsec1 );
	sprintf(outmsg,"%d %f %f %d Mpd %.1f %.2f %.2f %.1f %2d %2d %2d %.1f %.1f %d %d %.1f %c%c%c %.1f"
				,num_eew
				,t_now,hyp.time0
				,ccount
				,mag.xMpd,hyp.xla0, hyp.xlo0, hyp.depth0
				,G_n, G_sta_num, G_n_mag
				,G_averr, hyp.avwei, G_Q, (int)hyp.gap, pro_time, Mark[0], Mark[1], Mark[2], mag.Padj);	
	ReportEEW( Region_out, reclogo_out, outmsg );

			
			/*
			faa=fopen("qwer.txt","a");
			fprintf(faa,"------------- %s \n",outmsg );
			fclose(faa);
			*/
	pre_G_n = G_n;
	pre_G_sta_num = G_sta_num;
	pre_proc_time = proc_time;	
	pre_GAP = hyp.gap;
	pre_G_n_mag = G_n_mag;
			   
}



double cal_pgv(double pd)
{
	double pPgv=0.0;
	
	if(pd==-1) return 0.0;
	
        pPgv=pow(10.0,(0.953*log10(pd)+1.659) );     
        
        return 	pPgv;
}

//C-------- This sub program calculate Mtc ------------
double cal_Tc(double tc)
{
	double mtc=0.0;
	if(tc<=0) return 0.0;
	
	 mtc=4.218*log10(tc)+6.166;
	 
	 return mtc;
}


void time_transe (int yr, int mo, int dy, int hr, int mn, int se, char *s, int *tt)
{
	struct tm tt2= {0};	
	char ss[64];
	time_t tt2_t;
	
	tt2.tm_year = yr-1900;
	tt2.tm_mon  = mo-1;
	tt2.tm_mday = dy;
	tt2.tm_hour = hr;
	tt2.tm_min  = mn;
	tt2.tm_sec  = se;
	
	tt2_t = mktime(&tt2);
 	strftime (ss, sizeof(ss), "%Y/%m/%d %H:%M:%S", &tt2);
 	strcpy(s,ss);
 	*tt = tt2_t;		
	
}
/*******************************************************
* Transfer yr mo dy hr mn se into 
* seconds from 1970/01/01 08:00:00
* Type = 1 : 20080202020202
* Type = 2 : 200802020202
********************************************************/
void disp_time1 (int yr, int mo, int dy, int hr, int mn, int se, char *ss, int type)
{
	char mon[3], day[3], hor[3], min[3], sec[3];
	
	if(mo<10) {
		sprintf(mon,"0%d",mo);
	} else {
		sprintf(mon,"%d",mo);		
	}
	if(dy<10) {
		sprintf(day,"0%d",dy);
	} else {
		sprintf(day,"%d",dy);		
	}	
	if(hr<10) {
		sprintf(hor,"0%d",hr);
	} else {
		sprintf(hor,"%d",hr);		
	}	
	if(mn<10) {
		sprintf(min,"0%d",mn);
	} else {
		sprintf(min,"%d",mn);		
	}	
	if(se<10) {
		sprintf(sec,"0%d",se);
	} else {
		sprintf(sec,"%d",se);		
	}	
	
	if(type==1){
		sprintf(ss,"%d%s%s%s%s%s",yr,mon,day,hor,min,sec);
	}
	else {
		sprintf(ss,"%d%s%s%s%s",yr,mon,day,hor,min);
	}
}
/*******************************************************
* Transfer yr mo dy hr mn se into 
* seconds from 1970/01/01 08:00:00
* Type = 1 : 20080202020202
* Type = 2 : 20080202 0202
********************************************************/
void disp_time (int yr, int mo, int dy, int hr, int mn, int se, char *ss, int type)
{
	char mon[3], day[3], hor[3], min[3], sec[3];
	
	if(mo<10) {
		sprintf(mon,"0%d",mo);
	} else {
		sprintf(mon,"%d",mo);		
	}
	if(dy<10) {
		sprintf(day,"0%d",dy);
	} else {
		sprintf(day,"%d",dy);		
	}	
	if(hr<10) {
		sprintf(hor,"0%d",hr);
	} else {
		sprintf(hor,"%d",hr);		
	}	
	if(mn<10) {
		sprintf(min,"0%d",mn);
	} else {
		sprintf(min,"%d",mn);		
	}	
	if(se<10) {
		sprintf(sec,"0%d",se);
	} else {
		sprintf(sec,"%d",se);		
	}	
	
	if(type==1){
		sprintf(ss,"%d%s%s%s%s%s",yr,mon,day,hor,min,sec);
	}
	else {
		sprintf(ss,"%d%s%s %s%s",yr,mon,day,hor,min);
	}
}
/*******************************************************
* Transfer yr mo dy hr mn se into 
* seconds from 1970/01/01 08:00:00
* Type = 1 : minus 8 hr
* Type = 2 : the same
********************************************************/
void time_now (char *ss, int plus_min, int type)
{
	time_t  tt, pp;
	struct tm *p;

		tt=time(NULL);
		pp=tt+plus_min*60;
		if(type==1) pp-=8*60*60;
		p = localtime(&pp);
	
	disp_time(1900+p->tm_year, 1+p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec, ss, 2);
}


int split_c(char **out_ss, char *in_ss)
{
	int i,j,k,len;
		
	len = strlen(in_ss);

		         	
	//printf("in_Ss:%s\nLen: %d \n", in_ss, len);
	
	j=0;
	k=0;
	for(i=0;i<len;i++)
	{	
		if(isalpha(in_ss[i]) || isdigit(in_ss[i]) || in_ss[i]=='.' || in_ss[i]=='-')
		{
			out_ss[j][k]=in_ss[i];	
			k++;			
		}
		else
		{
			//printf("%c \n", in_ss[i]);
			
			 out_ss[j][k]='\0';		 
		         //printf("%s \n", out_ss[j]);
			 j++;
			 k=0;
		}		
	}
			 out_ss[j][k]='\0';		 
		        // printf("%s \n", out_ss[j]);	
		       
	return j+1;	       
       
}

// double Mpa_HLZ_01(double pa, double dis)
// {
	// double ans;
	// if(pa<=0) return 0.0;	
	// ans = -0.405 + 1.457 * log10(pa) + 3.026 * log10(dis);    // 4.0
	
	// return ans;
// }
// double Mpa_HHZ_01(double pa, double dis)
// {
	// double ans;
	// if(pa<=0) return 0.0;	
	// ans = -0.197 + 1.493 * log10(pa) + 3.023 * log10(dis);    // 4.0
	
	// return ans;
// }
// double Mpv_HSZ_01(double pv, double dis)
// {
	// double ans;
	// if(pv<=0) return 0.0;	
	// ans = 1.595 + 1.585 * log10(pv) + 3.398 * log10(dis);    // 4.0
	
	// return ans;
// }
// double Mpa_HHZ_02(double pa, double dis)
// {
	// double ans;
	// if(pv<=0) return 0.0;	
	// ans = 0.760 + 1.379 * log10(pa) + 2.773 * log10(dis);    // 4.0
	
	// return ans;
// }


double Mpd1_HH(double pd, double dis)
{
	double ans;
	if(pd<=0) return 0.0;	
	// ans = 5.038 + 0.923 * log10(pd) + 1.338 * log10(dis);    // 4.5
	ans = 5.000 + 1.102 * log10(pd) + 1.737 * log10(dis);    // 4.0
	
	return ans;
}

double Mpv1_HH(double pv, double dis)
{
	double ans;
	if(pv<=0) return 0.0;	
	//ans = 2.579 + 1.553 * log10(pv) + 2.699 * log10(dis);
	//ans = 2.999 + 1.156 * log10(pv) + 2.149 * log10(dis);   // 4.5
	//ans = 2.593 + 1.349 * log10(pv) + 2.518 * log10(dis);   // 4.0
	ans = 2.928 + 1.316 * log10(pv) + 2.455 * log10(dis);   // 4.3
	
	return ans;
}
//==============================================================================================
double Mpd1_HL(double pd, double dis)
{
	double ans;
	if(pd<=0) return 0.0;	
	// ans = 5.038 + 0.923 * log10(pd) + 1.338 * log10(dis);    // 4.5
	ans = 5.067 + 1.281 * log10(pd) + 1.760 * log10(dis);    // 4.0
	
	return ans;
}

double Mpv1_HL(double pv, double dis)
{
	double ans;
	if(pv<=0) return 0.0;	
	//ans = 2.579 + 1.553 * log10(pv) + 2.699 * log10(dis);
	//ans = 2.999 + 1.156 * log10(pv) + 2.149 * log10(dis);   // 4.5
	//ans = 2.593 + 1.349 * log10(pv) + 2.518 * log10(dis);   // 4.0
	ans = 2.593 + 1.349 * log10(pv) + 2.518 * log10(dis);   // 4.3
	
	return ans;
}
//==============================================================================================
double Mpd1_HS(double pd, double dis)
{
	double ans;
	if(pd<=0) return 0.0;	
	// ans = 5.038 + 0.923 * log10(pd) + 1.338 * log10(dis);    // 4.5
	ans = 4.811 + 1.089 * log10(pd) + 1.738 * log10(dis);    // 4.0
	
	return ans;
}

double Mpv1_HS(double pv, double dis)
{
	double ans;
	if(pv<=0) return 0.0;	
	//ans = 2.579 + 1.553 * log10(pv) + 2.699 * log10(dis);
	//ans = 2.999 + 1.156 * log10(pv) + 2.149 * log10(dis);   // 4.5
	//ans = 2.593 + 1.349 * log10(pv) + 2.518 * log10(dis);   // 4.0
	ans = 2.784 + 1.260 * log10(pv) + 2.411 * log10(dis);   // 4.3
	
	return ans;
}

void cal_avg_std(double *data, int num, double *avg, double *std, double *maxmin, double *maxx, int st)
{
		int i, j;
		
		double ssum=0.0, aavg=0.0, sstd=0.0, sum_dev=0.0, max=-999.0, min=999.0;
		
		if(num==0)
		{
			*avg = 0.0;
			*std = 0.0;			
		}		
		j=0;
		for(i=0;i<num;i++)
		{
			if(i>=st)
			{	
				j++;
				ssum += data[i];
				if( data[i] < min ) min = data[i];
				if( data[i] > max ) max = data[i];
			}
		}
		aavg = ssum / (double)j;
		
		for(i=0;i<num;i++)
		{
			if(i>=st)
				sum_dev += pow(      (data[i] - aavg) ,   2  );
		}
		sstd = sqrt(sum_dev/j );
		
		*avg = aavg;
		*std = sstd;
		*maxmin = max - min;
		*maxx = max;
		
}
void cal_avg_std_mag(MAG_DATA *data, int num, double *avg, double *std)
{
		int i, cc=0;
		
		double ssum=0.0, aavg=0.0, sstd=0.0, sum_dev=0.0;
		
		if(num==0)
		{
			*avg = 0.0;
			*std = 0.0;	
			return;		
		}				
		for(i=0;i<num;i++)
		{
			if(data[i].mag >0)
			{
				ssum += data[i].mag;
				cc++;
			}
		}
		
		if(cc==0)
		{
			*avg = 0.0;
			*std = 0.0;	
			return;		
		}		
		
		aavg = ssum / (double)cc;
		
		// printf("ssum: %f, num: %d, aavg: %f \n", ssum, num, aavg);
		
		for(i=0;i<cc;i++)
				sum_dev += pow(      (data[i].mag - aavg) ,   2  );
		
		sstd = sqrt(sum_dev/(double)cc );
	
		*avg = aavg;
		*std = sstd;		
}
void cal_avg_std1(double *data, int num, double *avg, double *std)
{
		int i, cc=0;
		
		double ssum=0.0, aavg=0.0, sstd=0.0, sum_dev=0.0;
		
		if(num==0)
		{
			*avg = 0.0;
			*std = 0.0;	
			return;		
		}				
		for(i=0;i<num;i++)
		{
			if(data[i]>0)
			{
				ssum += data[i];
				cc++;
			}
		}
		
		if(cc==0)
		{
			*avg = 0.0;
			*std = 0.0;	
			return;		
		}		
		
		aavg = ssum / (double)cc;
		
		printf("ssum: %f, num: %d, aavg: %f \n", ssum, num, aavg);
		
		for(i=0;i<cc;i++)
				sum_dev += pow(      (data[i] - aavg) ,   2  );
		
		sstd = sqrt(sum_dev/(double)cc );
	
		*avg = aavg;
		*std = sstd;		
}
double cal_z(double x, double avg, double std)
{
	double z=0.0;
	
	if(avg==0.0) return 0.0;
	
	z = fabs(x-avg)/std;
	return z;
}

void Ini_stat( stat *data)
{
	data->avg = 0.0;
	data->std = 0.0;
	data->new_sum = 0.0;
	data->new_num = 0;	
}
void Ini_MAG( MAG *data)
{
// printf("---q1\n");
	data->xMpd     =0.0;
// printf("---q2\n");	
	// data->xMpd_sort     =0.0;	
	// data->xMpv     =0.0;
	data->mtc      =0.0;	
	data->ALL_Mag  =0.0;	
}



void sort_array(double *array, int num)
{
	int i,j;
	double tmp;
	
	for(i=0;i<num;i++)
	{
		for(j=0;j<i-1;j++)
		{
			if( array[j] >array[j+1]) 
			{
				tmp = array[j];
				array[j] = array[j+1];
				array[j+1] = tmp;
			}
		}
	}
}
void sort_array_mag(MAG_DATA *array, int num)
{
	int i,j;
	double tmp;
	
	for(i=0;i<num;i++)
	{
		for(j=0;j<i-1;j++)
		{
			if( array[j].mag >array[j+1].mag) 
			{
				tmp = array[j].mag;
				array[j].mag = array[j+1].mag;
				array[j+1].mag = tmp;
			}
		}
	}
}
void sort_array_P_S(PEEW *array, int num)
{
	int i,j;
	double tmp;
	
	for(i=0;i<num;i++)
	{
		for(j=0;j<i-1;j++)
		{
			if( array[j].P_S_time >array[j+1].P_S_time) 
			{
				tmp = array[j].P_S_time;
				array[j].P_S_time = array[j+1].P_S_time;
				array[j+1].P_S_time = tmp;
			}
		}
	}
}
void ReportEEW( SHM_INFO Region, MSG_LOGO reclogo_out, char *outmsg )
{

   int         lineLen;

   lineLen = strlen(outmsg);


/* Send the pick to the output ring
   ********************************/
   reclogo_out.type   = TypeEEW;
   //reclogo_out.mod    = Gparm->MyModId;
   //reclogo_out.instid = Ewh->MyInstId;

//printf("line: %s \n", line);

   if ( tport_putmsg( &Region, &reclogo_out, lineLen, outmsg ) != PUT_OK )
      logit( "et", "pick_ew: Error sending pick to output ring.\n" );
   return;
}
void ReportEEW_record( SHM_INFO Region, MSG_LOGO reclogo_out, char *outmsg )
{

   int         lineLen;

   lineLen = strlen(outmsg);


/* Send the pick to the output ring
   ********************************/
   reclogo_out.type   = Type_EEW_record;
   //reclogo_out.mod    = Gparm->MyModId;
   //reclogo_out.instid = Ewh->MyInstId;

//printf("line: %s \n", line);

   if ( tport_putmsg( &Region, &reclogo_out, lineLen, outmsg ) != PUT_OK )
      logit( "et", "pick_ew: Error sending pick to output ring.\n" );
   return;
}


double pa_HS(double M, double dis)
{
	double ans, inter;
	// if(pa<=0) return 0.0;
	// ans = -0.437 + 1.525 * log10(pa) + 3.215 * log10(dis); // 4.3	
	
	inter = ( M + 0.437 - 3.215 * log10(dis) ) / 1.525;
	ans = pow(10,inter);
		
	return ans;
}
double pa_HL(double M, double dis)
{
	double ans, inter;
	// if(pa<=0) return 0.0;
	// ans = -1.006 + 1.594 * log10(pa) + 3.496 * log10(dis); // 4.3
	
	inter = ( M + 1.006 - 3.496 * log10(dis) ) / 1.594;
	ans = pow(10,inter);
		
	return ans;
}
double pa_HH(double M, double dis)
{
	double ans, inter;
	// if(pa<=0) return 0.0;
	// ans = -0.133 + 1.533 * log10(pa) + 3.083 * log10(dis); // 4.3
	
	inter = ( M + 0.133 - 3.083 * log10(dis) ) / 1.533;
	ans = pow(10,inter);
	
	return ans;
}
