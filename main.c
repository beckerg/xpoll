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

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <limits.h>
#include <sysexits.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "xpoll.h"

#ifndef INFTIM
#define INFTIM (-1)
#endif

struct conn {
    int fd[2];
};

volatile sig_atomic_t sigalrm;

void
sigalrm_isr(int sig)
{
    if (sig == SIGALRM)
        sigalrm = 1;
}

char rwbuf[PIPE_BUF];

/* The following is a simple test program to demonstrate the power and
 * efficiency of epoll(7)/kqueue(2) vs poll(2).
 *
 * First, it creates an array of n pipes and makes all pipe fds (both
 * read and write ends) known to xpoll (i.e., it sets POLLIN on each
 * read-end and disables all write-ends).  We start things off by
 * writing to the write-end of the first pipe.
 *
 * Next, we enter a timed loop which calls xpoll() until the timer fires.
 * When xpoll() returns, the "ready for read" pipe is read and then the
 * next pipe in the array is enabled for POLLOUT (circling back to the
 * first pipe once the end of the array is reached).  When xpoll returns
 * that the "ready for write" pipe is ready, we disable POLLOUT on that
 * fd and then write to it.  On the next iteration xpoll() will return
 * that it is ready to read, and we'll repeat the entire process over
 * with the next pipe in the array.
 *
 * Repeating the test with successively larger number of pipes (n)
 * shows that poll(2) performs worse as n goes up, while
 * epoll(7)/kqueue(2) produce consistent results regardless of n
 * (until n becomes very large).
 */
int
main(int argc, char **argv)
{
    struct timeval tv_start, tv_stop, tv_diff;
    struct xpoll *xpoll;
    struct conn *connv;
    ssize_t cc, rwmax;
    u_long rd_total;
    u_long iter;
    int connc;
    int rc;
    int i;

    connc = 8;
    rwmax = 1;

    if (argc > 1) {
        if (argv[1][0] == '-') {
            printf("usage: %s [connmax [connlimit [rwmax]]]\n", argv[0]);
            exit(0);
        }

        connc = strtol(argv[1], NULL, 0);
        if (connc < 1)
            connc = 1;
    }

    if (rwmax < 1)
        rwmax = 1;
    else if ((size_t)rwmax > sizeof(rwbuf))
        rwmax = sizeof(rwbuf);

    if (connc == 1) {
#if XPOLL_KQUEUE
        const char *pollname = "kevent";
#elif XPOLL_EPOLL
        const char *pollname = "epoll";
#else
        const char *pollname = "poll";
#endif

        printf("%6s %6s %9s %8s %12s\n",
               "NFD", "SEC", pollname, "READ", "XPOLL/SEC");
    }

    connv = malloc(sizeof(*connv) * connc);
    if (!connv)
        exit(1);

    xpoll = xpoll_create(connc * 2);
    if (!xpoll) {
        fprintf(stderr, "xpoll_create: %s\n", strerror(errno));
        exit(1);
    }

    for (i = 0; i < connc; ++i) {
        struct conn *conn = connv + i;

        rc = pipe(conn->fd);
        if (rc) {
            fprintf(stderr, "pipe: %s\n", strerror(errno));
            connc = i;
            break;
        }

        rc = xpoll_ctl(xpoll, XPOLL_ADD, POLLIN, conn->fd[0], conn);
        if (rc) {
            fprintf(stderr, "xpoll_ctl(%p, ADD, POLLIN, %d): %s\n",
                    xpoll, conn->fd[0], strerror(errno));
            exit(EX_OSERR);
        }

        rc = xpoll_ctl(xpoll, XPOLL_ADD, POLLOUT, conn->fd[1], conn);
        if (rc) {
            fprintf(stderr, "xpoll_ctl(%p, ADD, POLLOUT, %d): %s\n",
                    xpoll, conn->fd[0], strerror(errno));
            exit(EX_OSERR);
        }

        rc = xpoll_ctl(xpoll, XPOLL_DISABLE, POLLOUT, conn->fd[1], conn);
    }

    cc = write(connv->fd[1], rwbuf, rwmax);
    if (cc != rwmax) {
        fprintf(stderr, "write: %s\n", strerror(errno));
        exit(1);
    }

    rd_total = 0;
    iter = 0;

    signal(SIGALRM, sigalrm_isr);
    alarm(10);

    gettimeofday(&tv_start, NULL);

    while (!sigalrm) {
        int n;

        n = xpoll_wait(xpoll, INFTIM);

        if (n < 1) {
            fprintf(stderr, "xpoll: %s\n",
                    (-1 == n) ? strerror(errno) : "timeout");
            sleep(1);
            continue;
        }

        while (1) {
            struct conn *conn;
            ssize_t rcc, wcc;
            int revents;

            revents = xpoll_revents(xpoll, (void **)&conn);
            if (!revents)
                break;

            if (revents & (POLLERR | POLLHUP)) {
                fprintf(stderr, "pollerr | pollhup: conn=%p\n", conn);
                goto errout;
            }

            if (revents & POLLIN) {
                struct conn *rconn = conn;
                struct conn *wconn;

                rcc = read(rconn->fd[0], rwbuf, rwmax);
                if (rcc < 1) {
                    fprintf(stderr, "read(%d, %p, %ld): %s\n",
                            rconn->fd[0], rwbuf, rwmax,
                            (-1 == rcc) ? strerror(errno) : "EOF");
                    goto errout;
                }

                wconn = rconn + 1;
                if (wconn >= connv + connc)
                    wconn = connv;

                rc = xpoll_ctl(xpoll, XPOLL_ENABLE, POLLOUT, wconn->fd[1], wconn);
                if (rc) {
                    fprintf(stderr, "xpoll_ctl: enable pollout wconn=%p\n", wconn);
                    goto errout;
                }

                ++rd_total;
            }

            if (revents & POLLOUT) {
                struct conn *wconn = conn;

                rc = xpoll_ctl(xpoll, XPOLL_DISABLE, POLLOUT, wconn->fd[1], wconn);
                if (rc) {
                    fprintf(stderr, "xpoll_ctl: disable pollout wconn=%p\n", wconn);
                    goto errout;
                }

                wcc = write(wconn->fd[1], rwbuf, rwmax);
                if (wcc != rwmax) {
                    fprintf(stderr, "%ld = write(%d, %p, %ld): %s\n",
                            wcc, wconn->fd[1], rwbuf, rwmax,
                            (-1 == wcc) ? strerror(errno) : "short write");
                    goto errout;
                }
            }
        }

        ++iter;
    }

  errout:
    gettimeofday(&tv_stop, NULL);
    timersub(&tv_stop, &tv_start, &tv_diff);

    printf("%6d %6.3lf %9ld %8lu %12.2lf\n",
           connc,
           (tv_diff.tv_sec * 1000000.0 + tv_diff.tv_usec) / 1000000,
           iter, rd_total,
           (iter * 1000000.0) / (tv_diff.tv_sec * 1000000 + tv_diff.tv_usec));

    printf("%12d connections\n", connc);
    printf("%12.3lf total run time\n",
           (tv_diff.tv_sec * 1000000.0 + tv_diff.tv_usec) / 1000000);
    printf("%12ld total iterations\n", iter);
    printf("%12lu total read operations\n", rd_total);
    printf("%12.2lf reads/sec\n",
           (iter * 1000000.0) / (tv_diff.tv_sec * 1000000 + tv_diff.tv_usec));

    xpoll_destroy(xpoll);

    return 0;
}
