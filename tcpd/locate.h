#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/timeb.h>    //抓當地時間
#include <math.h>


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
    double Pd[15];
    double Tc;
    double dura;
    
    double report_time;
    int    npoints;
    
    double perr;
    double wei;  // determine in locaeq  
	
	int weight; // from pick
	int inst;
	int upd_sec;
	int usd_sec;	

	double P_S_time;	// P-S time
	int pin;			// represent specific SCNL
	
}PEEW;


typedef struct  {
    char sta[10];
    char chn[10];
    char net[10];
    char loc[10];
} SCNL;



typedef struct
{
	double xla0;
	double xlo0;
	double depth0;
	double time0;
	int Q; 
	double averr;	
	double gap;	
	double avwei;
} HYP;

typedef struct
{
	double xMpd;
	// double xMpd_sort;
	// double xMpv;
	double mtc;	
	double ALL_Mag;
	double Padj;
} MAG;

typedef struct
{
	double mag;
	double wei;
} MAG_DATA;

typedef struct
{
	double avg;
	double std;
	double new_sum;
	int    new_num;
} stat;




void locaeq(PEEW *ptr,int nsta, HYP *hyp);
void locaeq_grid(PEEW *ptr,int nsta, HYP *hyp);
int hyp_cmp(const void *x1, const void *x2);
void  matinv(double a[4][4],int n);
double wt(double dist,double res,int iter, double depth, double weight);
double delaz(double elat,double elon,double slat, double slon);

void processTrigger( int vsn_trigger, PEEW *vsn_ntri);

void Magnitude( PEEW *vsn_ntri, int qtrigger, HYP hyp, MAG *mag, double *pPgv, int *ntc);
				
void Report_seq( PEEW *vsn_ntri, int qtrigger, HYP hyp, MAG mag, int ntc);           

double cal_Tc(double tc);
double cal_pgv(double pd);

double Mpd1_HH(double pd, double dis);
double Mpv1_HH(double pv, double dis);
double Mpa1_HH(double pa, double dis);

double Mpd1_HL(double pd, double dis);
double Mpv1_HL(double pv, double dis);
double Mpa1_HL(double pa, double dis);

double Mpd1_HS(double pd, double dis);
double Mpv1_HS(double pv, double dis);
double Mpa1_HS(double pa, double dis);

void cal_avg_std(double *data, int num, double *avg, double *std, double *maxmin, double *max, int st);
void cal_avg_std1(double *data, int num, double *avg, double *std);
void cal_avg_std_mag(MAG_DATA *data, int num, double *avg, double *std);
void Ini_stat( stat *data);
void Ini_MAG( MAG *data);
double cal_z(double x, double avg, double std);                        

int check_seis_r(float *tlon, float *tlat, float elon, float elat, int num_boundary, float r);

