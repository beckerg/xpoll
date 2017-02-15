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

#ifndef NELEM
#define NELEM(_array)   (sizeof(_array) / sizeof((_array)[0]))
#endif

/*
 */
struct xpoll *
xpoll_create(int fdmax)
{
    struct xpoll *xpoll;

    errno = 0;

    if (fdmax < 1) {
        errno = EINVAL;
        return NULL;
    }

    xpoll = calloc(1, sizeof(*xpoll));
    if (!xpoll)
        return NULL;

    xpoll->fdmax = fdmax + 128;

#if !defined(XPOLL_KQUEUE)
    xpoll->fds = calloc(xpoll->fdmax, sizeof(*xpoll->fds));
    if (!xpoll->fds)
        goto errout;

    for (int i = 0; i < xpoll->fdmax; ++i)
        xpoll->fds[i].fd = -1;
#endif

#if defined(XPOLL_EPOLL) || defined(XPOLL_KQUEUE)
    xpoll->nfds = 128;
    xpoll->eventv = calloc(xpoll->nfds, sizeof(*xpoll->eventv));
    if (!xpoll->eventv)
        goto errout;

#else
    xpoll->datav = calloc(xpoll->fdmax, sizeof(*xpoll->datav));
    if (!xpoll->datav)
        goto errout;

    xpoll->eventv = xpoll->fds;
#endif

#if defined(XPOLL_EPOLL)
    xpoll->fd = epoll_create1(0);
#elif defined(XPOLL_KQUEUE)
    xpoll->fd = kqueue();
#else
    xpoll->fd = getdtablesize();
#endif

  errout:
    if (errno) {
        int xerrno = errno;

        xpoll_destroy(xpoll);
        errno = xerrno;
        xpoll = NULL;
    }

    return xpoll;
}

void
xpoll_destroy(struct xpoll *xpoll)
{
    if (xpoll) {
        close(xpoll->fd);

#if !defined(XPOLL_KQUEUE)
        free(xpoll->fds);
        free(xpoll->datav);
#endif

#if defined(XPOLL_EPOLL) || defined(XPOLL_KQUEUE)
        free(xpoll->eventv);
#endif
    }
}

/*
 */
int
xpoll_ctl(struct xpoll *xpoll, int op, int events, int fd, void *data)
{
    int rc = 0;

    if (fd < 0)
        abort();

#if !defined(XPOLL_KQUEUE)
    struct pollfd *fds;

    if (fd >= xpoll->fdmax) {
        errno = EINVAL;
        return -1;
    }

    fds = xpoll->fds + fd;

    fds->fd = (op == XPOLL_DELETE) ? -1 : fd;

    events &= POLLIN | POLLOUT;

    if (op == XPOLL_ADD || op == XPOLL_ENABLE)
        fds->events |= events;
    else if (op == XPOLL_DELETE || op == XPOLL_DISABLE)
        fds->events &= ~events;
#endif

#if defined(XPOLL_EPOLL)
    struct xpollev change;

    change.events = fds->events;
    change.data.ptr = data;

    rc = epoll_ctl(xpoll->fd, op & 0xff, fd, &change);

#elif defined(XPOLL_KQUEUE)
    struct xpollev *change = xpoll->changev + xpoll->changec;

    if (events & POLLIN) {
        EV_SET(change, fd, EVFILT_READ, op, 0, 0, data);
        ++change;
    }

    if (events & POLLOUT) {
        EV_SET(change, fd, EVFILT_WRITE, op, 0, 0, data);
        ++change;
    }

    xpoll->changec = change - xpoll->changev;

    if (xpoll->changec >= NELEM(xpoll->changev) - 1) {
        rc = kevent(xpoll->fd, xpoll->changev, xpoll->changec, NULL, 0, NULL);

        xpoll->changec = 0;
    }

#else
    if (fd >= xpoll->nfds)
        xpoll->nfds = fd + 1;

    xpoll->datav[fd] = data;
#endif

    return rc;
}

/*
 */
int
xpoll_wait(struct xpoll *xpoll, int timeout)
{
    xpoll->n = 0;

#if defined(XPOLL_EPOLL)
    xpoll->nrdy = epoll_wait(xpoll->fd, xpoll->eventv, xpoll->nfds, timeout);

#elif defined(XPOLL_KQUEUE)
    struct timespec tsbuf, *ts = NULL;

    if (timeout >= 0) {
        tsbuf.tv_sec = timeout / 1000;
        tsbuf.tv_nsec = (timeout * 1000000) % 1000000;
        ts = &tsbuf;
    }

    xpoll->nrdy = kevent(xpoll->fd, xpoll->changev, xpoll->changec,
                         xpoll->eventv, xpoll->nfds, ts);

    xpoll->changec = 0;

#else
    xpoll->nrdy = poll(xpoll->eventv, xpoll->nfds, timeout);
#endif

    return xpoll->nrdy;
}

/*
 */
int
xpoll_revents(struct xpoll *xpoll, void **datap)
{
    struct xpollev *event;

    *datap = NULL;

    if (xpoll->nrdy < 1)
        return 0;

#if defined(XPOLL_EPOLL)
    event = xpoll->eventv + xpoll->n;
    *datap = event->data.ptr;

    --xpoll->nrdy;
    ++xpoll->n;

    return event->events;

#elif defined(XPOLL_KQUEUE)
    int events = 0;

    event = xpoll->eventv + xpoll->n;

    if (event->filter == EVFILT_READ)
        events |= POLLIN;
    else if (event->filter == EVFILT_WRITE)
        events |= POLLOUT;

    if (event->flags & EV_ERROR)
        events |= POLLERR;

    *datap = event->udata;

    --xpoll->nrdy;
    ++xpoll->n;

    return events;

#else
    event = xpoll->eventv;

    while (xpoll->n < xpoll->nfds) {
        if (event->revents) {
            *datap = xpoll->datav[xpoll->n];
            --xpoll->nrdy;
            ++xpoll->n;
            return event->revents;
        }

        ++xpoll->n;
        ++event;
    }

    return 0; // Should never get here..
#endif
}
