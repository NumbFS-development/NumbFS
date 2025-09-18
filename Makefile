#
# Makefile for NumbFS
#
obj-m += numbfs.o

numbfs-objs := super.o inode.o utils.o dir.o data.o xattr.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD)

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f *.o *.mod.* .*.cmd
