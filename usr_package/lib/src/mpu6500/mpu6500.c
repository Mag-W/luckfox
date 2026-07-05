#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <math.h>
#include "log.h"
#include "mpu6500.h"

#define IOCTL_MPU6500_READ_DATA  _IOR('m', 1, struct mpu6500_sensor_data)

/**
 * 打开 MPU6500 设备
 */
int mpu6500_open_device(const char *device_path)
{
	int fd = open(device_path, O_RDWR);
	if (fd < 0) {
		LOG_ERROR_FMT("Failed to open MPU6500 device");
		return -1;
	}
	return fd;
}

/**
 * 关闭设备
 */
int mpu6500_close_device(int fd)
{
	if (fd >= 0) {
		return close(fd);
	}
	return -1;
}

/**
 * 方法 1：使用 read() 读取原始数据
 */
int mpu6500_read_raw_data(int fd, struct mpu6500_sensor_data *data)
{
	if (fd < 0 || !data) {
		LOG_ERROR_FMT("Invalid file descriptor or data pointer\n");
		return -1;
	}

	/* 驱动会返回 sizeof(struct mpu6500_sensor_data) 字节的数据 */
	ssize_t bytes_read = read(fd, (void *)data, sizeof(*data));
	if (bytes_read < 0) {
		LOG_ERROR_FMT("Failed to read sensor data");
		return -1;
	}

	if (bytes_read != sizeof(*data)) {
		LOG_ERROR_FMT("Incomplete data read: %ld bytes\n", bytes_read);
		return -1;
	}

	return 0;
}

/**
 * 方法 2：使用 ioctl() 读取数据
 */
int mpu6500_read_ioctl(int fd, struct mpu6500_sensor_data *data)
{
	if (fd < 0 || !data)
    {
		LOG_ERROR_FMT("Invalid file descriptor or data pointer\n");
		return -1;
	}

	if (ioctl(fd, IOCTL_MPU6500_READ_DATA, data) < 0)
    {
		LOG_ERROR_FMT("Failed to read sensor data via ioctl");
		return -1;
	}

	return 0;
}

/**
 * 解析加速度数据（原始值 → g）
 */
void mpu6500_parse_accel(const struct mpu6500_sensor_data *raw,
                          float *accel_x, float *accel_y, float *accel_z)
{
	if (!raw || !accel_x || !accel_y || !accel_z)
		return;

	*accel_x = raw->accel_x * ACCEL_SCALE;
	*accel_y = raw->accel_y * ACCEL_SCALE;
	*accel_z = raw->accel_z * ACCEL_SCALE;
}

/**
 * 解析陀螺仪数据（原始值 → °/s）
 */
void mpu6500_parse_gyro(const struct mpu6500_sensor_data *raw,
                         float *gyro_x, float *gyro_y, float *gyro_z)
{
	if (!raw || !gyro_x || !gyro_y || !gyro_z)
		return;

	*gyro_x = raw->gyro_x * GYRO_SCALE;
	*gyro_y = raw->gyro_y * GYRO_SCALE;
	*gyro_z = raw->gyro_z * GYRO_SCALE;
}

/**
 * 解析温度数据（原始值 → °C）
 */
float mpu6500_parse_temperature(const struct mpu6500_sensor_data *raw)
{
	if (!raw)
		return 0.0f;

	return (raw->temp * TEMP_SCALE) + TEMP_OFFSET;
}

void mpu_raw_data_trans(const struct mpu6500_sensor_data *raw, struct mpu6500_actual_data *actual)
{
    if (!raw || !actual)
        return;

    mpu6500_parse_accel(raw, &actual->accel_x, &actual->accel_y, &actual->accel_z);
    mpu6500_parse_gyro(raw, &actual->gyro_x, &actual->gyro_y, &actual->gyro_z);
    actual->temp = mpu6500_parse_temperature(raw);
}