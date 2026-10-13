# IMU (Inertial Measurement Unit) Sensor Data
uint64      timestamp_us        # measurement time (microseconds)
vector3     accel               # linear acceleration (m/s^2)
vector3     gyro                # angular velocity (rad/s)
float32     temperature  25.0   # internal sensor temperature (Celsius), default 25.0

# Sensor health and status (packed into 1 byte)
uint8       is_calibrated : 1   # 0: Uncalibrated, 1: Calibrated
uint8       accel_healthy : 1   # 0: Error, 1: OK
uint8       gyro_healthy  : 1   # 0: Error, 1: OK
uint8       error_code    : 5   # other hardware error code (0-31)

# Publish two IMUs as separate topics with the same struct (multi-instance)
# TOPICS sensor_imu0 sensor_imu1
