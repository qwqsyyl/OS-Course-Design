// SPDX-License-Identifier: GPL-2.0
/*
 * vchar_device.c - a virtual character device for OS course design.
 *
 * Features:
 * - automatic major/minor allocation
 * - automatic /dev node creation
 * - one-page kernel buffer
 * - open/read/write/release callbacks
 * - independent offset for every open file instance
 * - sysfs status attribute for device state inspection
 */

#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#define DEVICE_NAME "vchar_device"
#define CLASS_NAME "vchar_device"

struct vchar_file_state {
	loff_t offset;
};

static dev_t vchar_devno;
static struct cdev vchar_cdev;
static struct class *vchar_class;
static struct device *vchar_device;

static char *vchar_buffer;
static size_t vchar_size;
static DEFINE_MUTEX(vchar_lock);

static atomic_t open_count = ATOMIC_INIT(0);
static atomic64_t total_reads = ATOMIC64_INIT(0);
static atomic64_t total_writes = ATOMIC64_INIT(0);

static int vchar_open(struct inode *inode, struct file *filp)
{
	struct vchar_file_state *state;

	(void)inode;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	state->offset = 0;
	filp->private_data = state;
	atomic_inc(&open_count);

	pr_info(DEVICE_NAME ": open, active=%d\n", atomic_read(&open_count));
	return 0;
}

static int vchar_release(struct inode *inode, struct file *filp)
{
	(void)inode;

	kfree(filp->private_data);
	filp->private_data = NULL;
	atomic_dec(&open_count);

	pr_info(DEVICE_NAME ": release, active=%d\n", atomic_read(&open_count));
	return 0;
}

static ssize_t vchar_read(struct file *filp, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct vchar_file_state *state = filp->private_data;
	size_t available;
	size_t to_copy;
	ssize_t ret;

	if (!state)
		return -EIO;

	if (count == 0)
		return 0;

	mutex_lock(&vchar_lock);

	if (state->offset >= vchar_size) {
		ret = 0;
		goto out_unlock;
	}

	available = vchar_size - state->offset;
	to_copy = min(count, available);

	if (copy_to_user(user_buf, vchar_buffer + state->offset, to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	state->offset += to_copy;
	*ppos = state->offset;
	atomic64_inc(&total_reads);
	ret = to_copy;

out_unlock:
	mutex_unlock(&vchar_lock);
	return ret;
}

static ssize_t vchar_write(struct file *filp, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct vchar_file_state *state = filp->private_data;
	size_t space;
	size_t to_copy;
	ssize_t ret;

	if (!state)
		return -EIO;

	if (count == 0)
		return 0;

	mutex_lock(&vchar_lock);

	if (state->offset >= PAGE_SIZE) {
		ret = -ENOSPC;
		goto out_unlock;
	}

	space = PAGE_SIZE - state->offset;
	to_copy = min(count, space);

	if (copy_from_user(vchar_buffer + state->offset, user_buf, to_copy)) {
		ret = -EFAULT;
		goto out_unlock;
	}

	state->offset += to_copy;
	if (state->offset > vchar_size)
		vchar_size = state->offset;

	*ppos = state->offset;
	atomic64_inc(&total_writes);
	ret = to_copy;

out_unlock:
	mutex_unlock(&vchar_lock);
	return ret;
}

static loff_t vchar_llseek(struct file *filp, loff_t offset, int whence)
{
	struct vchar_file_state *state = filp->private_data;
	loff_t new_offset;

	if (!state)
		return -EIO;

	mutex_lock(&vchar_lock);

	switch (whence) {
	case SEEK_SET:
		new_offset = offset;
		break;
	case SEEK_CUR:
		new_offset = state->offset + offset;
		break;
	case SEEK_END:
		new_offset = (loff_t)vchar_size + offset;
		break;
	default:
		mutex_unlock(&vchar_lock);
		return -EINVAL;
	}

	if (new_offset < 0 || new_offset > PAGE_SIZE) {
		mutex_unlock(&vchar_lock);
		return -EINVAL;
	}

	state->offset = new_offset;
	filp->f_pos = new_offset;

	mutex_unlock(&vchar_lock);
	return new_offset;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	size_t size_snapshot;

	(void)dev;
	(void)attr;

	mutex_lock(&vchar_lock);
	size_snapshot = vchar_size;
	mutex_unlock(&vchar_lock);

	return scnprintf(buf, PAGE_SIZE,
			 "device=%s\nmajor=%d\nminor=%d\nbuffer_size=%zu\nbuffer_capacity=%lu\nopen_count=%d\ntotal_reads=%lld\ntotal_writes=%lld\n",
			 DEVICE_NAME, MAJOR(vchar_devno), MINOR(vchar_devno),
			 size_snapshot, PAGE_SIZE, atomic_read(&open_count),
			 atomic64_read(&total_reads),
			 atomic64_read(&total_writes));
}

static DEVICE_ATTR_RO(status);

static const struct file_operations vchar_fops = {
	.owner = THIS_MODULE,
	.open = vchar_open,
	.read = vchar_read,
	.write = vchar_write,
	.llseek = vchar_llseek,
	.release = vchar_release,
};

static int __init vchar_init(void)
{
	int ret;

	vchar_buffer = (char *)get_zeroed_page(GFP_KERNEL);
	if (!vchar_buffer)
		return -ENOMEM;

	ret = alloc_chrdev_region(&vchar_devno, 0, 1, DEVICE_NAME);
	if (ret)
		goto err_free_buffer;

	cdev_init(&vchar_cdev, &vchar_fops);
	vchar_cdev.owner = THIS_MODULE;

	ret = cdev_add(&vchar_cdev, vchar_devno, 1);
	if (ret)
		goto err_unregister;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	vchar_class = class_create(CLASS_NAME);
#else
	vchar_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(vchar_class)) {
		ret = PTR_ERR(vchar_class);
		goto err_cdev;
	}

	vchar_device = device_create(vchar_class, NULL, vchar_devno, NULL,
				     DEVICE_NAME);
	if (IS_ERR(vchar_device)) {
		ret = PTR_ERR(vchar_device);
		goto err_class;
	}

	ret = device_create_file(vchar_device, &dev_attr_status);
	if (ret)
		goto err_device;

	pr_info(DEVICE_NAME ": loaded major=%d minor=%d capacity=%lu\n",
		MAJOR(vchar_devno), MINOR(vchar_devno), PAGE_SIZE);
	return 0;

err_device:
	device_destroy(vchar_class, vchar_devno);
err_class:
	class_destroy(vchar_class);
err_cdev:
	cdev_del(&vchar_cdev);
err_unregister:
	unregister_chrdev_region(vchar_devno, 1);
err_free_buffer:
	free_page((unsigned long)vchar_buffer);
	return ret;
}

static void __exit vchar_exit(void)
{
	device_remove_file(vchar_device, &dev_attr_status);
	device_destroy(vchar_class, vchar_devno);
	class_destroy(vchar_class);
	cdev_del(&vchar_cdev);
	unregister_chrdev_region(vchar_devno, 1);
	free_page((unsigned long)vchar_buffer);

	pr_info(DEVICE_NAME ": unloaded\n");
}

module_init(vchar_init);
module_exit(vchar_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Course Design Team");
MODULE_DESCRIPTION("Virtual character device driver with one-page kernel buffer");
MODULE_VERSION("1.0");
