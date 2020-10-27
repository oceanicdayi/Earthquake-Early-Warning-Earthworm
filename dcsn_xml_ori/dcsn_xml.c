/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: dcsn.c,v 1.8 2007/03/28 17:50:34 paulf Exp $
 *
 *    Revision history:
 *     $Log: dcsn.c,v $
 *     Revision 1.8  2007/03/28 17:50:34  paulf
 *     fixed return of main
 *
 *     Revision 1.7  2007/02/26 14:57:30  paulf
 *     made sure time_t are casted to long for heartbeat sprintf()
 *
 *     Revision 1.6  2007/02/20 16:34:32  paulf
 *     fixed some windows bugs for time_t declarations being complained about
 *
 *     Revision 1.5  2007/02/20 13:29:20  paulf
 *     added lockfile testing to dcsn module
 *
 *     Revision 1.4  2002/05/15 16:56:38  patton
 *     Made logit changes
 *
 *     Revision 1.3  2001/05/09 17:26:54  dietz
 *     Changed to shut down gracefully if the transport flag is
 *     set to TERMINATE or myPid.
 *
 *     Revision 1.2  2000/07/24 19:18:47  lucky
 *     Implemented global limits to module, installation, ring, and message type strings.
 *
 *     Revision 1.1  2000/02/14 19:43:11  lucky
 *     Initial revision
 *
 *
 */

/*
 * dcsn.c:  Sample code for a basic earthworm module which:
 *              1) reads a configuration file using kom.c routines 
 *                 (dcsn_config).
 *              2) looks up shared memory keys, installation ids, 
 *                 module ids, message types from earthworm.h tables 
 *                 using getutil.c functions (dcsn_lookup).
 *              3) attaches to one public shared memory region for
 *                 input and output using transport.c functions.
 *              4) processes hard-wired message types from configuration-
 *                 file-given installations & module ids (This source
 *                 code expects to process TYPE_HINVARC & TYPE_H71SUM
 *                 messages).
 *              5) sends heartbeats and error messages back to the
 *                 shared memory region (dcsn_status).
 *              6) writes to a log file using logit.c functions.
 */

/* changes: 
  Lombard: 11/19/98: V4.0 changes: 
     0) changed message types to Y2K-compliant ones
     1) changed argument of logit_init to the config file name.
     2) process ID in heartbeat message
     3) flush input transport ring
     4) add `restartMe' to .desc file
     5) multi-threaded logit: not applicable
*/

#ifdef _OS2
#define INCL_DOSMEMMGR
#define INCL_DOSSEMAPHORES
#include <os2.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <earthworm.h>
#include <kom.h>
#include <transport.h>
#include <lockfile.h>
#include <time_ew.h>
#include <math.h>


// #include <iconv.h>
#include <string.h>

#include <errno.h>

typedef struct _DDATE
{
	int yr,mo,dy,hr,mn,se;
} DDATE;
typedef struct _MESSAGE
{
    int num_eew;
	char sentTime[40], oriTime[40];
	int nth;
	char magType[10];
	float mag, lat, lon, dep;
	int n_sta, n_csta, n_mag;
	float averr, avwei;
    int	Q, gap;
	float pro_time;
	double sstime, ootime;
	char ident[50];
	char Mark[5];
	double Padj;
} MESSAGE;

typedef struct  {
    int    flag;  	// 0: empty, 1: used-latest, 2: used-old. 
    int    serial;	// specific number    
    char   stn_name[8];
    char   stn_Loc[8];
    char   stn_Comp[8];    
    char   stn_Net[8];        
    double latitude;
    double longitude;
    double altitude;
    double P;             // in seconds. epoch seconds.
    double Pa;
    double Pv;
    double Pd;
    double Tc;
    double dura;
    
    double report_time;
    int    npoints;
    
    double perr;
    double wei;  // determine in locaeq  
	
	int weight; // from pick
	int inst;
	int upd_sec;

	double P_S_time;	// P-S time
	int pin;			// represent specific SCNL
	
}PEEW;

typedef struct
{
	double lon;
	double lat;
	double site_s;
	double site_d;
	char code[5];
	
}SITE_INFO;

#define MAX_STRING_LEN 32768


/* Functions in this source file 
 *******************************/
int convertFile(const char* tocode,const char* fromcode,char* inbuf,char* outbuf);
void b2u(char src[],char tar[]); 
int convert_utf8(char *instr, char *outstr );

void  dcsn_config  ( char * );
void  dcsn_lookup  ( void );
void  dcsn_status  ( unsigned char, short, char * );
void  copy_msg(MESSAGE *msg, MESSAGE *pre_msg);
void  ini_msg(MESSAGE *msg);


int make_xml_update(MESSAGE msg, char *status, char *Reference, char *ident_update);
int make_xml_alert(MESSAGE msg, char *status, char *ident_alert);
int make_xml_test();
int estimate_pga(double lon, double lat, double dep, double mag);

double delaz(double elat,double elon,double slat, double slon);

void fmd1(char *fname, char *InfoType);
   
static  SHM_INFO  Region;      /* shared memory region to use for i/o    */

#define   MAXLOGO   2
MSG_LOGO  GetLogo[MAXLOGO];    /* array for requesting module,type,instid */
short     nLogo;
pid_t     myPid;               /* for restarts by startstop               */

#define BUF_SIZE 60000          /* define maximum size for an event msg   */
static char Buffer[BUF_SIZE];   /* character string to hold event message */
        
/* Things to read or derive from configuration file
 **************************************************/
static char    RingName[MAX_RING_STR];        /* name of transport ring for i/o    */
static char    MyModName[MAX_MOD_STR];       /* speak as this module name/id      */
static int     LogSwitch;           /* 0 if no logfile should be written */
static long    HeartBeatInterval;   /* seconds between heartbeats        */

static int     Show_Report_Num;
/* Things to look up in the earthworm.h tables with getutil.c functions
 **********************************************************************/
static long          RingKey;       /* key of transport ring for i/o     */
static unsigned char InstId;        /* local installation id             */
static unsigned char MyModId;       /* Module Id for this program        */
static unsigned char TypeHeartBeat; 
static unsigned char TypeError;
static unsigned char TypeHinvArc;
static unsigned char TYPE_EEW;
static unsigned char Type_EEW_record;

static double   Magnitude;
static double   Pro_time;
static char     XML_DIR[200]; 
static char     XML_DIR_LOCAL[200]; 
static char     InfoType[50];



/* For DataBase and XML
 **********************************************************************/
static char    XML[50];
 
/* Error messages used by dcsn 
 *********************************/
#define  ERR_MISSMSG       0   /* message missed in transport ring       */
#define  ERR_TOOBIG        1   /* retreived msg too large for buffer     */
#define  ERR_NOTRACK       2   /* msg retreived; tracking limit exceeded */
static char  Text[150];        /* string for log/error messages          */


main( int argc, char **argv )
{
   time_t      timeNow;          /* current time                  */       
   time_t      timeLastBeat;     /* time last heartbeat was sent  */
   long      recsize;          /* size of retrieved message     */
   MSG_LOGO  reclogo;          /* logo of retrieved message     */
   int       res;
 
   char * lockfile;
   int lockfile_fd;
   char ident_update[20];
   char ident_alert[20];
   
   MESSAGE msg, pre_msg, alert;
   MESSAGE pic_alert;
   char ss[300];

   
   PEEW vsn_ntri;
   int num_eew,ddis,nth;
   
   // double report_time, origin_time;
   char otime[300], rtime[300], Ctmp[300], nntime[300], cmd[100]; 
   
    int ryr,rmo,rdy,rhr,rmn; 
  	double rsec;

    int oyr,omo,ody,ohr,omn;
  	double osec;   
	
    int ooyr,oomo,oody,oohr,oomn;
  	double oosec;  	
	
	double dis;
	char Mark[5];
	int  active_nth=0;
	
	int flag_alert, confirm;
	
	char t_now[300];	
	int cyr=0 ,cmo=0, cdy=0, chr=0, cmn=0, cse=0;
	int diff;
	int flag;

	FILE *fp;
	

/* Check command line arguments 
 ******************************/
   if ( argc != 2 )
   {
        fprintf( stderr, "Usage: dcsn <configfile>\n" );
        exit( 0 );
   }
   ini_msg(&pre_msg);
   ini_msg(&msg);
   ini_msg(&alert);   
   ini_msg(&pic_alert); 
/* Initialize name of log-file & open it 
 ***************************************/
   logit_init( argv[1], 0, 256, 1 );
   
/* Read the configuration file(s)
 ********************************/
   Magnitude = 4.5;
   Pro_time  = 40.0;
   sprintf(InfoType,"Exercise");
   
   
   dcsn_config( argv[1] );
   logit( "" , "%s: Read command file <%s>\n", argv[0], argv[1] );
   
    
	if( make_xml_test()==-1 ){ return;	}

   printf("============ XML InfoType: %s ============\n", InfoType);          
   printf("\n");
   printf("Magnitude: %f \n", Magnitude);
   printf("Pro_time: %f \n", Pro_time);  
   
   printf("XML_DIR: %s \n\n", XML_DIR);   
   printf("XML_DIR_LOCAL: %s \n\n", XML_DIR_LOCAL); 
   

   

   
/* Look up important info from earthworm.h tables
 ************************************************/
   dcsn_lookup();
 
/* Reinitialize logit to desired logging level 
 **********************************************/
   logit_init( argv[1], 0, 256, LogSwitch );


   lockfile = ew_lockfile_path(argv[1]); 
   if ( (lockfile_fd = ew_lockfile(lockfile) ) == -1) {
	fprintf(stderr, "one  instance of %s is already running, exiting\n", argv[0]);
	exit(-1);
   }
/*
   fprintf(stderr, "DEBUG: for %s, fd=%d for %s, LOCKED\n", argv[0], lockfile_fd, lockfile);
*/

/* Get process ID for heartbeat messages */
   myPid = getpid();
   if( myPid == -1 )
   {
     logit("e","dcsn: Cannot get pid. Exiting.\n");
     exit (-1);
   }

   
/* Attach to Input/Output shared memory ring 
 *******************************************/
   tport_attach( &Region, RingKey );
   logit( "", "dcsn: Attached to public memory region %s: %d\n", 
          RingName, RingKey );

/* Flush the transport ring */
   while( tport_getmsg( &Region, GetLogo, nLogo, &reclogo, &recsize, Buffer, 
			sizeof(Buffer)-1 ) != GET_NONE );

/* Force a heartbeat to be issued in first pass thru main loop
 *************************************************************/
   timeLastBeat = time(&timeNow) - HeartBeatInterval - 1;

/*----------------------- setup done; start main loop -------------------------*/

	
   while(1)
   {
     /* send dcsn's heartbeat
      ***************************/
        if  ( time(&timeNow) - timeLastBeat  >=  HeartBeatInterval ) 
        {
            timeLastBeat = timeNow;
            dcsn_status( TypeHeartBeat, 0, "" );
			
            datestr23 (time(&timeNow), nntime, 256);  //Origin time
			printf ("System: %s \n",nntime);
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
           /* write a termination msg to log file */
                logit( "t", "dcsn: Termination requested; exiting!\n" );
                fflush( stdout );
	   /* should check the return of these if we really care */
/*
   		fprintf(stderr, "DEBUG: %s, fd=%d for %s\n", argv[0], lockfile_fd, lockfile);
*/
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
                break;
           }
           else if( res == GET_TOOBIG )   /* next message was too big */
           {                              /* complain and try again   */
                sprintf(Text, 
                        "Retrieved msg[%ld] (i%u m%u t%u) too big for Buffer[%d]",
                        recsize, reclogo.instid, reclogo.mod, reclogo.type, 
                        sizeof(Buffer)-1 );
                dcsn_status( TypeError, ERR_TOOBIG, Text );
                continue;
           }
           else if( res == GET_MISS )     /* got a msg, but missed some */
           {
                sprintf( Text,
                        "Missed msg(s)  i%u m%u t%u  %s.",
                         reclogo.instid, reclogo.mod, reclogo.type, RingName );
                dcsn_status( TypeError, ERR_MISSMSG, Text );
           }
           else if( res == GET_NOTRACK ) /* got a msg, but can't tell */
           {                             /* if any were missed        */
                sprintf( Text,
                         "Msg received (i%u m%u t%u); transport.h NTRACK_GET exceeded",
                          reclogo.instid, reclogo.mod, reclogo.type );
                dcsn_status( TypeError, ERR_NOTRACK, Text );
           }

        /* Process the message 
         *********************/
           Buffer[recsize] = '\0';      /*null terminate the message*/
        /* logit( "", "%s", rec ); */   /*debug*/


		   // if( reclogo.type == Type_EEW_record ) 
           // {
		   // }
		   
		   								// time(&timeNow);											
										// strftime(t_now,sizeof(t_now)," %Y %m %d %H %M %S %Z",localtime(&timeNow) );
										// sscanf(t_now,"%d %d %d %d %d %d %*s", &cyr, &cmo, &cdy, &chr, &cmn, &cse);
										
										// printf( " -- %s \n ",ctime(&timeNow) );										
										// printf("%d %d %d %d %d %d\n", cyr, cmo, cdy, chr, cmn, cse);
										// printf("Now Time: %s \n", t_now);										

		    if( reclogo.type == TYPE_EEW) 
            {
								// printf("---TYPE_EEW \n");
//86 2013-10-02T05:58:55 2013-10-02T05:58:30 19 Mpd 4.6 23.84 120.99 10.0 56 25 16 0.5 0.6 6 44 25.2
				 // printf("source:%s\n",Buffer);				
				sscanf(Buffer,"%d %lf %lf %d %s %f %f %f %f %d %d %d %f %f %d %d %f %s %lf"
						, &msg.num_eew
						, &msg.sstime
						, &msg.ootime
						, &msg.nth
						, msg.magType
						, &msg.mag
						, &msg.lat
						, &msg.lon
						, &msg.dep
						, &msg.n_sta
						, &msg.n_csta
						, &msg.n_mag
						, &msg.averr
						, &msg.avwei
						, &msg.Q
						, &msg.gap
						, &msg.pro_time
						, msg.Mark
						, &msg.Padj);
						
		// printf("msg.Padj = %f \n", msg.Padj);
		
				datestr23 (msg.ootime, otime, 256);  //Origin time
	
				sprintf(Ctmp,"%c%c%c%c",otime[0],otime[1],otime[2],otime[3]);
				ooyr=atoi(Ctmp);		
				sprintf(Ctmp,"%c%c",otime[5],otime[6]);
				oomo=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[8],otime[9]);
				oody=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[11],otime[12]);
				oohr=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[14],otime[15]);
				oomn=atoi(Ctmp);	

		

				datestr23 (msg.sstime+28800, rtime, 256);  //Report time
				datestr23 (msg.ootime+28800, otime, 256);  //Origin time
	

	
	
				sprintf(Ctmp,"%c%c%c%c",otime[0],otime[1],otime[2],otime[3]);
				oyr=atoi(Ctmp);		
				sprintf(Ctmp,"%c%c",otime[5],otime[6]);
				omo=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[8],otime[9]);
				ody=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[11],otime[12]);
				ohr=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c",otime[14],otime[15]);
				omn=atoi(Ctmp);	
				sprintf(Ctmp,"%c%c%c%c%c",otime[17],otime[18],otime[19],otime[20],otime[21]);
				osec=atof(Ctmp);	
							
				
				sprintf(Ctmp,"%c%c%c%c",rtime[0],rtime[1],rtime[2],rtime[3]);
				ryr=atoi(Ctmp);		
				sprintf(Ctmp,"%c%c",rtime[5],rtime[6]);
				rmo=atoi(Ctmp);
				sprintf(Ctmp,"%c%c",rtime[8],rtime[9]);
				rdy=atoi(Ctmp);
				sprintf(Ctmp,"%c%c",rtime[11],rtime[12]);
				rhr=atoi(Ctmp);
				sprintf(Ctmp,"%c%c",rtime[14],rtime[15]);
				rmn=atoi(Ctmp);
				sprintf(Ctmp,"%c%c%c%c%c",rtime[17],rtime[18],rtime[19],rtime[20],rtime[21]);
				rsec=atof(Ctmp); 	
				
				
				sprintf(msg.sentTime,"%4d-%02d-%02dT%02d:%02d:%02d"
						,ryr,rmo,rdy,rhr,rmn,(int)rsec );
				sprintf(msg.oriTime,"%4d-%02d-%02dT%02d:%02d:%02d"
						,oyr,omo,ody,ohr,omn,(int)osec );	
				sprintf(msg.ident,"CWB-EEW%03d%04d%02d", oyr-1911, msg.num_eew, msg.nth);
				
		
		   								time(&timeNow);											
										strftime(t_now,sizeof(t_now)," %Y %m %d %H %M %S",localtime(&timeNow) );
										sscanf(t_now,"%d %d %d %d %d %d", &cyr, &cmo, &cdy, &chr, &cmn, &cse);
										
										// printf( " -- %s \n ",ctime(&timeNow) );										
										// printf("%d %d %d %d %d %d \n", cyr, cmo, cdy, chr, cmn, cse);
										// printf("Time: %s \n", t_now);						
				
				diff = ((((cyr-ooyr)*365+(cmo-oomo)*30+(cdy-oody))*24+(chr-oohr))*60+(cmn-oomn))*60+(cse-oosec);		
				diff = fabs(diff);
				if(diff>180)
				{
					/* write a msg to log file with time */
					logit( "t", "Origin Time: %s , Not Current EQ. Diff : %d\n", otime, diff );
					// printf("Origin Time: %s , Not Current EQ. \n", otime);
					// printf("Now Time: %s \n", ctime(&timeNow));
					// printf("Diff : %d \n", diff);
					continue;
				}
				// else
				// {
					// printf("Origin Time: %s , Current EQ. \n", otime);
					// printf("Now Time: %s \n", ctime(&timeNow));	
					// printf("Diff : %d \n", diff);					
				// }
				
				if( msg.gap >= 150 && msg.n_csta<= 10 ) continue;				
				
                // 2019.08.15 Director said
				msg.mag = msg.mag - 0.3;
                
                // printf("-----------  msg.mag: %f \n", msg.mag);

				if( !strncmp(msg.Mark,"231",3) || !strncmp(msg.Mark,"236",3) || !strncmp(msg.Mark,"230",3) || !strncmp(msg.Mark,"116",3) )
				{
					if(msg.nth>=3 && msg.n_mag>4 )	
					{
						//if( msg.num_eew !=pre_msg.num_eew ) active_nth = 0;
					
						if(   fabs(msg.sstime-alert.sstime)>60.0 && msg.nth>=3 && msg.mag >=4.5	)
						{
							fp=fopen("pga.log","a");
							fprintf(fp,"OT(TAP): %s, lon: %f, lat: %f, Dep: %f, Mag: %f\n",msg.oriTime,msg.lon,msg.lat,msg.dep,msg.mag);
							fclose(fp);
							flag = estimate_pga(msg.lon, msg.lat, msg.dep, msg.mag);
							fp=fopen("pga.log","a");
							fprintf(fp,"flag=%d\n",flag);
							fprintf(fp,"*****************************************************\n\n");
							fclose(fp);
							// if(	msg.mag >= Magnitude && msg.pro_time < Pro_time) 
							if(	flag == 1 && msg.pro_time < Pro_time) 
							{
								/* write a msg to log file with time */
								logit( "t", "Alert--msg: %f , alert: %f \n",msg.sstime ,alert.sstime );
								// printf("Alert--msg: %f , alert: %f \n",msg.sstime ,alert.sstime );
					
								confirm = 1;
								active_nth = 1;
								msg.nth = active_nth;
															
															
								/* write a msg to log file with time */
								logit( "t", "Origin Time: %s , Current EQ. Diff : %d\n", otime, diff );
								// printf("Origin Time: %s , Current EQ. \n", otime);
								// printf("Now Time: %s \n", ctime(&timeNow));	
								// printf("Diff : %d \n", diff);
								

	
								if(make_xml_alert(msg, InfoType, ident_alert)==-1)
								{
										/* write a msg to log file with time */
										logit( "t", "Error Opening file .. \n" );										
										// printf("Error Opening file .. \n");
										return;
								}			
								logit( "t", "Alert.... %s  %s\n",InfoType, ident_alert);
								// printf("Alert.... %s\n",InfoType);
								copy_msg(&msg, &pre_msg);	
								copy_msg(&msg, &alert);
							}
							else
							{
								if(LogSwitch)
									logit( "t", "Not an alert: time_diff%f , no:%d, pre_no:%d, Mag: %f, nth: %d, time: %.1f \n"
									, fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, msg.mag, msg.nth, msg.pro_time );
									// printf("Not an alert: time_diff%f , no:%d, pre_no:%d, Mag: %f, nth: %d, time: %.1f \n"
									// , fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, msg.mag, msg.nth, msg.pro_time);
							}
						}
						else if( (msg.gap < pre_msg.gap || (msg.n_mag > pre_msg.n_mag)) && confirm<10 )
						{						
							if  (   
									msg.num_eew==alert.num_eew && !strncmp(msg.Mark,alert.Mark,3)  					                           			          
								)
							{
								
								dis = delaz((double)msg.lat, (double)msg.lon, (double)pre_msg.lat, (double)pre_msg.lon);
								if( fabs(msg.mag-pre_msg.mag)>=0.5 || dis >= 20.0 )
								{						
							
									if( msg.gap >= 150 && dis < 50.0 ) 
									{
										confirm ++;
										continue;
									}
							
									confirm = 1;
									active_nth += 1;
									msg.nth = active_nth;
									msg.num_eew = alert.num_eew;
									sprintf(pre_msg.ident,"CWB-EEW%03d%04d%02d", oyr-1911, msg.num_eew, msg.nth-1);
									
									
	
									
									if(make_xml_update(msg, InfoType, pre_msg.ident, ident_update)==-1)						
									{
										logit( "t", "Error Opening file .. \n" );
										// printf("Error Opening file .. \n");
										return;
									}	
												
									copy_msg(&msg, &pre_msg);			
									logit( "t", "Update.... %s  %s\n", InfoType, ident_update );
									// printf("Update.... %s\n", InfoType);									
								}
								else
								{
									confirm ++;
									if(LogSwitch)
										logit( "t", "No need to update-- time_diff:%f , no:%d, pre_no:%d, Mag: %f, nth: %d \n"
										, fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, msg.mag, msg.nth );
										// printf("No need to update-- time_diff:%f , no:%d, pre_no:%d, Mag: %f, nth: %d \n"
										// , fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, msg.mag, msg.nth);
								}
							}
							else
							{
								if(LogSwitch)
									logit( "t", "Different: %f , no:%d, pre_no:%d, Alert_no:%d, Mag: %f, nth: %d , n_mag: %d -- %s %s\n"
									, fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, alert.num_eew, msg.mag, msg.nth, msg.n_mag, msg.Mark, alert.Mark );
									// printf("Different: %f , no:%d, pre_no:%d, Alert_no:%d, Mag: %f, nth: %d , n_mag: %d -- %s %s\n"
									// , fabs(msg.sstime-pre_msg.sstime),msg.num_eew ,pre_msg.num_eew, alert.num_eew, msg.mag, msg.nth, msg.n_mag, msg.Mark, alert.Mark);
							
							}
						}		
					}			
				}
            }
           
        } while( res != GET_NONE );  /*end of message-processing-loop */
        
        sleep_ew( 1000 );  /* no more messages; wait for new ones to arrive */

   }  
/*-----------------------------end of main loop-------------------------------*/        
}

/******************************************************************************
 *  dcsn_config() processes command file(s) using kom.c functions;        *
 *                    exits if any errors are encountered.                    *
 ******************************************************************************/
void dcsn_config( char *configfile )
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
                "dcsn: Error opening command file <%s>; exiting!\n", 
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
                          "dcsn: Error opening command file <%s>; exiting!\n",
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

				

  /*2*/     else if( k_its("XML_DIR") ) {
                str = k_str();
                if(str) strcpy( XML_DIR, str );
                init[2] = 1;
            }			
			
  /*2*/     else if( k_its("XML_DIR_LOCAL") ) {
                str = k_str();
                if(str) strcpy( XML_DIR_LOCAL, str );
                init[2] = 1;
            }				

			
  /*2*/     else if( k_its("InfoType") ) {
                str = k_str();
                if(str) strcpy( InfoType, str );
                init[2] = 1;
            }							
	
			
  /*3-1*/     else if( k_its("Magnitude") ) {
                Magnitude = k_val();
                init[3] = 1;
            }		
  /*3-1*/     else if( k_its("Pro_time") ) {
                Pro_time = k_val();
                init[3] = 1;
            }				
		
  /*3*/     else if( k_its("HeartBeatInterval") ) {
                HeartBeatInterval = k_long();
                init[3] = 1;
            }
  /*3*/     else if( k_its("Show_Report_Num") ) {
                Show_Report_Num = k_long();
                init[3] = 1;
            }

         /* Enter installation & module to get event messages from
          ********************************************************/
  /*4*/     else if( k_its("GetEventsFrom") ) {
                if ( nLogo+1 >= MAXLOGO ) {
                    logit( "e", 
                            "dcsn: Too many <GetEventsFrom> commands in <%s>", 
                             configfile );
                    logit( "e", "; max=%d; exiting!\n", (int) MAXLOGO/2 );
                    exit( -1 );
                }
                if( ( str=k_str() ) ) {
                   if( GetInst( str, &GetLogo[nLogo].instid ) != 0 ) {
                       logit( "e", 
                               "dcsn: Invalid installation name <%s>", str ); 
                       logit( "e", " in <GetEventsFrom> cmd; exiting!\n" );
                       exit( -1 );
                   }
                   GetLogo[nLogo+1].instid = GetLogo[nLogo].instid;
                }
                if( ( str=k_str() ) ) {
                   if( GetModId( str, &GetLogo[nLogo].mod ) != 0 ) {
                       logit( "e", 
                               "dcsn: Invalid module name <%s>", str ); 
                       logit( "e", " in <GetEventsFrom> cmd; exiting!\n" );
                       exit( -1 );
                   }
                   GetLogo[nLogo+1].mod = GetLogo[nLogo].mod;
                }
                if( GetType( "TYPE_EEW", &GetLogo[nLogo].type ) != 0 ) {
                    printf("\n --->Please define Message Type: TYPE_EEW in Earthworm.d. \n\n");
                    exit( -1 );
                }

                nLogo  += 2;
                init[4] = 1;
            /*    printf("GetLogo[%d] inst:%d module:%d type:%d\n",
                        nLogo, (int) GetLogo[nLogo].instid,
                               (int) GetLogo[nLogo].mod,
                               (int) GetLogo[nLogo].type ); */  /*DEBUG*/
            /*    printf("GetLogo[%d] inst:%d module:%d type:%d\n",
                        nLogo+1, (int) GetLogo[nLogo+1].instid,
                               (int) GetLogo[nLogo+1].mod,
                               (int) GetLogo[nLogo+1].type ); */  /*DEBUG*/
            }

         /* Unknown command
          *****************/ 
            else {
                logit( "e", "dcsn: <%s> Unknown command in <%s>.\n", 
                         com, configfile );
                continue;
            }

        /* See if there were any errors processing the command 
         *****************************************************/
            if( k_err() ) {
               logit( "e", 
                       "dcsn: Bad <%s> command in <%s>; exiting!\n",
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
       logit( "e", "dcsn: ERROR, no " );
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
 *  dcsn_lookup( )   Look up important info from earthworm.h tables       *
 ******************************************************************************/
void dcsn_lookup( void )
{
/* Look up keys to shared memory regions
   *************************************/
   if( ( RingKey = GetKey(RingName) ) == -1 ) {
        fprintf( stderr,
                "dcsn:  Invalid ring name <%s>; exiting!\n", RingName);
        exit( -1 );
   }


/* Look up installations of interest
   *********************************/
   if ( GetLocalInst( &InstId ) != 0 ) {
      fprintf( stderr, 
              "dcsn: error getting local installation id; exiting!\n" );
      exit( -1 );
   }

/* Look up modules of interest
   ***************************/
   if ( GetModId( MyModName, &MyModId ) != 0 ) {
      fprintf( stderr, 
              "dcsn: Invalid module name <%s>; exiting!\n", MyModName );
      exit( -1 );
   }

/* Look up message types of interest
   *********************************/
   if ( GetType( "TYPE_HEARTBEAT", &TypeHeartBeat ) != 0 ) {
      fprintf( stderr, 
              "dcsn: Invalid message type <TYPE_HEARTBEAT>; exiting!\n" );
      exit( -1 );
   }
   if ( GetType( "TYPE_ERROR", &TypeError ) != 0 ) {
      fprintf( stderr, 
              "dcsn: Invalid message type <TYPE_ERROR>; exiting!\n" );
      exit( -1 );
   }
   if ( GetType( "TYPE_EEW", &TYPE_EEW ) != 0 ) {
		printf("\n --->Please define Message Type: TYPE_EEW in Earthworm.d. \n\n");
        exit( -1 );
   }
   if ( GetType( "Type_EEW_record", &Type_EEW_record ) != 0 ) {
		printf("\n --->Please define Message Type: Type_EEW_record in Earthworm.d. \n\n");
        exit( -1 );
   }   
   return;
} 

/******************************************************************************
 * dcsn_status() builds a heartbeat or error message & puts it into       *
 *                   shared memory.  Writes errors to log file & screen.      *
 ******************************************************************************/
void dcsn_status( unsigned char type, short ierr, char *note )
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
        sprintf( msg, "%ld %ld\n\0", (long) t, (long) myPid);
   }
   else if( type == TypeError )
   {
        sprintf( msg, "%ld %hd %s\n\0", (long) t, ierr, note);
        logit( "et", "dcsn: %s\n", note );
   }

   size = strlen( msg );   /* don't include the null byte in the message */     

/* Write the message to shared memory
 ************************************/
   if( tport_putmsg( &Region, &logo, size, msg ) != PUT_OK )
   {
        if( type == TypeHeartBeat ) {
           logit("et","dcsn:  Error sending heartbeat.\n" );
        }
        else if( type == TypeError ) {
           logit("et","dcsn:  Error sending error:%d.\n", ierr );
        }
   }

   return;
}




int make_xml_update(MESSAGE msg, char *status, char *Reference, char *ident_update)
{
	FILE *fp;

	char ident[20], fname[300], fname1[300], cmd[600];
	char tmp[10];
	int yr;
	
	

	sprintf(tmp,"%c%c%c%c", msg.oriTime[0],msg.oriTime[1]
	                      , msg.oriTime[2],msg.oriTime[3]);
	yr = atoi(tmp)-1911;
	
		
	sprintf(ident,"CWB-EEW%03d%04d%02d", yr, msg.num_eew, msg.nth);
	sprintf(ident_update,"CWB-EEW%03d%04d%02d", yr, msg.num_eew, msg.nth);
	sprintf(fname,"%s\\%s.xml",XML_DIR,ident);
	sprintf(fname1,"%s\\%s.xml",XML_DIR_LOCAL,ident);	
	
	remove("tmpXML.zxc");
	
	fp=fopen("tmpXML.zxc","w");
	if(fp==NULL)
	{
		printf("%s is not valid ... ! \n", fname);
		return -1;
	}
      fprintf(fp,"<?xml version=\"1.0\" encoding=\"utf-8\" ?>			  \n ");
      fprintf(fp,"<earthquake>                                            \n ");
      fprintf(fp," <identifier>%s</identifier>               \n ", ident);
      fprintf(fp," <schemaVer>TW-CWB-XML-EEW:1.0</schemaVer>              \n ");
      fprintf(fp," <language>zh-TW</language>                             \n ");
      fprintf(fp," <event>地震警報</event>                                     \n ");
      fprintf(fp," <senderName>中華民國交通部中央氣象局</senderName> \n ");
      fprintf(fp," <sent>%s+08:00</sent>                  \n ", msg.sentTime);
      fprintf(fp," <status>%s</status>               \n ", status);
      fprintf(fp," <msgType>Update</msgType>              \n ");
      fprintf(fp," <references>%s</references>         \n ", Reference);
      fprintf(fp," <msgNo>%d</msgNo>                    \n ",msg.nth);
      fprintf(fp," <description></description>           \n ");
      fprintf(fp," <originTime>%s+08:00</originTime>     \n ", msg.oriTime);
      fprintf(fp," <epicenter>                                            \n ");
      fprintf(fp,"	 <epicenterLon unit=\"deg\">%.2f</epicenterLon>  \n ",msg.lon);
      fprintf(fp,"	 <epicenterLat unit=\"deg\">%.2f</epicenterLat>   \n ",msg.lat);
      fprintf(fp," </epicenter>                                           \n ");
      fprintf(fp," <depth unit=\"km\">%.1f</depth>                 \n ",msg.dep);
      fprintf(fp," <magnitude>                                            \n ");
      fprintf(fp,"	 <magnitudeType>%s</magnitudeType>                   \n ",msg.magType);
      fprintf(fp,"	 <magnitudeValue>%.1f</magnitudeValue>                 \n ",msg.mag);
      fprintf(fp," </magnitude>                                           \n ");
      fprintf(fp,"  <pgaAdj>1.0</pgaAdj>                                  \n ");
      fprintf(fp,"</earthquake>	                                          \n ");
	fclose(fp);
	
	sprintf(cmd,"iconv -c -f big5 -t utf-8 tmpXML.zxc > %s", fname);
	system(cmd);
	sprintf(cmd,"iconv -c -f big5 -t utf-8 tmpXML.zxc > %s", fname1);
	system(cmd);	
	
	remove("tmpXML.zxc");
	
	
	// printf("cmd: %s \n", cmd);	
	
	printf("%03d--%04d--%02d--%s %s %02d %s %.1f %.2f %.2f %.0f %02d %02d %02d %.1f %.1f %3d %03d %.1f %s\n"
		  , yr, msg.num_eew, msg.nth
		  , msg.oriTime
		  , msg.sentTime
		  , msg.nth
		  , msg.magType
		  , msg.mag
		  , msg.lat
		  , msg.lon
		  , msg.dep
		  , msg.n_sta
		  , msg.n_csta
		  , msg.n_mag
		  , msg.averr
		  , msg.avwei
		  , msg.Q
		  , msg.gap
		  , msg.pro_time
		  , msg.Mark);		
	return 1;
}
int make_xml_alert(MESSAGE msg, char *status, char *ident_alert)
{
	FILE *fp;

	char ident[20], fname[300], fname1[300], cmd[600];
	char tmp[10];
	int yr;
	char instr[250], outstr[250];	
	
	

	sprintf(tmp,"%c%c%c%c", msg.oriTime[0],msg.oriTime[1]
	                      , msg.oriTime[2],msg.oriTime[3]);
	yr = atoi(tmp)-1911;
	
	
		
	sprintf(ident,"CWB-EEW%03d%04d%02d", yr, msg.num_eew, msg.nth);
	sprintf(ident_alert,"CWB-EEW%03d%04d%02d", yr, msg.num_eew, msg.nth);

	sprintf(fname,"%s\\%s.xml",XML_DIR,ident);
	sprintf(fname1,"%s\\%s.xml",XML_DIR_LOCAL,ident);	
	
	remove("tmpXML.zxc");	
	
	fp=fopen("tmpXML.zxc","w");
	if(fp==NULL)
	{
		printf("%s is not valid ... ! \n", fname);
		return -1;
	}
      fprintf(fp,"<?xml version=\"1.0\" encoding=\"utf-8\" ?>			  \n ");
      fprintf(fp,"<earthquake>                                            \n ");
      fprintf(fp,"<identifier>%s</identifier>               \n ", ident);
      fprintf(fp,"<schemaVer>TW-CWB-XML-EEW:1.0</schemaVer>              \n ");
      fprintf(fp,"<language>zh-TW</language>                             \n ");

      fprintf(fp,"<event>地震警報</event>                             \n ");	  
	  // sprintf(instr,"<event>地震警報</event>");
	  // convertFile("BIG5","UTF-8",instr,outstr);
	  // convert_utf8(instr,outstr);
	  // b2u(instr,outstr);
      // fprintf(fp," %s                                     \n ", instr);	  
	  
      fprintf(fp,"<senderName>中華民國交通部中央氣象局</senderName>\n ");		  
	  // sprintf(instr,"<senderName>中華民國交通部中央氣象局</senderName>");
	  // convertFile("BIG5","UTF-8",instr,outstr);
	  // convert_utf8(instr,outstr);	  
	  // b2u(instr,outstr);
      // fprintf(fp," %s                                     \n ", instr);		  
	  

      fprintf(fp," <sent>%s+08:00</sent>                  \n ", msg.sentTime);
      fprintf(fp," <status>%s</status>               \n ", status);
      fprintf(fp," <msgType>Alert</msgType>              \n ");
      fprintf(fp," <msgNo>%d</msgNo>                    \n ",msg.nth);
      fprintf(fp," <description></description>           \n ");
      fprintf(fp," <originTime>%s+08:00</originTime>     \n ", msg.oriTime);
      fprintf(fp," <epicenter>                                            \n ");
      fprintf(fp,"	 <epicenterLon unit=\"deg\">%.2f</epicenterLon>  \n ",msg.lon);
      fprintf(fp,"	 <epicenterLat unit=\"deg\">%.2f</epicenterLat>   \n ",msg.lat);
      fprintf(fp," </epicenter>                                           \n ");
      fprintf(fp," <depth unit=\"km\">%.1f</depth>                 \n ",msg.dep);
      fprintf(fp," <magnitude>                                            \n ");
      fprintf(fp,"	 <magnitudeType>%s</magnitudeType>                   \n ",msg.magType);
      fprintf(fp,"	 <magnitudeValue>%.1f</magnitudeValue>                 \n ",msg.mag);
      fprintf(fp," </magnitude>                                           \n ");
      fprintf(fp,"  <pgaAdj>1.0</pgaAdj>                                  \n ");
      fprintf(fp,"</earthquake>	                                          \n ");
	fclose(fp);
	
	sprintf(cmd,"iconv -c -f big5 -t utf-8 tmpXML.zxc > %s", fname);
	system(cmd);
	sprintf(cmd,"iconv -c -f big5 -t utf-8 tmpXML.zxc > %s", fname1);
	system(cmd);	
	
	remove("tmpXML.zxc");
	
	// printf("cmd: %s \n", cmd);
	
	printf("%03d--%04d--%02d--%s %s %02d %s %.1f %.2f %.2f %.0f %02d %02d %02d %.1f %.1f %3d %03d %.1f %s\n"
		  , yr, msg.num_eew, msg.nth
		  , msg.oriTime
		  , msg.sentTime
		  , msg.nth
		  , msg.magType
		  , msg.mag
		  , msg.lat
		  , msg.lon
		  , msg.dep
		  , msg.n_sta
		  , msg.n_csta
		  , msg.n_mag
		  , msg.averr
		  , msg.avwei
		  , msg.Q
		  , msg.gap
		  , msg.pro_time
		  , msg.Mark);		
	return 1;
}
int make_xml_test()
{
	FILE *fp;
	char fname[300], fname1[300];
	
	
	
	sprintf(fname,"%s\\EEW_CWB",XML_DIR);
	fp=fopen(fname,"w");
	if(fp==NULL)
	{
		printf("%s is not valid ... ! \n", fname);
		return -1;
	}
	fclose(fp);
	
	sprintf(fname1,"%s\\EEW_CWB",XML_DIR_LOCAL);
	fp=fopen(fname1,"w");
	if(fp==NULL)
	{
		printf("%s is not valid ... ! \n", fname1);
		return -1;
	}
	fclose(fp);	
	
	return 1;
}
 
   
	void copy_msg(MESSAGE *msg, MESSAGE *pre_msg)
	{
		pre_msg->num_eew = msg->num_eew;
		pre_msg->sstime  = msg->sstime;
		pre_msg->ootime  = msg->ootime;
		pre_msg->nth	 = msg->nth;
		pre_msg->mag	 = msg->mag;
		pre_msg->lat	 = msg->lat;
		pre_msg->lon	 = msg->lon;
		pre_msg->dep	 = msg->dep;
		pre_msg->n_sta	 = msg->n_sta;
		pre_msg->n_csta	 = msg->n_csta;
		pre_msg->n_mag	 = msg->n_mag;
		pre_msg->averr	 = msg->averr;
		pre_msg->avwei	 = msg->avwei;
		pre_msg->Q		 = msg->Q;
		pre_msg->gap	 = msg->gap;
		pre_msg->pro_time= msg->pro_time;		
		pre_msg->Padj	 = msg->Padj;			
	
		sprintf(pre_msg->magType ,"%s",msg->magType);
		sprintf(pre_msg->sentTime,"%s",msg->sentTime);
		sprintf(pre_msg->oriTime ,"%s",msg->oriTime);	
		sprintf(pre_msg->ident   ,"%s",msg->ident);				
		sprintf(pre_msg->Mark   ,"%s",msg->Mark);			
		
		// printf("\n-- %s , %f , %f \n", pre_msg->Mark, pre_msg->sstime, msg->sstime);
	}
	void ini_msg(MESSAGE *msg)
	{
		msg->num_eew = 0;
		msg->sstime  = 0.0;
		msg->ootime  = 0.0;
		msg->nth	 = 0;
		msg->mag	 = 0.0;
		msg->lat	 = 0.0;
		msg->lon	 = 0.0;
		msg->dep	 = 0.0;
		msg->n_sta	 = 0;
		msg->n_csta	 = 0;
		msg->n_mag	 = 0;
		msg->averr	 = 0.0;
		msg->avwei	 = 0.0;
		msg->Q		 = 0;
		msg->gap	 = 0;
		msg->pro_time= 0.0;		
		msg->Padj	 = 0.0;				
	
		sprintf(msg->magType ," ");
		sprintf(msg->sentTime," ");
		sprintf(msg->oriTime ," ");	
		sprintf(msg->ident   ," ");						
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
// void show_report()	

void fmd1(char *fname, char *InfoType)
{
	FILE *fp;
	int len,i;

	
	
	
	fp=fopen("fmd1.bat","w");
	
	fprintf(fp,"@ echo off \n\n");

	fprintf(fp," SET NETCDF=C:\\NETCDF\n");
	fprintf(fp," SET GMTHOME=C:\\gmt341\n");
	fprintf(fp," SET HOME=C:\\gmt341\n");
	fprintf(fp," SET INCLUDE=%cINCLUDE%c;%cNETCDF%c\\INCLUDE\n",'%','%','%','%');
	fprintf(fp," SET LIB=%cLIB%c;%cNETCDF%c\\LIB;%cGMTHOME%c\\LIB\n",'%','%','%','%','%','%');
	fprintf(fp," SET PATH=%cPATH%c;%cGMTHOME%c\\BIN;%cNETCDF%c\\BIN\n\n",'%','%','%','%','%','%');            

//----------------------------------

fprintf(fp," psbasemap -R119/124.5/21/26 -JM7 -B1f.5:.%c%s%c: -P -K  -Y5 > %s.ps \n"
				,'"',  fname,'"', InfoType);

fprintf(fp," pscoast -R -B -JM -Dh -S200/200/200 -K -O >> %s.ps \n", InfoType);
fprintf(fp," pstext txt -R -B -JM -Gblue -K -O  >> %s.ps \n", InfoType);
fprintf(fp," pstext txt1 -R -B -JM -Gred -K -O  >> %s.ps \n", InfoType);

fprintf(fp," psxy main -R -B -JM -W3/255/0/0 -Sa0.5 -O  >> %s.ps \n", InfoType);
	

fprintf(fp," ps2raster %s.ps -GC:\\gs\\gs7.04\\bin\\gswin32c -A -Tj\n", InfoType);
//fprintf(fp," mdlsq.png\n\n");	
	
	
		
	fclose(fp);
		
}

int estimate_pga(double lon, double lat, double dep, double mag)
{
	int flag=0,i;
	double hypo_dist,pga,Si,Padj=1.0;
	FILE *fp;
	
	SITE_INFO site_info[22];
	site_info[0].lon = 121.565; site_info[0].lat = 25.038; site_info[0].site_s = 2.218; site_info[0].site_d = 2.565; sprintf(site_info[0].code,"TAP"); 
	site_info[1].lon = 121.745; site_info[1].lat = 25.132; site_info[1].site_s = 0.741; site_info[1].site_d = 1.567; sprintf(site_info[1].code,"KLU"); 
	site_info[2].lon = 121.465; site_info[2].lat = 25.013; site_info[2].site_s = 2.218; site_info[2].site_d = 2.565; sprintf(site_info[2].code,"BAC"); 
	site_info[3].lon = 121.301; site_info[3].lat = 24.993; site_info[3].site_s = 1.210; site_info[3].site_d = 2.683; sprintf(site_info[3].code,"NTY"); 
	site_info[4].lon = 120.969; site_info[4].lat = 24.807; site_info[4].site_s = 1.888; site_info[4].site_d = 2.120; sprintf(site_info[4].code,"HSN1"); 
	site_info[5].lon = 121.013; site_info[5].lat = 24.827; site_info[5].site_s = 1.342; site_info[5].site_d = 1.586; sprintf(site_info[5].code,"HSN"); 
	site_info[6].lon = 120.821; site_info[6].lat = 24.565; site_info[6].site_s = 1.782; site_info[6].site_d = 2.156; sprintf(site_info[6].code,"NML"); 
	site_info[7].lon = 120.679; site_info[7].lat = 24.139; site_info[7].site_s = 1.624; site_info[7].site_d = 2.416; sprintf(site_info[7].code,"TCU"); 
	site_info[8].lon = 120.690; site_info[8].lat = 23.902; site_info[8].site_s = 1.479; site_info[8].site_d = 2.426; sprintf(site_info[8].code,"WNT"); 
	site_info[9].lon = 120.545; site_info[9].lat = 24.076; site_info[9].site_s = 2.066; site_info[9].site_d = 3.674; sprintf(site_info[9].code,"WCH"); 
	site_info[10].lon = 120.526; site_info[10].lat = 23.699; site_info[10].site_s = 2.116; site_info[10].site_d = 2.949; sprintf(site_info[10].code,"WDL"); 
	site_info[11].lon = 120.454; site_info[11].lat = 23.481; site_info[11].site_s = 2.004; site_info[11].site_d = 3.030; sprintf(site_info[11].code,"CHY"); 
	site_info[12].lon = 120.293; site_info[12].lat = 23.459; site_info[12].site_s = 2.201; site_info[12].site_d = 3.030; sprintf(site_info[12].code,"CHY1"); 
	site_info[13].lon = 120.185; site_info[13].lat = 22.992; site_info[13].site_s = 2.447; site_info[13].site_d = 1.910; sprintf(site_info[13].code,"TAI"); 
	site_info[14].lon = 120.312; site_info[14].lat = 22.621; site_info[14].site_s = 1.803; site_info[14].site_d = 2.359; sprintf(site_info[14].code,"KAU"); 
	site_info[15].lon = 120.488; site_info[15].lat = 22.683; site_info[15].site_s = 1.068; site_info[15].site_d = 1.103; sprintf(site_info[15].code,"SPT"); 
	site_info[16].lon = 121.763; site_info[16].lat = 24.731; site_info[16].site_s = 1.669; site_info[16].site_d = 1.973; sprintf(site_info[16].code,"ILA"); 
	site_info[17].lon = 121.620; site_info[17].lat = 23.991; site_info[17].site_s = 1.077; site_info[17].site_d = 1.061; sprintf(site_info[17].code,"HWA"); 
	site_info[18].lon = 121.151; site_info[18].lat = 22.755; site_info[18].site_s = 0.919; site_info[18].site_d = 1.043; sprintf(site_info[18].code,"TTN"); 
	site_info[19].lon = 119.566; site_info[19].lat = 23.570; site_info[19].site_s = 2.607; site_info[19].site_d = 1.000; sprintf(site_info[19].code,"PNG"); 
	site_info[20].lon = 118.319; site_info[20].lat = 24.437; site_info[20].site_s = 1.000; site_info[20].site_d = 1.000; sprintf(site_info[20].code,"KNM"); 
	site_info[21].lon = 119.951; site_info[21].lat = 26.157; site_info[21].site_s = 1.000; site_info[21].site_d = 1.000; sprintf(site_info[21].code,"MSU"); 
	
	fp=fopen("pga.log","a");
	for(i=0; i<22; i++)
	{
		if (dep<40) Si=site_info[i].site_s;
		else Si=site_info[i].site_d;
		hypo_dist = sqrt(pow(delaz(lat,lon,site_info[i].lat,site_info[i].lon),2)+pow(dep,2));
		// pga = 1.657*exp(1.533*mag)*pow(hypo_dist,-1.607)*Si*Padj;
		pga = 12.44*exp(1.31*mag)*pow(hypo_dist,-1.837)*Si*Padj;
		// printf("Site: %s, lon: %f, lat: %f, Si: %f, dist: %f, pga: %f\n",site_info[i].code,site_info[i].lon,site_info[i].lat,Si,hypo_dist,pga);
		fprintf(fp,"Site: %s, lon: %f, lat: %f, Si: %f, dist: %f, pga: %f\n",site_info[i].code,site_info[i].lon,site_info[i].lat,Si,hypo_dist,pga);
		if (pga>=8.0) 
		{
			flag=1;
			break;
		}
	}
	fclose(fp);
	
	
	return flag;
}
