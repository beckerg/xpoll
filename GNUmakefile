
PROG := xpoll

HDR := xpoll.h
SRC := xpoll.c main.c

PLATFORM    := $(shell uname -s | tr 'a-z' 'A-Z')
INCLUDE     := -I. -I../lib -I../../src/include
DEBUG       := -g -O0 -DDEBUG
CPPFLAGS    := -D${PLATFORM}
CFLAGS      += -Wall -g -O2 ${INCLUDE}

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

kqueue_debug: CFLAGS += ${DEBUG}
kqueue_debug: kqueue

kqueue: CFLAGS += -DXPOLL_KQUEUE
kqueue: ${PROG}

${PROG}: ${OBJ}
