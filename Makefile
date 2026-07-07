obj-m += vchar_device.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC ?= gcc
CFLAGS_USER := -Wall -Wextra -O2

.PHONY: all module test clean load unload demo status

all: module test

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

test: test_vchar.c
	$(CC) $(CFLAGS_USER) -o test_vchar test_vchar.c

load: module
	sudo insmod vchar_device.ko

unload:
	sudo rmmod vchar_device

demo: test
	sudo ./test_vchar

status:
	@if [ -r /sys/class/vchar_device/vchar_device/status ]; then \
		cat /sys/class/vchar_device/vchar_device/status; \
	else \
		echo "status node not found; load the module first"; \
	fi

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) test_vchar
