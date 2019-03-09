
# This makefile builds xpoll based on poll(2) by default.
# Use 'gmake kqueue' to build xpoll with kqueue(2) on FreeBSD,
# and use 'gmake epoll' to build xpoll with epoll(7) on Linux.

PROG := xpoll

HDR := xpoll.h
SRC := xpoll.c main.c

PLATFORM    := $(shell uname -s | tr 'a-z' 'A-Z')
#INCLUDE     := -I. -I../lib -I../../src/include
INCLUDE     := -I.
DEBUG       := -DDEBUG -O0
CPPFLAGS    := -D${PLATFORM}
CFLAGS      += -Wall -O2 -g3 ${INCLUDE}

OBJ     := ${SRC:.c=.o}

all: ${PROG}

clean:
	rm -f ${PROG} ${OBJ} *.core

cleandir: clean

debug: CFLAGS += ${DEBUG}
debug: ${PROG}

epoll_debug: CFLAGS += ${DEBUG}
epoll_debug: epoll

epoll: CFLAGS += -DXPOLL_EPOLL
epoll: ${PROG}

kqueue_asan: CFLAGS += ${DEBUG}
kqueue_asan: CFLAGS += -fsanitize=address -fsanitize=undefined
kqueue_asan: LDLIBS += -fsanitize=address -fsanitize=undefined
kqueue_asan: kqueue

kqueue_debug: CFLAGS += ${DEBUG}
kqueue_debug: kqueue

kqueue: CFLAGS += -DXPOLL_KQUEUE
kqueue: ${PROG}

${PROG}: ${OBJ}
