KERNEL_DIR=linux-3.4.70

obj-m:copker.o
copker-objs: cache.o switch_stack.o

all:
	make -C /usr/src/$(KERNEL_DIR)/ M=$(PWD) modules
	
clean:
	make -C /usr/src/$(KERNEL_DIR)/ M=$(PWD) clean

