/* //device/libs/telephony/ril_event.cpp
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0  
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "RILC" 

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <utils/Log.h>
#include <ril_event.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <pthread.h>
static pthread_mutex_t listMutex;
#define MUTEX_ACQUIRE() pthread_mutex_lock(&listMutex)
#define MUTEX_RELEASE() pthread_mutex_unlock(&listMutex)
#define MUTEX_INIT() pthread_mutex_init(&listMutex, NULL)
#define MUTEX_DESTROY() pthread_mutex_destroy(&listMutex)

#ifndef timeradd
#define timeradd(tvp, uvp, vvp)						\
	do {								\
		(vvp)->tv_sec = (tvp)->tv_sec + (uvp)->tv_sec;		\
		(vvp)->tv_usec = (tvp)->tv_usec + (uvp)->tv_usec;       \
		if ((vvp)->tv_usec >= 1000000) {			\
			(vvp)->tv_sec++;				\
			(vvp)->tv_usec -= 1000000;			\
		}							\
	} while (0)
#endif

#ifndef timercmp
//(&tev->timeout, &now, >)
//tev比now大返回真，比now小返回假
#define timercmp(a, b, op)               \
        ((a)->tv_sec == (b)->tv_sec      \   //判断秒
        ? (a)->tv_usec op (b)->tv_usec   \   //判断微秒
        : (a)->tv_sec op (b)->tv_sec)		//判断秒
#endif

#ifndef timersub
//(&tev->timeout, &now, tv)
#define timersub(a, b, res)                           \
    do {                                              \
        (res)->tv_sec = (a)->tv_sec - (b)->tv_sec;    \
        (res)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((res)->tv_usec < 0) {                     \
            (res)->tv_usec += 1000000;                \
            (res)->tv_sec -= 1;                       \
        }                                             \
    } while(0);
#endif

static fd_set readFds;
static int nfds = 0;

static struct ril_event * watch_table[MAX_FD_EVENTS];
static struct ril_event timer_list;
static struct ril_event pending_list;

#define DEBUG 0

#if DEBUG
#define dlog(x...) LOGD( x )
static void dump_event(struct ril_event * ev)
{
    dlog("~~~~ Event %x ~~~~", (unsigned int)ev);
    dlog("     next    = %x", (unsigned int)ev->next);
    dlog("     prev    = %x", (unsigned int)ev->prev);
    dlog("     fd      = %d", ev->fd);
    dlog("     pers    = %d", ev->persist);
    dlog("     timeout = %ds + %dus", (int)ev->timeout.tv_sec, (int)ev->timeout.tv_usec);
    dlog("     func    = %x", (unsigned int)ev->func);
    dlog("     param   = %x", (unsigned int)ev->param);
    dlog("~~~~~~~~~~~~~~~~~~");
}
#else
#define dlog(x...) do {} while(0)
#define dump_event(x) do {} while(0)
#endif

static void getNow(struct timeval * tv)
{
#ifdef HAVE_POSIX_CLOCKS
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec/1000;
#else
    gettimeofday(tv, NULL);
#endif
}

static void init_list(struct ril_event * list)
{
    memset(list, 0, sizeof(struct ril_event));
    list->next = list;
    list->prev = list;
    list->fd = -1;
}

static void addToList(struct ril_event * ev, struct ril_event * list)
{
    ev->next = list;
    ev->prev = list->prev;
    ev->prev->next = ev;
    list->prev = ev;
    dump_event(ev);
}

static void removeFromList(struct ril_event * ev)
{
    dlog("~~~~ Removing event ~~~~");
    dump_event(ev);

    ev->next->prev = ev->prev;
    ev->prev->next = ev->next;
    ev->next = NULL;
    ev->prev = NULL;
}


static void removeWatch(struct ril_event * ev, int index)
{
    watch_table[index] = NULL;
    ev->index = -1;

    FD_CLR(ev->fd, &readFds);

    if (ev->fd+1 == nfds) {
        int n = 0;

        for (int i = 0; i < MAX_FD_EVENTS; i++) {
            struct ril_event * rev = watch_table[i];

            if ((rev != NULL) && (rev->fd > n)) {
                n = rev->fd;
            }
        }
        nfds = n + 1;
        dlog("~~~~ nfds = %d ~~~~", nfds);
    }
}

static void processTimeouts()
{
    dlog("~~~~ +processTimeouts ~~~~");
    MUTEX_ACQUIRE();
    struct timeval now;
    struct ril_event * tev = timer_list.next;
    struct ril_event * next;

    getNow(&now);
    // walk list, see if now >= ev->timeout for any events

    dlog("~~~~ Looking for timers <= %ds + %dus ~~~~", (int)now.tv_sec, (int)now.tv_usec);
	/* 当前事件超时了，就把事件从timer_list移除，然后加入到pending_list */
	while ((tev != &timer_list) && (timercmp(&now, &tev->timeout, >))) {
        // Timer expired
        dlog("~~~~ firing timer ~~~~");
        next = tev->next;
        removeFromList(tev);
        addToList(tev, &pending_list);
        tev = next;
    }
    MUTEX_RELEASE();
    dlog("~~~~ -processTimeouts ~~~~");
}

static void processReadReadies(fd_set * rfds, int n)
{
    dlog("~~~~ +processReadReadies (%d) ~~~~", n);
    MUTEX_ACQUIRE();

    for (int i = 0; (i < MAX_FD_EVENTS) && (n > 0); i++) {
        struct ril_event * rev = watch_table[i];
		/* 把watch_table内的事件加入到pending_list */
        if (rev != NULL && FD_ISSET(rev->fd, rfds)) {
            addToList(rev, &pending_list);
			/* 然后根据rev->persist，判断是否保留watch_table内的事件 */
            if (rev->persist == false) {
                removeWatch(rev, i);
            }
            n--;
        }
    }

    MUTEX_RELEASE();
    dlog("~~~~ -processReadReadies (%d) ~~~~", n);
}

static void firePending()
{
    dlog("~~~~ +firePending ~~~~");
    struct ril_event * ev = pending_list.next;
    while (ev != &pending_list) {
        struct ril_event * next = ev->next;
        removeFromList(ev);
		/* 执行事件函数 */
        ev->func(ev->fd, 0, ev->param);
        ev = next;
    }
    dlog("~~~~ -firePending ~~~~");
}

static int calcNextTimeout(struct timeval * tv)
{
    struct ril_event * tev = timer_list.next;
    struct timeval now;

	//得到现在的时间
    getNow(&now);

    // Sorted list, so calc based on first node
    //如果下一个和当前的相等
    if (tev == &timer_list) {
        // no pending timers
        return -1;
    }

    dlog("~~~~ now = %ds + %dus ~~~~", (int)now.tv_sec, (int)now.tv_usec);
    dlog("~~~~ next = %ds + %dus ~~~~",
            (int)tev->timeout.tv_sec, (int)tev->timeout.tv_usec);
	//这个时间的具体计算过程?
	//tev比now大，返回真，比now小返回假
	/**
	 * 判断tev事件内的时间和当前时间
	 * 如果tev内的时间比当前时间多，那就是没有超时，就是真
	 */
    if (timercmp(&tev->timeout, &now, >)) {
		/* 用事件内的时间减去当前时间，也就是还过多长时间超时，把剩下的时间赋给tv */
        timersub(&tev->timeout, &now, tv);
    } else {
    	/* 如果tev事件内的时间小于当前时间，那就是超时了，把tv清零 */
        // timer already expired.
        tv->tv_sec = tv->tv_usec = 0;
    }
    return 0;
}

// Initialize internal data structs
void ril_event_init()
{
	//互斥初始化
    MUTEX_INIT();

	//清零
    FD_ZERO(&readFds);
	//初始化下面三个链表
    init_list(&timer_list);
    init_list(&pending_list);
    memset(watch_table, 0, sizeof(watch_table));
}

// Initialize an event
//(&s_wakeupfd_event, s_fdWakeupRead, true,processWakeupCallback, NULL)
void ril_event_set(struct ril_event * ev, int fd, bool persist, ril_event_cb func, void * param)
{
    dlog("~~~~ ril_event_set %x ~~~~", (unsigned int)ev);
    memset(ev, 0, sizeof(struct ril_event));
    ev->fd = fd;
    ev->index = -1;
    ev->persist = persist;
    ev->func = func;
    ev->param = param;
	//这里又设置了一遍
    fcntl(fd, F_SETFL, O_NONBLOCK);
}

// Add event to watch list
void ril_event_add(struct ril_event * ev)
{
    dlog("~~~~ +ril_event_add ~~~~");
    MUTEX_ACQUIRE();
    for (int i = 0; i < MAX_FD_EVENTS; i++) {
		/* 链表是空才会添加事件 */
        if (watch_table[i] == NULL) {
            watch_table[i] = ev;
            ev->index = i;
            dlog("~~~~ added at %d ~~~~", i);
            dump_event(ev);
			//增加一个新的文件描述符到readFds
            FD_SET(ev->fd, &readFds);
			/* nfds始终存放最大的一个描述符 */
            if (ev->fd >= nfds) nfds = ev->fd+1;
            dlog("~~~~ nfds = %d ~~~~", nfds);
            break;
        }
    }
    MUTEX_RELEASE();
    dlog("~~~~ -ril_event_add ~~~~");
}

// Add timer event
void ril_timer_add(struct ril_event * ev, struct timeval * tv)
{
    dlog("~~~~ +ril_timer_add ~~~~");
    MUTEX_ACQUIRE();

    struct ril_event * list;
    if (tv != NULL) {
        // add to timer list
        list = timer_list.next;
        ev->fd = -1; // make sure fd is invalid

        struct timeval now;
        getNow(&now);
        timeradd(&now, tv, &ev->timeout);

        // keep list sorted
        while (timercmp(&list->timeout, &ev->timeout, < )
                && (list != &timer_list)) {
            list = list->next;
        }
        // list now points to the first event older than ev
        addToList(ev, list);
    }

    MUTEX_RELEASE();
    dlog("~~~~ -ril_timer_add ~~~~");
}

// Remove event from watch or timer list
void ril_event_del(struct ril_event * ev)
{
    dlog("~~~~ +ril_event_del ~~~~");
    MUTEX_ACQUIRE();

    if (ev->index < 0 || ev->index >= MAX_FD_EVENTS) {
        MUTEX_RELEASE();
        return;
    }

    removeWatch(ev, ev->index);

    MUTEX_RELEASE();
    dlog("~~~~ -ril_event_del ~~~~");
}

#if DEBUG
static void printReadies(fd_set * rfds)
{
    for (int i = 0; (i < MAX_FD_EVENTS); i++) {
        struct ril_event * rev = watch_table[i];
			//判断事件rev->fd是否在rfds中
        if (rev != NULL && FD_ISSET(rev->fd, rfds)) {
          dlog("DON: fd=%d is ready", rev->fd);
        }
    }
}
#else
#define printReadies(rfds) do {} while(0)
#endif

/*
 * 监视所有注册的事件源，当某事件源有数据到来时，
 * 相应事件源的回调函数被触发
 */
void ril_event_loop()
{
    int n;
    fd_set rfds;
    struct timeval tv;
    struct timeval * ptv;


    for (;;) {

        // make local copy of read fd_set
        // 创建 readfds 的副本
        memcpy(&rfds, &readFds, sizeof(fd_set));
		//这个函数没有看懂
        if (-1 == calcNextTimeout(&tv)) {
            // no pending timers; block indefinitely
            dlog("~~~~ no timers; blocking indefinitely ~~~~");
			/* 事件超时，所以时间是0，所以不需要等待 */
            ptv = NULL;
        } else {
            dlog("~~~~ blocking for %ds + %dus ~~~~", (int)tv.tv_sec, (int)tv.tv_usec);
			/* 需要等待的时间 */
            ptv = &tv;
        }
		/* 判断事件描述符是否有效 */
        printReadies(&rfds);
		/* select返回的是: 状态发生变化的描述符总数 */
        n = select(nfds, &rfds, NULL, NULL, ptv);
		/* 判断事件描述符是否有效 */
        printReadies(&rfds);
        dlog("~~~~ %d events fired ~~~~", n);
        if (n < 0) {
            if (errno == EINTR) continue;

            ALOGE("ril_event: select error (%d)", errno);
            // bail?
            return;
        }

        // Check for timeouts
        processTimeouts();
        // Check for read-ready
        processReadReadies(&rfds, n);
        // Fire away
        firePending();
    }
}
