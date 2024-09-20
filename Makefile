KDIR := /lib/modules/$(shell uname -r)/build

obj-m += clear_accessed_bits.o
obj-m += read_accessed.o

all:
    make -C $(KDIR) M=$(PWD) modules

clean:
    make -C $(KDIR) M=$(PWD) clean