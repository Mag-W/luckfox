/* SPDX-License-Identifier: GPL-2.0 */
#ifndef FLASH_CDEV_H
#define FLASH_CDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FLASH_IOC_MAGIC     'F'

/* 读取 Flash：传入 flash_rw_arg */
#define FLASH_IOC_READ      _IOWR(FLASH_IOC_MAGIC, 1, struct flash_rw_arg)

/* 写入 Flash：传入 flash_rw_arg（驱动内部自动擦写）*/
#define FLASH_IOC_WRITE     _IOW(FLASH_IOC_MAGIC,  2, struct flash_rw_arg)

/* 擦除 Flash：传入 flash_erase_arg */
#define FLASH_IOC_ERASE     _IOW(FLASH_IOC_MAGIC,  3, struct flash_erase_arg)

/* 获取分区信息：传出 flash_info */
#define FLASH_IOC_GET_INFO  _IOR(FLASH_IOC_MAGIC,  4, struct flash_info)

/**
 * flash_rw_arg - 读/写参数
 * @offset: 分区内偏移（字节）
 * @buf:    用户空间缓冲区指针
 * @len:    操作长度（字节），实际完成长度回写此字段
 */
struct flash_rw_arg {
    __u32           offset;
    unsigned long   buf;    /* 用户空间缓冲区地址（__u64 在32位系统兼容） */
    __u32           len;
};

/**
 * flash_erase_arg - 擦除参数
 * @offset: 擦除起始偏移，必须是 erase_size 整数倍
 * @len:    擦除长度，必须是 erase_size 整数倍
 */
struct flash_erase_arg {
    __u32 offset;
    __u32 len;
};

/**
 * flash_info - 分区信息
 */
struct flash_info {
    __u32 total_size;
    __u32 erase_size;
    __u32 write_size;
    char  name[32];
};

#endif /* FLASH_CDEV_H */