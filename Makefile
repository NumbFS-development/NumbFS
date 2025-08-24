#
# Makefile for NumbFS
#
obj-m += numbfs.o

numbfs-objs := super.o inode.o utils.o dir.o data.o
