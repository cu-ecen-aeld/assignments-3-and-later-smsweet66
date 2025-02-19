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
#include "aesd_ioctl.h"

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
    AesdDevice *device;

    PDEBUG("open\n");

    device = container_of(inode->i_cdev, AesdDevice, cdev);
    filp->private_data = device;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release\n");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t bytes_read = 0;
    size_t entry_offset = 0;
    size_t str_len = 0;
    size_t copy_len = 0;
    AesdDevice *device;
    AesdBufferEntry *entry;

    PDEBUG("read %zu bytes with offset %lld\n", count, *f_pos);
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY)
    {
        return -EPERM;
    }

    device = (AesdDevice *)filp->private_data;

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        goto device_mutex_lock_failed;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(device->buffer, *f_pos, &entry_offset);
    if (entry == NULL)
    {
        goto close_function;
    }

    while (bytes_read < count)
    {
        str_len = entry->size - entry_offset;
        copy_len = (count - bytes_read < str_len) ? count - bytes_read : str_len;
        bytes_read += copy_len - copy_to_user(buf + bytes_read, entry->buffptr + entry_offset, copy_len);

        entry_offset = 0;
        entry = aesd_circular_buffer_next_entry(device->buffer, entry);
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
    AesdDevice *device;
    const char *result;
    AesdBufferEntry entry;

    PDEBUG("write %zu bytes with offset %lld\n", count, *f_pos);
    if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
    {
        return -EPERM;
    }

    device = filp->private_data;

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        retval = -ERESTARTSYS;
        goto device_mutex_lock_failed;
    }

    if (device->current_write != NULL)
    {
        char *temp_write = kmalloc_array(device->current_write_len + count, sizeof(char), GFP_KERNEL);
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
        entry = (AesdBufferEntry){
            .buffptr = device->current_write,
            .size = device->current_write_len,
        };

        result = aesd_circular_buffer_add_entry(device->buffer, &entry);
        device->current_write = NULL;
        device->current_write_len = 0;
        if (result != NULL)
        {
            kfree(result);
            result = NULL;
        }
    }

    (*f_pos) += retval;
str_malloc_failed:
    mutex_unlock(device->device_mutex);
device_mutex_lock_failed:
    return retval;
}

loff_t aesd_seek(struct file *filp, loff_t f_pos, int whence)
{
    AesdDevice *device;
    size_t file_size;

    PDEBUG("Seek to position %zu from location %d\n");

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        return -ERESTARTSYS;
    }

    file_size = aesd_circular_buffer_size(device->buffer);

    mutex_unlock(device->device_mutex);

    return fixed_size_llseek(filp, f_pos, whence, file_size);
}

long aesd_adjust_file_offset(struct file *filp, uint32_t command, uint32_t command_offset)
{
    AesdDevice *device;
    long position = 0;
    size_t index = 0;
    size_t command_index = 0;

    PDEBUG("Seeking to position %u within command %u\n", command_offset, command);

    if (mutex_lock_interruptible(device->device_mutex) != 0)
    {
        return -ERESTARTSYS;
    }

    if ((device->buffer->in_offs == device->buffer->out_offs && !device->buffer->full) || command >= 10)
    {
        position = -EINVAL;
        goto early_return;
    }

    for (size_t i = 0; i < command; ++i)
    {
        index = (i + device->buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        position += device->buffer->entry[index].size;
    }

    command_index = (command + device->buffer->out_offs) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    if (command_offset >= device->buffer->entry[command_index].size)
    {
        position = -EINVAL;
    }
    else
    {
        position += command_offset;
    }

early_return:
    mutex_unlock(device->device_mutex);

    return aesd_seek(filp, position, SEEK_SET);
}

long aesd_ioctl(struct file *filp, unsigned int command, unsigned long arg)
{
    switch (command)
    {
    case AESDCHAR_IOCSEEKTO:
        AesdSeekTo seek_to = {0};
        if (copy_from_user(&seek_to, (const void __user *)arg, sizeof(AesdSeekTo)) != 0)
        {
            return EFAULT;
        }
        else
        {
            return aesd_adjust_file_offset(filp, seek_to.write_cmd, seek_to.write_cmd_offset);
        }
    default:
        return ENOTSUPP;
    }
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_seek,
    .unlocked_ioctl = aesd_ioctl,
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
        printk(KERN_ERR "Error %d adding aesd cdev\n", err);
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
        goto device_mutex_malloc_failed;
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

    aesd_circular_buffer_clear(aesd_device.buffer);
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
