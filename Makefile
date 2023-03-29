#
#	Makefile for COMP 421 Yalnix kernel and user programs
#

ALL = yalnix idle init console shell

KERNEL_OBJS = kernel.o proc.o mmu.o interrupt.o syscall.o args.o queue.o list.o
KERNEL_SRCS = kernel.c proc.c mmu.c interrupt.c syscall.c args.c queue.c list.c

TEST_DIR = ./tests
PUBLIC_DIR = /clear/courses/comp421/pub

CPPFLAGS = -I$(PUBLIC_DIR)/include
CFLAGS = -g -Wall 

LANG = gcc

%: $(TEST_DIR)/%.o
	$(LINK.o) -o $@ $^ $(LOADLIBES) $(LDLIBS)

LINK.o = $(PUBLIC_DIR)/bin/link-user-$(LANG) $(LDFLAGS) $(TARGET_ARCH)

%: %.c
%: %.cc
%: %.cpp

all: $(ALL)

yalnix: $(KERNEL_OBJS)
	$(PUBLIC_DIR)/bin/link-kernel-$(LANG) -o yalnix $(KERNEL_OBJS)

idle: idle.o
	$(LINK.o) -o idle idle.o $(LOADLIBES) $(LDLIBS)

clean:
	rm -f $(KERNEL_OBJS) idle.o $(ALL)

depend:
	$(CC) $(CPPFLAGS) -M $(KERNEL_SRCS) > .depend

#include .depend