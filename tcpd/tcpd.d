
# This is template's parameter file

#  Basic Earthworm setup:
#
MyModuleId         MOD_TCPD  # module id for this instance of template 
RingName           PICK_RING   # shared memory ring for input/output
RingName_out       EEW_RING   # shared memory ring for input/output

LogFile            0           # 0 to turn off disk log file; 1 to turn it on
                               # to log to module log but not stderr/stdout
HeartBeatInterval  15          # seconds between heartbeats


MagMin 0.5 			# Min magnitude
MagMax 10		        # Max magnitude

Ignore_weight_P		2  # include 1
Ignore_weight_S		2


#MagReject			NANB HHZ  BB 01		# ignore magnitude


#Trig_tm_win        9.0		# The P wave arrival time between each triggered station
#Trig_dis_win      60.0		# Distances between each triggered station
#Active_parr_win   40.0		# Survival time of each station (sec) , between the P wave arrival time and current time

Trig_tm_win       15.0		# The P wave arrival time between each triggered station
Trig_dis_win      100.0		# Distances between each triggered station
Active_parr_win   80.0		# Survival time of each station (sec) , between the P wave arrival time and current time


#Trig_tm_win       15.0		# The P wave arrival time between each triggered station
#Trig_dis_win      100.0		# Distances between each triggered station
#Active_parr_win   70.0		# Survival time of each station (sec) , between the P wave arrival time and current time

Term_num     50          		          # The last report should be less than this number.                     
Show_Report	  1							  #  0: Disable, 1:Enable

 #----------------- P-wave velocity model
  Boundary_P     40.0        		          # boundary of shallow and deep layers                                    
  SwP_V        5.10298          		      # initial velocity in shallow layer                                    
  SwP_VG       0.06659       		          # gradient velocity in shallow layer                                   
  DpP_V        7.80479         		          # initial velocity in deep layer                                       
  DpP_VG       0.00457      		          # gradient velocity in deep layer                                      
##  #


 #----------------- S-wave velocity model
  Boundary_S     50.0        		          # boundary of shallow and deep layers                                  
  SwS_V        2.9105          		  		  # initial velocity in shallow layer                                    
  SwS_VG       0.0365       		          # gradient velocity in shallow layer                                   
  DpS_V        4.5374         		          # initial velocity in deep layer                                       
  DpS_VG       0.0023      		              # gradient velocity in deep layer                                      
#  #




# List the message logos to grab from transport ring
#              Installation       Module		Message
GetEventsFrom  INST_WILDCARD    MOD_WILDCARD	TYPE_EEW







