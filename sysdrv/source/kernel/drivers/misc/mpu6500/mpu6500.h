#ifndef __MPU6500_H__
#define __MPU6500_H__

#include <linux/types.h>
#include <linux/spi/spi.h>

/* MPU6500 寄存器定义 */
#define MPU6500_REG_WHOAMI         0x75
#define MPU6500_WHOAMI_VALUE       0x70
#define MPU6500_REG_PWR_MGMT_1     0x6B
#define MPU6500_REG_CONFIG         0x1A
#define MPU6500_REG_GYRO_CONFIG    0x1B
#define MPU6500_REG_ACCEL_CONFIG   0x1C
#define MPU6500_REG_ACCEL_CONFIG2  0x1D
#define MPU6500_REG_INT_ENABLE     0x38
#define MPU6500_REG_INT_STATUS     0x3A

#define MPU6500_REG_ACCEL_XOUT_H   0x3B
#define MPU6500_REG_ACCEL_YOUT_H   0x3D
#define MPU6500_REG_ACCEL_ZOUT_H   0x3F
#define MPU6500_REG_TEMP_OUT_H     0x41
#define MPU6500_REG_GYRO_XOUT_H    0x43
#define MPU6500_REG_GYRO_YOUT_H    0x45
#define MPU6500_REG_GYRO_ZOUT_H    0x47

/* 传感器数据结构 */
struct mpu6500_sensor_data {
	s16 accel_x;
	s16 accel_y;
	s16 accel_z;
	s16 gyro_x;
	s16 gyro_y;
	s16 gyro_z;
	s16 temp;
};

/* 驱动设备结构 */
struct mpu6500_device {
	struct spi_device *spi;
	struct device *dev;
	struct miscdevice misc_dev;
	struct mpu6500_sensor_data sensor_data;
	struct mutex data_lock;
	u8 tx_buffer[64];
	u8 rx_buffer[64];
};

int mpu6500_write_reg(struct mpu6500_device *dev, u8 reg, u8 value);
int mpu6500_read_reg(struct mpu6500_device *dev, u8 reg, u8 *value);
int mpu6500_read_bytes(struct mpu6500_device *dev, u8 reg, u8 *data, u8 len);
int mpu6500_read_sensor_data(struct mpu6500_device *dev);

#endif