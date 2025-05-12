obj-m += ads1115_driver.o
KDIR = /lib/modules/$(shell uname -r)/build
all:
	make -C $(KDIR) M=$(shell PWD) modules

clean:
	make -C $(KDIR) M=$(shell PWD) clean
