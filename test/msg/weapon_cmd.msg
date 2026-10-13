# Weapon command (aperiodic). Commands are events, so they are queued instead of overwritten
# QUEUE 4
uint8 CMD_ARM    = 1        # arm
uint8 CMD_DISARM = 2        # disarm

uint32      req_id              # request number, echoed in the reply
uint8       cmd                 # CMD_*
float32     param               # command parameter
