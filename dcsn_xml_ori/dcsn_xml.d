
# This is template's parameter file

#  Basic Earthworm setup:
#
MyModuleId         MOD_DCSN_XML  # module id for this instance of template 
RingName           EEW_RING   # shared memory ring for input/output
LogFile            1           # 0 to turn off disk log file; 1 to turn it on
                               # to log to module log but not stderr/stdout
HeartBeatInterval  15          # seconds between heartbeats

Magnitude	4.0
Pro_time	60.0
Show_Report_Num		50	# no larger than this number


XML_DIR			D:\Earthworm\xml		# where we store XML files for EEW client program
XML_DIR_LOCAL	D:\Earthworm\xml\xml	# where we store XML files for message
InfoType	Exercise			# Actual: for real case, Exercise: for drill, default: Exercise

# List the message logos to grab from transport ring
#              Installation       Module          Message Types
GetEventsFrom  INST_WILDCARD    MOD_WILDCARD    # HYP2000ARC & H72SUM2K

