#ifndef __MPU6500_H__
#define __MPU6500_H__

#include <stdint.h>

/* ===== 数据结构 ===== */
struct mpu6500_sensor_data {
	int16_t accel_x;   /* 加速度 X */
	int16_t accel_y;   /* 加速度 Y */
	int16_t accel_z;   /* 加速度 Z */
	int16_t gyro_x;    /* 陀螺仪 X */
	int16_t gyro_y;    /* 陀螺仪 Y */
	int16_t gyro_z;    /* 陀螺仪 Z */
	int16_t temp;      /* 温度 */
};

struct mpu6500_actual_data {
    float accel_x;     /* 加速度 X (g) */
    float accel_y;     /* 加速度 Y (g) */
    float accel_z;     /* 加速度 Z (g) */
    float gyro_x;      /* 陀螺仪 X (°/s) */
    float gyro_y;      /* 陀螺仪 Y (°/s) */
    float gyro_z;      /* 陀螺仪 Z (°/s) */
    float temp;        /* 温度 (°C) */
};

/* 陀螺仪零漂结构体 */
struct gyro_bias_t
{
    float bias_x;
    float bias_y;
    float bias_z;
};

/*姿态结构*/
struct attitude_t
{
    float roll;
    float pitch;
    float yaw;
};

#define RAD_TO_DEG 57.2957795f
#define ALPHA      0.98f

/* ===== 转换系数 ===== */
/* 加速度计：FSR = ±2g，分辨率 = 16384 LSB/g */
#define ACCEL_SCALE    (1.0f / 16384.0f)

/* 陀螺仪：FSR = ±250°/s，分辨率 = 131 LSB/(°/s) */
#define GYRO_SCALE     (1.0f / 131.0f)

/* 温度：偏移 = 21°C，分辨率 = 1/333.87 */
#define TEMP_SCALE     (1.0f / 333.87f)
#define TEMP_OFFSET    21.0f

/* ===== 函数声明 ===== */
int mpu6500_open_device(const char *device_path);
int mpu6500_close_device(int fd);
int mpu6500_read_raw_data(int fd, struct mpu6500_sensor_data *data);
int mpu6500_read_ioctl(int fd, struct mpu6500_sensor_data *data);

/* 数据解析函数 */
void mpu6500_parse_accel(const struct mpu6500_sensor_data *raw, 
                          float *accel_x, float *accel_y, float *accel_z);
void mpu6500_parse_gyro(const struct mpu6500_sensor_data *raw,
                         float *gyro_x, float *gyro_y, float *gyro_z);
float mpu6500_parse_temperature(const struct mpu6500_sensor_data *raw);

/* 数据转换函数 */
void mpu_raw_data_trans(const struct mpu6500_sensor_data *raw, struct mpu6500_actual_data *actual);

#endif /* __MPU6500_H__ */