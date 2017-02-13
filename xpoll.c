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

/*
 * xpoll(3) is a simple abstraction of poll(2) with underlying implementations
 * provided by poll(2), epoll(7), and kqueue(2).  It is loosely modeled on
 * Linux's epoll(7) as epoll has fewer capabilities than FreeBSD's kqueue.
 * While it's not a drop-in replacement for poll(2), it should prove fairly
 * easy to retrofit programs based on poll(2) to use xpoll(3).
 *
 * To use xpoll(3), one must first call xpoll_create(3) which returns a handle
 * to an xpoll(3) instance for use in subsequent xpoll(3) related calls.  This
 * handle should be passed to xpoll_destroy(3) when the caller is done with it.
 *
 * xpoll_ctl(3) associates poll events with a given file descriptor.  It is
 * analogous to epoll_ctl(2) and kevent(2) with respect to adding and enabling
 * events on a per file descriptor basis.  Currently, the event types are POLLIN
 * and POLLOUT, and the operations are XPOLL_ADD, XPOLL_DELETE, XPOLL_ENABLE,
 * and XPOLL_DISABLE.
 *
 * To poll for events one must call xpoll(3).  xpoll(2) has the same general
 * sematics as poll(2), but may vary depending upon which underlying
 * implementation is in use.  xpoll(3) manages the details of the underlying
 * implementation.  In order to retrieve all currently ready events,
 * xpoll_revents(3) should be called until it returns zero.
 *
 * Compile this file with the accompanying main.c to generate a simple test
 * program that illustrates how much more efficent epoll(7)/kqueue(2) are
 * than poll(2).
 *
 * Note that xpoll(3) was created for instructional purposes, and hence it's
 * not clear how or if it will useful it would be in production code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "xpoll.h"

#define FDMAX  (1024 * 256)


struct xpoll *
xpoll_create(int fdmax)
{
    struct xpoll *xpoll;

    if (fdmax < 1)
        return NULL;

    xpoll = calloc(1, sizeof(*xpoll));
    if (!xpoll)
        return NULL;

    xpoll->fdmax = fdmax + 128;

#if !defined(XPOLL_KQUEUE)
    xpoll->pollfdv = calloc(xpoll->fdmax, sizeof(*xpoll->pollfdv));
    if (!xpoll->pollfdv) {
        free(xpoll);
        return NULL;
    }

    for (int i = 0; i < xpoll->fdmax; ++i)
        xpoll->pollfdv[i].fd = -1;
#endif

#if defined(XPOLL_EPOLL) || defined(XPOLL_KQUEUE)
    xpoll->nfds = 8;
    xpoll->xpollevv = calloc(xpoll->nfds, sizeof(*xpoll->xpollevv));
    if (!xpoll->xpollevv) {
        xpoll_destroy(xpoll);
        return NULL;
    }

#if defined(XPOLL_EPOLL)
    xpoll->fd = epoll_create1(0);
#elif defined(XPOLL_KQUEUE)
    xpoll->fd = kqueue();
#endif

#else
    xpoll->datav = calloc(xpoll->fdmax, sizeof(*xpoll->datav));
    if (!xpoll->datav) {
        xpoll_destroy(xpoll);
        return NULL;
    }

    xpoll->xpollevv = xpoll->pollfdv;
    xpoll->fd = getdtablesize();
#endif

    if (xpoll->fd < 0) {
        xpoll_destroy(xpoll);
        return NULL;
    }

    return xpoll;
}

void
xpoll_destroy(struct xpoll *xpoll)
{
    if (xpoll) {
        close(xpoll->fd);

#if !defined(XPOLL_KQUEUE)
        free(xpoll->pollfdv);
#endif

#if defined(XPOLL_EPOLL) || defined(XPOLL_KQUEUE)
        free(xpoll->xpollevv);
#else
        free(xpoll->datav);
#endif
    }
}

int
xpoll_ctl(struct xpoll *xpoll, int op, int flags, int fd, void *data)
{
#if defined(XPOLL_EPOLL) || defined(XPOLL_KQUEUE)
    struct xpollev eventv[2], *event = eventv;
#endif

    if (fd < 0 || fd >= xpoll->fdmax)
        abort();

#if !defined(XPOLL_KQUEUE)
    struct pollfd *pollfd;

    pollfd = xpoll->pollfdv + fd;

    pollfd->fd = (op == XPOLL_DELETE) ? -1 : fd;

    flags &= POLLIN | POLLOUT;

    if (op == XPOLL_ADD || op == XPOLL_ENABLE)
        pollfd->events |= flags;
    else if (op == XPOLL_DELETE || op == XPOLL_DISABLE)
        pollfd->events &= ~flags;
#endif

#if defined(XPOLL_EPOLL)
    event->events = pollfd->events;
    event->data.ptr = data;

    return epoll_ctl(xpoll->fd, op & 0xff, fd, eventv);

#elif defined(XPOLL_KQUEUE)

    if (flags & POLLIN) {
        EV_SET(event, fd, EVFILT_READ, op, 0, 0, data);
        ++event;
    }

    if (flags & POLLOUT) {
        EV_SET(event, fd, EVFILT_WRITE, op, 0, 0, data);
        ++event;
    }

    return kevent(xpoll->fd, eventv, (event - eventv), NULL, 0, NULL);

#else

    if (fd >= xpoll->nfds)
        xpoll->nfds = fd + 1;

    xpoll->datav[fd] = data;

    return 0;
#endif
}

int
xpoll_wait(struct xpoll *xpoll, int timeout)
{
    xpoll->n = 0;

#if defined(XPOLL_EPOLL)
    xpoll->nrdy = epoll_wait(xpoll->fd, xpoll->xpollevv, xpoll->nfds, timeout);

#elif defined(XPOLL_KQUEUE)

    struct timespec *ts = NULL;
    struct timespec tsbuf;

    if (timeout >= 0) {
        tsbuf.tv_sec = timeout / 1000;
        tsbuf.tv_nsec = (timeout * 1000000) % 1000000;
        ts = &tsbuf;
    }

    xpoll->nrdy = kevent(xpoll->fd, NULL, 0, xpoll->xpollevv, xpoll->nfds, ts);

#else

    xpoll->nrdy = poll(xpoll->xpollevv, xpoll->nfds, timeout);
#endif

    return xpoll->nrdy;
}

int
xpoll_revents(struct xpoll *xpoll, void **datap)
{
    struct xpollev *xpollev;

    *datap = NULL;

    if (xpoll->nrdy < 1)
        return 0;

#if defined(XPOLL_EPOLL)
    xpollev = xpoll->xpollevv + xpoll->n;
    *datap = xpollev->data.ptr;

    --xpoll->nrdy;
    ++xpoll->n;

    return xpollev->events;

#elif defined(XPOLL_KQUEUE)

    int flags = 0;

    xpollev = xpoll->xpollevv + xpoll->n;

    if (xpollev->filter == EVFILT_READ)
        flags |= POLLIN;
    else if (xpollev->filter == EVFILT_WRITE)
        flags |= POLLOUT;

    if (xpollev->flags & EV_ERROR)
        flags |= POLLERR;

    *datap = xpollev->udata;

    --xpoll->nrdy;
    ++xpoll->n;

    return flags;

#else

    xpollev = xpoll->xpollevv;

    while (xpoll->n < xpoll->nfds) {
        if (xpollev->revents) {
            *datap = xpoll->datav[xpoll->n];
            --xpoll->nrdy;
            ++xpoll->n;
            return xpollev->revents;
        }

        ++xpoll->n;
        ++xpollev;
    }

    return 0;
#endif
}
