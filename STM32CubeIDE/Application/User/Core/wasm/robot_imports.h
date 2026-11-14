#ifndef ROBOT_IMPORTS_H
#define ROBOT_IMPORTS_H
/* name, native implementation, i32 parameter count, minimum ABI. All results i32. */
#define ROBOT_IMPORTS(X) \
 X(imu_word, native_imu_word, 1, 1) \
 X(tof_mm, native_tof_mm, 1, 1) \
 X(millis, native_millis, 0, 1) \
 X(servo_get_us, native_servo_get, 1, 1) \
 X(servo_set_us, native_servo_set, 2, 1) \
 X(tof_enable, native_tof_enable, 1, 2) \
 X(imu_enable, native_imu_enable, 1, 2) \
 X(tof_set_range, native_tof_set_range, 2, 2) \
 X(tof_at, native_tof_at, 2, 2) \
 X(tof_read, native_tof_read, 1, 2) \
 X(tof_ready, native_tof_ready, 0, 2) \
 X(tof_frame_id, native_tof_frame_id, 0, 2) \
 X(tof_age_ms, native_tof_age_ms, 0, 2) \
 X(tof_min_mm, native_tof_min_mm, 0, 2) \
 X(tof_max_mm, native_tof_max_mm, 0, 2) \
 X(tof_mean_mm, native_tof_mean_mm, 0, 2) \
 X(tof_min_zone, native_tof_min_zone, 0, 2) \
 X(tof_max_zone, native_tof_max_zone, 0, 2) \
 X(tof_count, native_tof_count, 0, 2) \
 X(imu_ready, native_imu_ready, 0, 2) \
 X(imu_frame_id, native_imu_frame_id, 0, 2) \
 X(imu_age_ms, native_imu_age_ms, 0, 2) \
 X(imu_accel_mg, native_imu_accel_mg, 1, 2) \
 X(imu_gyro_mdps, native_imu_gyro_mdps, 1, 2)
#endif
