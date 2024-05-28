/*
 * Copyright (c) 2017 Greg Becker.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef XPOLL_H
#define XPOLL_H

#include <poll.h>

#if __FreeBSD__
#define XPOLL_KQUEUE    (!XPOLL_POLL)
#elif __linux__
#define XPOLL_EPOLL     (!XPOLL_POLL)
#endif

#if XPOLL_KQUEUE
#include <sys/event.h>
#include <sys/queue.h>

#define xpollev         kevent

#define XPOLL_ADD       EV_ADD
#define XPOLL_DELETE    EV_DELETE
#define XPOLL_ENABLE    EV_ENABLE
#define XPOLL_DISABLE   EV_DISABLE

#elif XPOLL_EPOLL

#include <sys/epoll.h>

#define xpollev         epoll_event

#define XPOLL_ADD       EPOLL_CTL_ADD
#define XPOLL_DELETE    EPOLL_CTL_DEL
#define XPOLL_ENABLE    EPOLL_CTL_MOD
#define XPOLL_DISABLE   (EPOLL_CTL_MOD | 0x1000)

#else

#define xpollev         pollfd

#define XPOLL_ADD       0x0001
#define XPOLL_DELETE    0x0002
#define XPOLL_ENABLE    0x0004
#define XPOLL_DISABLE   0x0008
#endif

struct xpoll {
#if XPOLL_KQUEUE
    struct xpollev changev[8];      // kevent(2) changelist parameter
    int changec;                    // kevent(2) nchanges parameter
#else
    struct pollfd *fds;             // poll(2) fds parameter
    void **datav;
#endif

    struct xpollev *eventv;
    int fdmax;
    int nfds;
    int nrdy;
    int n;

    int fd; // fd from epoll_create() or kqueue()
};

extern struct xpoll *xpoll_create(int fdmax);
extern void xpoll_destroy(struct xpoll *xpoll);
extern int xpoll_ctl(struct xpoll *xpoll, int op, int events, int fd, void *data);
extern int xpoll_wait(struct xpoll *xpoll, int timeout);
extern int xpoll_revents(struct xpoll *xpoll, void **datap);

#endif /* XPOLL_H */
