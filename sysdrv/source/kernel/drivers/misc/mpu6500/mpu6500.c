#include <linux/init.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "mpu6500.h"

#define IOCTL_MPU6500_READ_DATA  _IOR('m', 1, struct mpu6500_sensor_data)

/* ===== SPI 操作 ===== */
int mpu6500_write_reg(struct mpu6500_device *dev, u8 reg, u8 value)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;

	if (!dev || !dev->spi)
		return -EINVAL;

	memset(&xfer, 0, sizeof(xfer));
	dev->tx_buffer[0] = reg & 0x7F;  /* 写操作，MSB=0 */
	dev->tx_buffer[1] = value;

	xfer.tx_buf = dev->tx_buffer;
	xfer.len = 2;
	xfer.speed_hz = 1000000;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(dev->spi, &msg);
	if (ret < 0)
		dev_err(dev->dev, "SPI write failed: %d\n", ret);

	return ret;
}

int mpu6500_read_reg(struct mpu6500_device *dev, u8 reg, u8 *value)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;

	if (!dev || !dev->spi || !value)
		return -EINVAL;

	memset(&xfer, 0, sizeof(xfer));
	dev->tx_buffer[0] = reg | 0x80;  /* 读操作，MSB=1 */
	dev->tx_buffer[1] = 0;

	xfer.tx_buf = dev->tx_buffer;
	xfer.rx_buf = dev->rx_buffer;
	xfer.len = 2;
	xfer.speed_hz = 1000000;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(dev->spi, &msg);  /*同步传输，阻塞传输*/
	if (ret < 0) {
		dev_err(dev->dev, "SPI read failed: %d\n", ret);
		return ret;
	}

	*value = dev->rx_buffer[1];
	return 0;
}

int mpu6500_read_bytes(struct mpu6500_device *dev, u8 reg, u8 *data, u8 len)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	int ret;

	if (!dev || !dev->spi || !data || len > 62)
		return -EINVAL;

	memset(&xfer, 0, sizeof(xfer));
	dev->tx_buffer[0] = reg | 0x80;
	memset(&dev->tx_buffer[1], 0, len);

	xfer.tx_buf = dev->tx_buffer;
	xfer.rx_buf = dev->rx_buffer;
	xfer.len = len + 1;
	xfer.speed_hz = 1000000;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(dev->spi, &msg);
	if (ret < 0) {
		dev_err(dev->dev, "SPI read bytes failed: %d\n", ret);
		return ret;
	}

	memcpy(data, &dev->rx_buffer[1], len);
	return 0;
}

/* ===== 传感器数据读取 ===== */
int mpu6500_read_sensor_data(struct mpu6500_device *dev)
{
	u8 data[14];
	int ret;

	if (!dev)
		return -EINVAL;

	mutex_lock(&dev->data_lock);

	ret = mpu6500_read_bytes(dev, MPU6500_REG_ACCEL_XOUT_H, data, 14);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to read sensor data\n");
		mutex_unlock(&dev->data_lock);
		return ret;
	}

	dev->sensor_data.accel_x = (s16)((data[0] << 8) | data[1]);
	dev->sensor_data.accel_y = (s16)((data[2] << 8) | data[3]);
	dev->sensor_data.accel_z = (s16)((data[4] << 8) | data[5]);
	dev->sensor_data.temp = (s16)((data[6] << 8) | data[7]);
	dev->sensor_data.gyro_x = (s16)((data[8] << 8) | data[9]);
	dev->sensor_data.gyro_y = (s16)((data[10] << 8) | data[11]);
	dev->sensor_data.gyro_z = (s16)((data[12] << 8) | data[13]);

	mutex_unlock(&dev->data_lock);
	return 0;
}

/* ===== 硬件初始化 ===== */
static int mpu6500_hw_init(struct mpu6500_device *dev)
{
	u8 chip_id;
	int ret;

	dev_info(dev->dev, "MPU6500 hardware initializing...\n");

	ret = mpu6500_read_reg(dev, MPU6500_REG_WHOAMI, &chip_id);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to read WHO_AM_I register\n");
		return ret;
	}

	dev_info(dev->dev, "MPU6500 WHO_AM_I: 0x%02X\n", chip_id);
	if (chip_id != MPU6500_WHOAMI_VALUE) {
		dev_err(dev->dev, "Invalid chip ID: 0x%02X (expected 0x%02X)\n",
			chip_id, MPU6500_WHOAMI_VALUE);
		return -ENODEV;
	}

	/* 清除电源管理，唤醒芯片 */
	ret = mpu6500_write_reg(dev, MPU6500_REG_PWR_MGMT_1, 0x00);
	if (ret < 0)
		return ret;
	msleep(10);

	/* 设置配置 */
	mpu6500_write_reg(dev, MPU6500_REG_CONFIG, 0x06);
	mpu6500_write_reg(dev, MPU6500_REG_GYRO_CONFIG, 0x00);
	mpu6500_write_reg(dev, MPU6500_REG_ACCEL_CONFIG, 0x00);
	mpu6500_write_reg(dev, MPU6500_REG_ACCEL_CONFIG2, 0x06);
	mpu6500_write_reg(dev, MPU6500_REG_INT_ENABLE, 0x01);

	dev_info(dev->dev, "MPU6500 hardware initialized successfully\n");
	return 0;
}

/* ===== Misc 设备接口 ===== 
* 把 Misc 框架默认保存的 struct miscdevice *，
* 转换成你自己的 struct mpu6500_device *，
* 然后存回 file->private_data，
* 方便后续 read/write/ioctl 直接访问设备私有数据。*/
static int mpu6500_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct mpu6500_device *dev = container_of(misc, struct mpu6500_device, misc_dev);
	file->private_data = dev;
	return 0;
}

static ssize_t mpu6500_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct mpu6500_device *dev = file->private_data;
	struct mpu6500_sensor_data data;
	int ret;

	if (count < sizeof(data))
		return -EINVAL;

	ret = mpu6500_read_sensor_data(dev);
	if (ret < 0)
		return ret;

	mutex_lock(&dev->data_lock);
	data = dev->sensor_data;
	mutex_unlock(&dev->data_lock);

	if (copy_to_user(buf, &data, sizeof(data)))
		return -EFAULT;

	return sizeof(data);
}

static long mpu6500_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct mpu6500_device *dev = file->private_data;
	struct mpu6500_sensor_data data;
	int ret = 0;

	switch (cmd) {
	case IOCTL_MPU6500_READ_DATA:
		ret = mpu6500_read_sensor_data(dev);
		if (ret < 0)
			return ret;

		mutex_lock(&dev->data_lock);
		data = dev->sensor_data;
		mutex_unlock(&dev->data_lock);

		if (copy_to_user((struct mpu6500_sensor_data __user *)arg, &data, sizeof(data)))
			return -EFAULT;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static const struct file_operations mpu6500_fops = {
	.owner = THIS_MODULE,
	.open = mpu6500_open,
	.read = mpu6500_read,
	.unlocked_ioctl = mpu6500_ioctl,
};

/* ===== SPI 驱动 ===== */
static int mpu6500_probe(struct spi_device *spi)
{
	struct mpu6500_device *dev;
	int ret;

	dev_info(&spi->dev, "MPU6500 SPI probe\n"); 

	dev = devm_kzalloc(&spi->dev, sizeof(struct mpu6500_device), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->spi = spi;
	dev->dev = &spi->dev;
	mutex_init(&dev->data_lock);
	spi_set_drvdata(spi, dev);

	/* SPI 配置 */
	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	spi->max_speed_hz = 1000000;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	/* 硬件初始化 */
	ret = mpu6500_hw_init(dev);
	if (ret < 0)
		return ret;

	/* 注册 misc 设备 */
	dev->misc_dev.minor = MISC_DYNAMIC_MINOR;
	dev->misc_dev.name = "mpu6500";
	dev->misc_dev.fops = &mpu6500_fops;
	ret = misc_register(&dev->misc_dev);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to register misc device\n");
		return ret;
	}

	dev_info(&spi->dev, "MPU6500 driver probe successfully\n");
	return 0;
}

static int mpu6500_remove(struct spi_device *spi)
{
	struct mpu6500_device *dev = spi_get_drvdata(spi);
	misc_deregister(&dev->misc_dev);
	return 0;
}

/* ===== 关键：of_match_table（用于设备树匹配） ===== */
static const struct of_device_id mpu6500_of_match[] = {
	{
		.compatible = "invensense,mpu6500",
		.data = NULL,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, mpu6500_of_match);

static const struct spi_device_id mpu6500_id_table[] = {
	{ "mpu6500", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, mpu6500_id_table);

static struct spi_driver mpu6500_spi_driver = {
	.driver = {
		.name = "mpu6500",
		.owner = THIS_MODULE,
		.of_match_table = mpu6500_of_match,  /* ← 设备树匹配 */
	},
	.id_table = mpu6500_id_table,   /* ← 非设备树匹配 */
	.probe = mpu6500_probe,
	.remove = mpu6500_remove,
};

static int __init mpu6500_init(void)
{
	return spi_register_driver(&mpu6500_spi_driver);
}

static void __exit mpu6500_exit(void)
{
	spi_unregister_driver(&mpu6500_spi_driver);
}

module_init(mpu6500_init);
module_exit(mpu6500_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("MPU6500 6-Axis Motion Sensor Driver");