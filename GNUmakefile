
# This makefile builds xpoll based on poll(2) by default.
# Use 'gmake kqueue' to build xpoll with kqueue(2) on FreeBSD,
# and use 'gmake epoll' to build xpoll with epoll(7) on Linux.

PROG := xpoll

HDR := xpoll.h
SRC := xpoll.c main.c
OBJ := ${SRC:.c=.o}

INCLUDE  := -I.
CFLAGS   += -Wall -Wextra -O2 -g3 ${INCLUDE}
CPPFLAGS += -DNDEBUG

.DELETE_ON_ERROR:
.NOT_PARALLEL:

.PHONY: all asan clean clobber debug distclean maintainer-clean


all: ${PROG}

clean:
	rm -f ${PROG} ${OBJ} *.core
	rm -f $(patsubst %.c,.%.d*,${SRC})

cleandir distclean maintainer-clean: clean

debug: CPPFLAGS += -UNDEBUG
debug: CFLAGS += -O0 -fno-omit-frame-pointer
debug: ${PROG}

asan: CPPFLAGS += -UNDEBUG
asan: CFLAGS += -O0 -fno-omit-frame-pointer
asan: CFLAGS += -fsanitize=address -fsanitize=undefined
asan: LDLIBS += -fsanitize=address -fsanitize=undefined
asan: ${PROG}

poll: CPPFLAGS += -DXPOLL_POLL=1
poll: ${PROG}

${PROG}: ${OBJ}

.%.d: %.c
	@set -e; rm -f $@; \
	$(CC) -M $(CPPFLAGS) ${INCLUDE} $< > $@.$$$$; \
	sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	rm -f $@.$$$$

-include $(patsubst %.c,.%.d,${SRC})
