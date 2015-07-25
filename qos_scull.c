/* Kernel module api */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <linux/cdev.h>
#include <linux/semaphore.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <linux/ioctl.h>

#include <linux/slab.h> /* kmalloc/kfree */
#include <asm/uaccess.h> /* [__]copy_to_user / [__]copy_from_user */
#include <linux/kernel.h> /* down_interruptible */

#include "qos_scull.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("qOS Scull driver");
MODULE_AUTHOR("Arthur SFEZ");

/* Param execution time */
int qos_scull_major = QOS_SCULL_MAJOR;
int qos_scull_minor = 0;
int qos_scull_no_devs = QOS_SCULL_NO;
int qos_scull_qset = QOS_SCULL_QSET;
int qos_scull_quantum = QOS_SCULL_QUANTUM;

module_param(qos_scull_major, int, S_IRUGO);
module_param(qos_scull_minor, int, S_IRUGO);
module_param(qos_scull_no_devs, int, S_IRUGO);
module_param(qos_scull_quantum, int, S_IRUGO);
module_param(qos_scull_qset, int, S_IRUGO);

dev_t major_devno;
struct qos_scull_dev *devs;

/* Proc file system functions */
static void *qos_scull_seq_start(struct seq_file *sfile, loff_t *pos)
{
	if (*pos >= qos_scull_no_devs)
		return NULL;
	return devs + *pos;
}

static void *qos_scull_seq_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	*pos = *pos + 1;
	if (*pos >= qos_scull_no_devs)
		return NULL;
	return devs + *pos;
}

static void qos_scull_seq_stop(struct seq_file *sfile, void *v)
{
	return ;
}

static int qos_scull_seq_show(struct seq_file *file, void *v)
{
	struct qos_scull_dev *dev = (struct qos_scull_dev *)v;
	struct qos_scull_qset *qset = dev->data;
	int i;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	seq_printf(file, "Device no %d:\n\tTotal sz %li\n",
			(int)(dev - devs), dev->size);
	for (i = 0; qset && qset->next; i++)
		qset = qset->next;
	if (qset) {
		seq_printf(file, "\tNumber of nodes : %d\n", i + 1);
		for (i = 0; qset->data && qset->data[i]; i++);
		if (i)
			seq_printf(file, "\tContent of quantum no %d:%8p\n",
					i - 1, qset->data[i - 1]);
	} else
		seq_printf(file, "\tNo nodes found\n");
	up(&dev->sem);
	return 0;
}

static struct seq_operations qos_scull_seq_ops = {
	.start = qos_scull_seq_start,
	.next = qos_scull_seq_next,
	.stop= qos_scull_seq_stop,
	.show= qos_scull_seq_show
};

static int qos_scull_proc_open(struct inode *inode, struct file *filp)
{
	return seq_open(filp, &qos_scull_seq_ops);
}

static struct file_operations qos_scull_proc_ops = {
	.owner = THIS_MODULE,
	.open = qos_scull_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release= seq_release,
};

/* Local functions */
static void qos_scull_trim(struct qos_scull_dev *dev)
{
	struct qos_scull_qset *cur, *next;
	int qset = dev->qset, i;

	for (cur = dev->data; cur; cur = next) {
		if (cur->data) {
			for (i = 0; i < qset; i++)
				kfree(cur->data[i]);
			kfree(cur->data);
			cur->data = NULL;
		}
		next = cur->next;
		kfree(cur);
	}
	dev->quantum = qos_scull_quantum;
	dev->qset = qos_scull_qset;
	dev->size = 0;
	dev->data = NULL;
}

static struct qos_scull_qset *qos_scull_follow(struct qos_scull_dev *dev,int i)
{
	struct qos_scull_qset *qset = dev->data;

	if (!qset) {
		qset = dev->data = kmalloc(sizeof(struct qos_scull_qset),
				GFP_KERNEL);
		if (!qset)
			return NULL;
		memset(qset, 0, sizeof(struct qos_scull_qset));
	}
	while (i--) {
		if (!qset->next) {
			qset->next = kmalloc(sizeof(struct qos_scull_qset),
					GFP_KERNEL);
			if (!qset->next)
				return NULL;
			memset(qset->next, 0, sizeof(struct qos_scull_qset));
		}
		qset = qset->next;
	}
	return qset;
}

/* File Operations functions */
static int qos_scull_open(struct inode *inode, struct file *filp)
{
	struct qos_scull_dev *dev;

	dev = container_of(inode->i_cdev, struct qos_scull_dev, cdev);
	filp->private_data = dev;
	if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
		if (down_interruptible(&dev->sem))
			return -ERESTARTSYS;
		qos_scull_trim(dev);
		up(&dev->sem);
	}
	return 0;
}

static int qos_scull_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t qos_scull_write(struct file *filp, char const __user *buf,
		size_t count, loff_t *f_pos)
{
	struct qos_scull_dev *dev = filp->private_data;
	struct qos_scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int node_size = quantum * qset;
	int node_pos, rest, set_pos, quantum_pos;
	ssize_t retval = -ENOMEM;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;

	node_pos = *f_pos / node_size;
	rest = *f_pos % node_size;
	set_pos = rest / quantum;
	quantum_pos = rest % quantum;

	dptr = qos_scull_follow(dev, node_pos);
	if (!dptr) {
		goto out;
	}
	if (!dptr->data) {
		dptr->data = kmalloc(sizeof(char *) * qset, GFP_KERNEL);
		if (!dptr->data) {
			goto out;
		}
		memset(dptr->data, 0, sizeof(char *) * qset);
	}
	if (!dptr->data[set_pos]) {
		dptr->data[set_pos] = kmalloc(quantum, GFP_KERNEL);
		if (!dptr->data[set_pos]) {
			goto out;
		}
	}
	if (count > quantum - quantum_pos)
		count = quantum - quantum_pos;
	if (copy_from_user(dptr->data[set_pos] + quantum_pos, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;
	if (dev->size < *f_pos)
		dev->size = *f_pos;

out:
	up(&dev->sem);
	return retval;
}

static loff_t qos_scull_llseek(struct file *filp, loff_t offset, int whence)
{
	struct qos_scull_dev *dev = filp->private_data;
	loff_t new_pos;
	switch (whence) {
		case SEEK_SET:
			new_pos = offset;
			break;
		case SEEK_CUR:
			new_pos = filp->f_pos + offset;
			break;
		case SEEK_END:
			new_pos = dev->size - offset;
			break;
		default:
			return -EINVAL;
	}
	if (new_pos < 0)
		return -EINVAL;
	filp->f_pos = new_pos;
	return new_pos;
}

static ssize_t qos_scull_read(struct file *filp, char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct qos_scull_dev *dev = filp->private_data;
	struct qos_scull_qset *dptr;
	int quantum = dev->quantum, qset = dev->qset;
	int node_size = quantum * qset;
	int node_pos, rest, set_pos, quantum_pos;
	ssize_t retval = 0;

	if (down_interruptible(&dev->sem))
		return -ERESTARTSYS;
	if (*f_pos >= dev->size)
		goto out;

	if (*f_pos + count > dev->size)
		count = dev->size - *f_pos;
	node_pos = (long)*f_pos / node_size;
	rest = (long)*f_pos % node_size;
	set_pos = rest / quantum;
	quantum_pos = rest % quantum;

	dptr = qos_scull_follow(dev, node_pos);
	if (!dptr || !dptr->data || !dptr->data[set_pos])
		goto out;
	if (count > quantum - quantum_pos)
		count = quantum - quantum_pos;
	if (copy_to_user(buf, dptr->data[set_pos] + quantum_pos, count)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += count;
	retval = count;
out:
	up(&dev->sem);
	return retval;
}

static struct file_operations qos_scull_fops = {
	.owner = THIS_MODULE,
	.open = &qos_scull_open,
	.release = &qos_scull_release,
	.llseek = qos_scull_llseek,
	.read = qos_scull_read,
	.write = qos_scull_write,
	/* .ioctl = qos_scull_ioctl, */
};

/* Initialization and release */
static int qos_scull_setup_cdev(struct qos_scull_dev *dev, int i)
{
	int result, devno = MKDEV(qos_scull_major, qos_scull_minor + i);

	cdev_init(&dev->cdev, &qos_scull_fops);
	dev->cdev.owner = THIS_MODULE;
	dev->cdev.ops = &qos_scull_fops;
	result = cdev_add(&dev->cdev, devno, 1);
	if (result < 0)
		pr_alert(QOS_SCULL_ALERT
				"failed to add device %d to the kernel\n", i);
	else
		dev->added = 1;
	return (result);
}

static int __init qos_scull_init(void)
{
	int result, i;
	struct proc_dir_entry *entry;

	pr_info(QOS_SCULL_NOTICE "initialization\n");
	/* Registration of major/minor numbers */
	if (qos_scull_major) { // Static allocation
		major_devno = MKDEV(qos_scull_major, qos_scull_minor);
		result = register_chrdev_region(major_devno,
				qos_scull_no_devs,
				"qos_scull");
	} else { // Dynamic allocation
		result = alloc_chrdev_region(&major_devno,
				qos_scull_minor,
				qos_scull_no_devs,
				"qos_scull");
		qos_scull_major = MAJOR(major_devno);
	}
	if (result < 0) {
		pr_alert(QOS_SCULL_ALERT "major number %d registration failed\n",
				qos_scull_major);
		goto fail_major;
	}
	pr_info(QOS_SCULL_NOTICE "major number %d registered\n", qos_scull_major);

	/* Allocation of array of struct qos_scull_dev */
	devs = kmalloc(sizeof(struct qos_scull_dev) * qos_scull_no_devs,
			GFP_KERNEL);
	if (!devs) {
		pr_alert(QOS_SCULL_ALERT "memory exhausted\n");
		result = -ENOMEM;
		goto fail_alloc;
	}
	memset(devs, 0, qos_scull_no_devs * sizeof(struct qos_scull_dev));

	/* Initialize each struct qos_scull_dev */
	for (i = 0; i < qos_scull_no_devs; i++) {
		devs[i].quantum = qos_scull_quantum;
		devs[i].qset = qos_scull_qset;
		sema_init(&devs[i].sem, 1);
		result = qos_scull_setup_cdev(devs + i, i);
		if (result < 0) {
			pr_alert(QOS_SCULL_ALERT "failed to add device %d\n", i);
			goto fail_add;
		}
	}
	entry = proc_create_data("scullseq", 0, NULL, &qos_scull_proc_ops, NULL);
	if (entry) {
		pr_notice(QOS_SCULL_NOTICE "entry created successfully\n");
	} else {
		pr_alert(QOS_SCULL_ALERT "failed to create entry in /proc\n");
	}
	return 0;

fail_add: /* Only delete devices from kernel that were successfully added */
	for (i = 0; i < qos_scull_no_devs; i++) {
		if (devs[i].added)
			cdev_del(&devs[i].cdev);
	}
	kfree(devs);
fail_alloc:
	unregister_chrdev_region(major_devno, qos_scull_no_devs);
fail_major:
	pr_alert(QOS_SCULL_ALERT "critical error occured, unloading module\n");
	return result;
}

static void __exit qos_scull_exit(void)
{
	int i;

	pr_info(QOS_SCULL_NOTICE "unloading module\n");
	for (i = 0; i < qos_scull_no_devs; i++) {
		qos_scull_trim(devs + i);
		if (devs[i].added)
			cdev_del(&devs[i].cdev);
	}
	kfree(devs);
	unregister_chrdev_region(major_devno, qos_scull_no_devs);
}

module_init(qos_scull_init);
module_exit(qos_scull_exit);
