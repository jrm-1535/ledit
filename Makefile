
#
# Makefile for ledit library
#

DEFINE :=

# make LEDIT_DEBUG=1 to enable debugging with CTL-D
ifeq ($(LEDIT_DEBUG), 1)
DEFINE += -DLEDIT_DEBUG
endif

# you can also give MAX_LINE_SIZE=xxxx
ifdef MAX_LINE_SIZE
DEFINE += -DMAX_LINE_SIZE=$(MAX_LINE_SIZE)
endif

# and/or give MAX_HISTIRY_SIZE=yyy
ifdef MAX_HISTORY_SIZE
DEFINE += -DMAX_HISTORY_SIZE=$(MAX_HISTORY_SIZE)
endif

LIBS :=
DEBUG := -g
OPTIMIZE := #-O3
STD := -std=c11 -D_POSIX_C_SOURCE=200809L
CFLAGS := -Wall -pedantic $(STD) $(DEFINE) $(OPTIMIZE) $(PROFILE) $(DEBUG)
CC := gcc $(GDEFS)

ALL := ledit.a tst

all:   $(ALL)

clean:
	   rm *.o $(ALL)

ledit.a:  ledit.o
	   /usr/bin/ar csr $@ $^

ledit.o:    ledit.c ledit.h

tst.o:  tst.c ledit.h

tst:   tst.o ledit.a
	   $(CC) $(CFLAGS) -o $@ $^
