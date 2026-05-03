// SPDX-License-Identifier: GPL-2.0
/**
 * @file  flash_cdev.c
 * @brief Flash 字符设备驱动
 *
 * 所有操作均通过 ioctl 完成：
 *   FLASH_IOC_READ     读取 Flash
 *   FLASH_IOC_WRITE    写入 Flash（内部自动 Read-Modify-Write）
 *   FLASH_IOC_ERASE    擦除 Flash
 *   FLASH_IOC_GET_INFO 获取分区信息
 *
 * 加载示例：
 *   insmod flash_cdev.ko mtd_index=5
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/mtd/mtd.h>
#include <linux/math64.h>

#include "flash_cdev.h"

/* ------------------------------------------------------------------ */
/* 模块参数                                                              */
/* ------------------------------------------------------------------ */
static int mtd_index = 5;
module_param(mtd_index, int, 0444);
MODULE_PARM_DESC(mtd_index, "MTD partition index (default: 5 = userdata)");

/* ------------------------------------------------------------------ */
/* 驱动内部结构                                                          */
/* ------------------------------------------------------------------ */
#define DEVICE_NAME  "flash_rw"
#define CLASS_NAME   "flash_cdev"
#define RW_BUF_SIZE  (4096)          /* 单次 DMA 缓冲最大值 */

struct flash_cdev_dev {
    struct cdev       cdev;
    struct class     *cls;
    struct device    *dev;
    dev_t             devno;
    struct mtd_info  *mtd;
    struct mutex      lock;
};

static struct flash_cdev_dev g_flash_dev;

/* ------------------------------------------------------------------ */
/* open / release                                                       */
/* ------------------------------------------------------------------ */
static int flash_cdev_open(struct inode *inode, struct file *filp)
{
    filp->private_data = &g_flash_dev;
    pr_info("flash_cdev: opened (mtd%d '%s')\n",
            mtd_index, g_flash_dev.mtd->name);
    return 0;
}

static int flash_cdev_release(struct inode *inode, struct file *filp)
{
    pr_info("flash_cdev: closed\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* 内部：分段读取（offset 到 offset+len）                                */
/* ------------------------------------------------------------------ */
static int _flash_read(struct mtd_info *mtd, uint32_t offset,
                       void __user *ubuf, uint32_t len, uint32_t *done)
{
    uint8_t *kbuf;
    uint32_t remaining = len;
    uint32_t pos       = offset;
    uint32_t copied    = 0;
    int      ret       = 0;

    kbuf = kmalloc(RW_BUF_SIZE, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    while (remaining > 0) {
        uint32_t chunk  = min_t(uint32_t, remaining, RW_BUF_SIZE);
        size_t   retlen = 0;

        ret = mtd_read(mtd, (loff_t)pos, chunk, &retlen, kbuf);
        if (ret && ret != -EUCLEAN) {
            pr_err("flash_cdev: mtd_read at 0x%x failed: %d\n", pos, ret);
            goto out;
        }
        ret = 0;

        if (copy_to_user(ubuf + copied, kbuf, retlen)) {
            ret = -EFAULT;
            goto out;
        }

        pos       += retlen;
        copied    += retlen;
        remaining -= retlen;
    }

    *done = copied;
out:
    kfree(kbuf);
    return ret;
}

/* ------------------------------------------------------------------ */
/* 内部：擦除（自动按块对齐）                                            */
/* ------------------------------------------------------------------ */
static int _flash_erase(struct mtd_info *mtd, uint32_t offset, uint32_t len)
{
    struct erase_info ei;
    uint32_t es;
    loff_t   erase_start;
    loff_t   erase_end;
    uint64_t nblocks;

    es          = mtd->erasesize;
    erase_start = div_u64((uint64_t)offset, es) * (uint64_t)es;
    erase_end   = div_u64((uint64_t)offset + len + es - 1, es) * (uint64_t)es;
    nblocks     = div_u64(erase_end - erase_start, es);

    memset(&ei, 0, sizeof(ei));
    ei.addr = erase_start;
    ei.len  = erase_end - erase_start;

    pr_info("flash_cdev: erase 0x%llx ~ 0x%llx (%llu blocks)\n",
            erase_start, erase_end, nblocks);

    return mtd_erase(mtd, &ei);
}

/* ------------------------------------------------------------------ */
/* 内部：分段写入（Read-Modify-Write，保护块内相邻数据）                 */
/* ------------------------------------------------------------------ */
static int _flash_write(struct mtd_info *mtd, uint32_t offset,
                        const void __user *ubuf, uint32_t len)
{
    uint32_t es       = mtd->erasesize;
    uint32_t remaining = len;
    uint32_t pos      = offset;
    int      ret      = 0;
    size_t retlen = 0;

    uint8_t *kbuf   = kmalloc(RW_BUF_SIZE, GFP_KERNEL);
    uint8_t *blkbuf = kmalloc(es,          GFP_KERNEL);
    if (!kbuf || !blkbuf) {
        ret = -ENOMEM;
        goto out;
    }

    while (remaining > 0) {
        /* 计算当前 pos 所在块的边界 */
        uint32_t blk_start = (pos / es) * es;
        uint32_t blk_off   = pos - blk_start;
        uint32_t chunk     = min_t(uint32_t, remaining, es - blk_off);
        chunk = min_t(uint32_t, chunk, RW_BUF_SIZE);

        retlen = 0;

        /* 1. 读出整块 */
        ret = mtd_read(mtd, (loff_t)blk_start, es, &retlen, blkbuf);
        if (ret && ret != -EUCLEAN) {
            pr_err("flash_cdev: pre-read at 0x%x failed: %d\n",
                   blk_start, ret);
            goto out;
        }
        ret = 0;

        /* 2. 拷贝用户数据到块缓冲对应位置 */
        if (copy_from_user(blkbuf + blk_off,
                           ubuf + (len - remaining),
                           chunk)) {
            ret = -EFAULT;
            goto out;
        }

        /* 3. 擦除该块 */
        ret = _flash_erase(mtd, blk_start, es);
        if (ret) {
            pr_err("flash_cdev: erase at 0x%x failed: %d\n",
                   blk_start, ret);
            goto out;
        }

        /* 4. 回写整块 */
        ret = mtd_write(mtd, (loff_t)blk_start, es, &retlen, blkbuf);
        if (ret) {
            pr_err("flash_cdev: write at 0x%x failed: %d\n",
                   blk_start, ret);
            goto out;
        }

        pos       += chunk;
        remaining -= chunk;
    }

out:
    kfree(kbuf);
    kfree(blkbuf);
    return ret;
}

/* ------------------------------------------------------------------ */
/* ioctl — 所有操作的统一入口                                            */
/* ------------------------------------------------------------------ */
static long flash_cdev_ioctl(struct file *filp, unsigned int cmd,
                              unsigned long arg)
{
    struct flash_cdev_dev *fdev = filp->private_data;
    struct mtd_info       *mtd  = fdev->mtd;
    int ret = 0;
    uint32_t done = 0;

    switch (cmd) {

    /* ---- 读取 ---- */
    case FLASH_IOC_READ: {
        struct flash_rw_arg rw;

        if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
            return -EFAULT;

        if (rw.offset + rw.len > mtd->size)
            return -EINVAL;

        done = 0;

        mutex_lock(&fdev->lock);
        ret = _flash_read(mtd, rw.offset,
                          (void __user *)(unsigned long)rw.buf,
                          rw.len, &done);
        mutex_unlock(&fdev->lock);

        if (ret == 0) {
            /* 回写实际读取长度 */
            rw.len = done;
            if (copy_to_user((void __user *)arg, &rw, sizeof(rw)))
                return -EFAULT;
        }
        break;
    }

    /* ---- 写入 ---- */
    case FLASH_IOC_WRITE: {
        struct flash_rw_arg rw;

        if (copy_from_user(&rw, (void __user *)arg, sizeof(rw)))
            return -EFAULT;

        if (rw.offset + rw.len > mtd->size)
            return -EINVAL;

        mutex_lock(&fdev->lock);
        ret = _flash_write(mtd, rw.offset,
                           (const void __user *)(unsigned long)rw.buf,
                           rw.len);
        mutex_unlock(&fdev->lock);
        break;
    }

    /* ---- 擦除 ---- */
    case FLASH_IOC_ERASE: {
        struct flash_erase_arg ea;

        if (copy_from_user(&ea, (void __user *)arg, sizeof(ea)))
            return -EFAULT;

        if (ea.offset % mtd->erasesize || ea.len % mtd->erasesize) {
            pr_err("flash_cdev: erase not aligned to erasesize=%u\n",
                   mtd->erasesize);
            return -EINVAL;
        }

        if ((uint64_t)ea.offset + ea.len > mtd->size)
            return -EINVAL;

        mutex_lock(&fdev->lock);
        ret = _flash_erase(mtd, ea.offset, ea.len);
        mutex_unlock(&fdev->lock);
        break;
    }

    /* ---- 获取分区信息 ---- */
    case FLASH_IOC_GET_INFO: {
        struct flash_info info;

        memset(&info, 0, sizeof(info));
        info.total_size = (uint32_t)mtd->size;
        info.erase_size = mtd->erasesize;
        info.write_size = mtd->writesize;
        strncpy(info.name, mtd->name, sizeof(info.name) - 1);

        if (copy_to_user((void __user *)arg, &info, sizeof(info)))
            return -EFAULT;
        break;
    }

    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

/* ------------------------------------------------------------------ */
/* file_operations — 只开放 ioctl，不提供 read/write                    */
/* ------------------------------------------------------------------ */
static const struct file_operations flash_cdev_fops = {
    .owner          = THIS_MODULE,
    .open           = flash_cdev_open,
    .release        = flash_cdev_release,
    .unlocked_ioctl = flash_cdev_ioctl,
};

/* ------------------------------------------------------------------ */
/* 模块初始化                                                            */
/* ------------------------------------------------------------------ */
static int __init flash_cdev_init(void)
{
    int ret;

    g_flash_dev.mtd = get_mtd_device(NULL, mtd_index);
    if (IS_ERR(g_flash_dev.mtd)) {
        pr_err("flash_cdev: get_mtd_device(%d) failed\n", mtd_index);
        return PTR_ERR(g_flash_dev.mtd);
    }

    pr_info("flash_cdev: MTD%d '%s' size=%llu KB erase=%u KB\n",
            mtd_index,
            g_flash_dev.mtd->name,
            (unsigned long long)g_flash_dev.mtd->size / 1024,
            g_flash_dev.mtd->erasesize / 1024);

    mutex_init(&g_flash_dev.lock);

    ret = alloc_chrdev_region(&g_flash_dev.devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("flash_cdev: alloc_chrdev_region failed: %d\n", ret);
        goto err_put_mtd;
    }

    cdev_init(&g_flash_dev.cdev, &flash_cdev_fops);
    g_flash_dev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&g_flash_dev.cdev, g_flash_dev.devno, 1);
    if (ret < 0) {
        pr_err("flash_cdev: cdev_add failed: %d\n", ret);
        goto err_unreg_chrdev;
    }

    g_flash_dev.cls = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(g_flash_dev.cls)) {
        ret = PTR_ERR(g_flash_dev.cls);
        goto err_cdev_del;
    }

    g_flash_dev.dev = device_create(g_flash_dev.cls, NULL,
                                     g_flash_dev.devno, NULL,
                                     DEVICE_NAME);
    if (IS_ERR(g_flash_dev.dev)) {
        ret = PTR_ERR(g_flash_dev.dev);
        goto err_class_destroy;
    }

    pr_info("flash_cdev: /dev/%s ready (major=%d)\n",
            DEVICE_NAME, MAJOR(g_flash_dev.devno));
    return 0;

err_class_destroy:
    class_destroy(g_flash_dev.cls);
err_cdev_del:
    cdev_del(&g_flash_dev.cdev);
err_unreg_chrdev:
    unregister_chrdev_region(g_flash_dev.devno, 1);
err_put_mtd:
    put_mtd_device(g_flash_dev.mtd);
    return ret;
}

/* ------------------------------------------------------------------ */
/* 模块退出                                                              */
/* ------------------------------------------------------------------ */
static void __exit flash_cdev_exit(void)
{
    device_destroy(g_flash_dev.cls, g_flash_dev.devno);
    class_destroy(g_flash_dev.cls);
    cdev_del(&g_flash_dev.cdev);
    unregister_chrdev_region(g_flash_dev.devno, 1);
    put_mtd_device(g_flash_dev.mtd);
    pr_info("flash_cdev: unloaded\n");
}

late_initcall(flash_cdev_init);
module_exit(flash_cdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mag-W");
MODULE_DESCRIPTION("Flash character device driver via MTD (ioctl only)");
MODULE_VERSION("1.1.0");