/// @file aesdchar.c
/// @brief Functions and data related to the AESD char driver implementation
///
/// Based on the implementation of the "scull" device driver, found in
/// Linux Device Drivers example code.
///
/// @author Dan Walkes
/// @date 2019-10-22
/// @copyright Copyright (c) 2019

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations

#include "aesdchar.h"

#ifndef __KERNEL__
#ifdef AESD_DEBUG
#include <stdio.h>
#endif
#endif

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

#ifdef __KERNEL__
MODULE_AUTHOR("Sean Sweet");
MODULE_LICENSE("Dual BSD/GPL");
#endif

AesdDevice aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");

    AesdDevice *device = container_of(inode->i_cdev, AesdDevice, cdev);
    filp->private_data = device;
    if (filp->f_flags & O_ACCMODE == O_WRONLY)
    {
        if (mutex_lock_interruptible(device->device_mutex))
        {
            return -ERESTARTSYS;
        }

        clear_buffer(device->buffer);
        if (device->current_write != NULL)
        {
            kfree(device->current_write);
            device->current_write = NULL;
            device->current_write_len = 0;
        }

        mutex_unlock(device->device_mutex);
    }

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t bytes_read = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    if (filp->f_flags & O_ACCMODE == O_WRONLY)
    {
        return -EPERM;
    }

    AesdDevice *device = (AesdDevice *)filp->private_data;

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        goto device_mutex_lock_failed;
    }

    size_t entry_offset = 0;
    AesdBufferEntry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(device->buffer, *f_pos, &entry_offset);
    if (entry == NULL)
    {
        goto close_function;
    }

    size_t str_len = 0;
    size_t copy_len = 0;
    while (bytes_read < count)
    {
        str_len = entry->size - entry_offset;
        copy_len = (count - bytes_read < str_len) ? count - bytes_read : str_len;
        copy_to_user(buf + bytes_read, entry->buffptr + entry_offset, copy_len);

        bytes_read += copy_len;
        entry_offset = 0;
        entry = next_entry(device->buffer, entry);
        if (entry == NULL)
        {
            goto close_function;
        }
    }

close_function:
    (*f_pos) += bytes_read;
    mutex_unlock(device->device_mutex);
device_mutex_lock_failed:
    return bytes_read;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = count;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    if (filp->f_flags & O_ACCMODE == O_RDONLY)
    {
        return -EPERM;
    }

    AesdDevice *device = filp->private_data;

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        retval = -ERESTARTSYS;
        goto device_mutex_lock_failed;
    }

    if (device->current_write != NULL)
    {
        const char *temp_write = kmalloc_array(device->current_write_len + count, sizeof(char), GFP_KERNEL);
        if (temp_write == NULL)
        {
            retval = -ENOMEM;
            goto str_malloc_failed;
        }

        memset(temp_write, 0, device->current_write_len + count);

        memcpy(temp_write, device->current_write, device->current_write_len);
        retval -= copy_from_user(temp_write + device->current_write_len, buf, count);
        kfree(device->current_write);
        device->current_write = temp_write;
        device->current_write_len += count;
    }
    else
    {
        device->current_write = kmalloc_array(count, sizeof(char), GFP_KERNEL);
        if (device->current_write == NULL)
        {
            retval = -ENOMEM;
            goto str_malloc_failed;
        }

        memset(device->current_write, 0, count);

        retval -= copy_from_user(device->current_write, buf, count);
        device->current_write_len = count;
    }

    if (device->current_write[device->current_write_len - 1] == '\n')
    {
        AesdBufferEntry entry = {
            .buffptr = device->current_write,
            .size = device->current_write_len,
        };

        AesdBufferEntry *result = aesd_circular_buffer_add_entry(device->buffer, &entry);
        device->current_write = NULL;
        device->current_write_len = 0;
        if (result != NULL)
        {
            kfree(result);
        }
    }

str_malloc_failed:
    mutex_unlock(device->device_mutex);
device_mutex_lock_failed:
    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(AesdDevice *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        goto alloc_chrdev_failed;
    }

    memset(&aesd_device, 0, sizeof(AesdDevice));

    aesd_device.buffer = (AesdCircularBuffer *)kmalloc(sizeof(AesdCircularBuffer), GFP_KERNEL);
    if (aesd_device.buffer == NULL)
    {
        result = ENOMEM;
        goto buffer_malloc_failed;
    }

    memset(aesd_device.buffer, 0, sizeof(AesdCircularBuffer));

    aesd_device.device_mutex = (struct mutex *)kmalloc(sizeof(struct mutex), GFP_KERNEL);
    if (aesd_device.device_mutex == NULL)
    {
        result = ENOMEM;
    }

    memset(aesd_device.device_mutex, 0, sizeof(struct mutex));
    mutex_init(aesd_device.device_mutex);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        goto setup_cdev_failed;
    }

    return 0;

setup_cdev_failed:
    kfree(aesd_device.device_mutex);
device_mutex_malloc_failed:
    kfree(aesd_device.buffer);
buffer_malloc_failed:
    unregister_chrdev_region(dev, 1);
alloc_chrdev_failed:
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    clear_buffer(aesd_device.buffer);
    kfree(aesd_device.buffer);

    mutex_destroy(aesd_device.device_mutex);
    kfree(aesd_device.device_mutex);

    if (aesd_device.current_write != NULL)
    {
        kfree(aesd_device.current_write);
        aesd_device.current_write = NULL;
        aesd_device.current_write_len = 0;
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
