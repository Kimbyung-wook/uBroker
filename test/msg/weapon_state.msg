# Weapon state payload
uint8 ARM_STATE_SAFE  = 0       # safe
uint8 ARM_STATE_ARMED = 1       # armed

uint64      timestamp_us            # measurement time (microseconds)
float32     target_x
float32     target_y
uint8       arm_state   : 2  0      # ARM_STATE_* (default: SAFE)
uint8       error_code  : 6
bool        is_locked        false
TickType_t  last_change_tick        # FreeRTOS tick of the last state change
