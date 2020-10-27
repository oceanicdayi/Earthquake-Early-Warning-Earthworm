/*******************************************************
* Transfer yr mo dy hr mn se into 
* seconds from 1970/01/01 08:00:00
********************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

void time_transe (int yr, int mo, int dy, int hr, int mn, int se, char *s, int *tt);
void disp_time  (int yr, int mo, int dy, int hr, int mn, int se, char *ss, int type);
void disp_time1 (int yr, int mo, int dy, int hr, int mn, int se, char *ss, int type);
void time_now (char *ss, int plus_min, int type);
