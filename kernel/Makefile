CONFIG_MODULE_SIG=n
obj-m += episode.o
episode-objs := bitmap.o namei.o inode.o file.o dir.o tool.o

ccflags-y += -D__KERNEL__

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

