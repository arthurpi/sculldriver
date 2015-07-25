#ifndef QOS_SCULL_H_
#define QOS_SCULL_H_

#include <linux/cdev.h>
#include <linux/semaphore.h>

#define QOS_SCULL_MAJOR 0
#define QOS_SCULL_NO 4
#define QOS_SCULL_QUANTUM 4000
#define QOS_SCULL_QSET 1000

#define QOS_SCULL_NOTICE "qOS scull driver: "
#define QOS_SCULL_ALERT QOS_SCULL_NOTICE "error: "

struct qos_scull_qset {
  void **data;
  struct qos_scull_qset *next;
};

struct qos_scull_dev {
  int quantum; /* size of a quantum */
  int qset; /* number of quantum per set */
  unsigned long size; /* current total size */
  int added; /* added to kernel (1) or not (0) */
  struct qos_scull_qset *data;
  struct cdev cdev;
  struct semaphore sem;
};

extern int qos_scull_major;
extern int qos_scull_minor;
extern int qos_scull_no_devs;
extern int qos_scull_quantum;
extern int qos_scull_qset;

// IOCTL

#define QOS_SCULL_IOC_MAGIC 0x81
#define QOS_SCULL_IOCRESET _IO(QOS_SCULL_IOC_MAGIC, 0)

// _IO*(type, nr, datatype)
// type == ioc_magic number
// nr == ioctl command number
// datatype == type of the data passed
#define QOS_SCULL_IOCSQUANTUM _IOW(QOS_SCULL_IOC_MAGIC, 1, int)

#define QOS_SCULL_IOC_MAXNR 1

// int qos_scull_open(struct inode *, struct file *);
// int qos_scull_release(struct inode *, struct file *);
// loff_t qos_scull_llseek(struct file *filp, loff_t offset, int whence);
// ssize_t qos_scull_write(struct file *, char const __user *, size_t, loff_t *);
// ssize_t qos_scull_read(struct file *, char __user *, size_t, loff_t *);

#endif /* QOS_SCULL_H_ */
