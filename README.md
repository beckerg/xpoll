# xpoll

**xpoll(3)** is a simple abstraction of **poll(2)** with underlying
implementations provided by **poll(2)**, **epoll(7)**, and **kqueue(2)**.
It is loosely modeled on Linux's **epoll(7)** as _epoll_ has fewer
capabilities than FreeBSD's kqueue.
While it's not a drop-in replacement for **poll(2)**, it should prove fairly
easy to retrofit **poll(2)** based programs to use **xpoll(3)**.

# Build and Test
_**main.c**_ is a simple test program to demonstrate the power and
efficiency of **epoll(7)/kqueue(2)** vs **poll(2)**.

First, it creates an array of n pipes and makes all pipe fds (both
read and write ends) known to _xpoll_ (i.e., it sets **POLLIN**
on each read-end and disables all write-ends).  We start things off
by writing to the write-end of the first pipe.

Next, we enter a timed loop which calls _xpoll()_ until the timer fires.
When _xpoll()_ returns, the "ready for read" pipe is read and then the
next pipe in the array is enabled for **POLLOUT** (circling back to the
first pipe once the end of the array is reached).  When _xpoll()_ returns
that the "ready for write" pipe is ready, we disable **POLLOUT** on that
fd and then write to it.  On the next iteration _xpoll()_ will return
that it is ready to read, and we'll repeat the entire process over
with the next pipe in the array.

Repeating the test with successively larger number of pipes (n)
shows that **poll(2)** performs much worse as n goes up, while
**epoll(7)/kqueue(2)** produce consistent results regardless of n
(until n becomes very large).

## FreeBSD
Build and run the **kqueue(2)** based test (FreeBSD 13.0-RELEASE-p2 amd64):

```
$ gmake
cc -Wall -O2 -g3 -I. -DNDEBUG  -c -o xpoll.o xpoll.c
cc -Wall -O2 -g3 -I. -DNDEBUG  -c -o main.o main.c
cc   xpoll.o main.o   -o xpoll

$ ./xpoll
     8 10.001  14739959  7369980   1473887.72
           8 connections
      10.001 total run time
    14739959 total iterations
     7369980 total read operations
  1473887.72 reads/sec

$ ./xpoll 1000
  1000 10.001  13928585  6964293   1392773.82
        1000 connections
      10.001 total run time
    13928585 total iterations
     6964293 total read operations
  1392773.82 reads/sec
```

Build and run the **poll(2)** based test:

```
$ gmake clean poll
cc -Wall -O2 -g3 -I. -DNDEBUG -DXPOLL_POLL=1  -c -o xpoll.o xpoll.c
cc -Wall -O2 -g3 -I. -DNDEBUG -DXPOLL_POLL=1  -c -o main.o main.c
cc   xpoll.o main.o   -o xpoll

$ ./xpoll
     8 10.005   3920108  1960054    391819.20
           8 connections
      10.005 total run time
     3920108 total iterations
     1960054 total read operations
   391819.20 reads/sec

$ ./xpoll 1000
  1000 10.001     20518    10259      2051.60
        1000 connections
      10.001 total run time
       20518 total iterations
       10259 total read operations
     2051.60 reads/sec
```

Comparing the _reads/sec_, we see that for 8 pipes **kqueue(2)**
is roughly 3.7 times faster than **poll(2)** in responding
events, while for 1000 pipes **kqueue(2)** is over _679_ times
faster than **poll(2)**.

## Linux
Build and run the **epoll(2)** based test (Fedora 34 5.11.12-300.fc34.x86_64
in a VirtualBox VM on the host used for the FreeBSD test):

```
$ gmake
cc -Wall -O2 -g3 -I. -DNDEBUG  -c -o xpoll.o xpoll.c
cc -Wall -O2 -g3 -I. -DNDEBUG  -c -o main.o main.c
cc   xpoll.o main.o   -o xpoll

$ ./xpoll
     8 10.001  10265379  5132690   1026485.04
           8 connections
      10.001 total run time
    10265379 total iterations
     5132690 total read operations
  1026485.04 reads/sec

$ ./xpoll 1000
  1000 10.001   8821735  4410868    882090.23
        1000 connections
      10.001 total run time
     8821735 total iterations
     4410868 total read operations
   882090.23 reads/sec
```

Build and run the **poll(2)** based test:

```
$ gmake clean poll
cc -Wall -O2 -g3 -I. -DNDEBUG -DXPOLL_POLL=1  -c -o xpoll.o xpoll.c
cc -Wall -O2 -g3 -I. -DNDEBUG -DXPOLL_POLL=1  -c -o main.o main.c
cc   xpoll.o main.o   -o xpoll

$ ./xpoll
     8 10.001   5773675  2886838    577317.68
           8 connections
      10.001 total run time
     5773675 total iterations
     2886838 total read operations
   577317.68 reads/sec

$ ./xpoll 1000
  1000 10.001     57978    28989      5797.41
        1000 connections
      10.001 total run time
       57978 total iterations
       28989 total read operations
     5797.41 reads/sec
```

Comparing the _reads/sec_, we see that for 8 pipes **epoll(2)**
is roughly 1.8 times faster than **poll(2)** in responding
events, while for 510 pipes **epoll(2)** is roughly 152 times
faster than **poll(2)**.  Note that I would expect much
better numbers when not run within a VM, so YMMV.  I will
try to gather some numbers from a real machine when I get
a chance, and maybe bump up the open fd limits as well...
