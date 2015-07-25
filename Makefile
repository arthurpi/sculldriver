DEBUG = y

ifeq ($(DEBUG),y)
  DEBFLAGS = -O -g -DSCULL_DEBUG # "-O" is needed to expand inlines
else
  DEBFLAGS = -O2
endif
#CFLAGS += $(DEBFLAGS)
KERNELDIR = /home/qrthur/devel/c/kernel_dev/linux

obj-m += qos_scull.o
#obj-m += scull.o

all:
	make -C $(KERNELDIR) M=${shell pwd}

clean:
	rm -rf *.o *.order *.mod.c *.mod.o Module.symvers .*cmd .tmp_versions

fclean: clean
	rm -rf qos_scull.ko

re: fclean all
