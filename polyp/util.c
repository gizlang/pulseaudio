/* $Id$ */

/***
  This file is part of polypaudio.
 
  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.
 
  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
 
  You should have received a copy of the GNU General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>
#include <sched.h>
#include <sys/resource.h>

#include "util.h"
#include "xmalloc.h"
#include "log.h"

void pa_make_nonblock_fd(int fd) {
    int v;

    if ((v = fcntl(fd, F_GETFL)) >= 0)
        if (!(v & O_NONBLOCK))
            fcntl(fd, F_SETFL, v|O_NONBLOCK);
}

int pa_make_secure_dir(const char* dir) {
    struct stat st;

    if (mkdir(dir, 0700) < 0) 
        if (errno != EEXIST)
            return -1;
    
    if (lstat(dir, &st) < 0) 
        goto fail;
    
    if (!S_ISDIR(st.st_mode) || (st.st_uid != getuid()) || ((st.st_mode & 0777) != 0700))
        goto fail;
    
    return 0;
    
fail:
    rmdir(dir);
    return -1;
}

ssize_t pa_loop_read(int fd, void*data, size_t size) {
    ssize_t ret = 0;
    assert(fd >= 0 && data && size);

    while (size > 0) {
        ssize_t r;

        if ((r = read(fd, data, size)) < 0)
            return r;

        if (r == 0)
            break;
        
        ret += r;
        data = (uint8_t*) data + r;
        size -= r;
    }

    return ret;
}

ssize_t pa_loop_write(int fd, const void*data, size_t size) {
    ssize_t ret = 0;
    assert(fd >= 0 && data && size);

    while (size > 0) {
        ssize_t r;

        if ((r = write(fd, data, size)) < 0)
            return r;

        if (r == 0)
            break;
        
        ret += r;
        data = (uint8_t*) data + r;
        size -= r;
    }

    return ret;
}

void pa_check_for_sigpipe(void) {
    struct sigaction sa;
    sigset_t set;

#ifdef HAVE_PTHREAD    
    if (pthread_sigmask(SIG_SETMASK, NULL, &set) < 0) {
#endif
        if (sigprocmask(SIG_SETMASK, NULL, &set) < 0) {
            pa_log(__FILE__": sigprocmask() failed: %s\n", strerror(errno));
            return;
        }
#ifdef HAVE_PTHREAD
    }
#endif

    if (sigismember(&set, SIGPIPE))
        return;
    
    if (sigaction(SIGPIPE, NULL, &sa) < 0) {
        pa_log(__FILE__": sigaction() failed: %s\n", strerror(errno));
        return;
    }
        
    if (sa.sa_handler != SIG_DFL)
        return;
    
    pa_log(__FILE__": WARNING: SIGPIPE is not trapped. This might cause malfunction!\n");
}

/* The following is based on an example from the GNU libc documentation */
char *pa_sprintf_malloc(const char *format, ...) {
    int  size = 100;
    char *c = NULL;
    
    assert(format);
    
    for(;;) {
        int r;
        va_list ap;

        c = pa_xrealloc(c, size);

        va_start(ap, format);
        r = vsnprintf(c, size, format, ap);
        va_end(ap);
        
        if (r > -1 && r < size)
            return c;

        if (r > -1)    /* glibc 2.1 */
            size = r+1; 
        else           /* glibc 2.0 */
            size *= 2;
    }
}

char *pa_vsprintf_malloc(const char *format, va_list ap) {
    int  size = 100;
    char *c = NULL;
    
    assert(format);
    
    for(;;) {
        int r;
        va_list ap;

        c = pa_xrealloc(c, size);
        r = vsnprintf(c, size, format, ap);
        
        if (r > -1 && r < size)
            return c;

        if (r > -1)    /* glibc 2.1 */
            size = r+1; 
        else           /* glibc 2.0 */
            size *= 2;
    }
}


char *pa_get_user_name(char *s, size_t l) {
    struct passwd pw, *r;
    char buf[1024];
    char *p;

    if (!(p = getenv("USER")))
        if (!(p = getenv("LOGNAME")))
            if (!(p = getenv("USERNAME"))) {
                
                if (getpwuid_r(getuid(), &pw, buf, sizeof(buf), &r) != 0 || !r) {
                    snprintf(s, l, "%lu", (unsigned long) getuid());
                    return s;
                }
                
                p = r->pw_name;
            }
    
    snprintf(s, l, "%s", p);
    return s;
}

char *pa_get_host_name(char *s, size_t l) {
    gethostname(s, l);
    s[l-1] = 0;
    return s;
}

pa_usec_t pa_timeval_diff(const struct timeval *a, const struct timeval *b) {
    pa_usec_t r;
    assert(a && b);

    if (pa_timeval_cmp(a, b) < 0) {
        const struct timeval *c;
        c = a;
        a = b;
        b = c;
    }

    r = (a->tv_sec - b->tv_sec)* 1000000;

    if (a->tv_usec > b->tv_usec)
        r += (a->tv_usec - b->tv_usec);
    else if (a->tv_usec < b->tv_usec)
        r -= (b->tv_usec - a->tv_usec);

    return r;
}

int pa_timeval_cmp(const struct timeval *a, const struct timeval *b) {
    assert(a && b);

    if (a->tv_sec < b->tv_sec)
        return -1;

    if (a->tv_sec > b->tv_sec)
        return 1;

    if (a->tv_usec < b->tv_usec)
        return -1;

    if (a->tv_usec > b->tv_usec)
        return 1;

    return 0;
}

pa_usec_t pa_age(const struct timeval *tv) {
    struct timeval now;
    assert(tv);
    gettimeofday(&now, NULL);
    return pa_timeval_diff(&now, tv);
}

#define NICE_LEVEL (-15)

void pa_raise_priority(void) {
    if (setpriority(PRIO_PROCESS, 0, NICE_LEVEL) < 0)
        pa_log(__FILE__": setpriority() failed: %s\n", strerror(errno));
    else
        pa_log(__FILE__": Successfully gained nice level %i.\n", NICE_LEVEL);

#ifdef _POSIX_PRIORITY_SCHEDULING
    {
        struct sched_param sp;

        if (sched_getparam(0, &sp) < 0) {
            pa_log(__FILE__": sched_getparam() failed: %s\n", strerror(errno));
            return;
        }
        
        sp.sched_priority = 1;
        if (sched_setscheduler(0, SCHED_FIFO, &sp) < 0) {
            pa_log(__FILE__": sched_setscheduler() failed: %s\n", strerror(errno));
            return;
        }

        pa_log(__FILE__": Successfully enabled SCHED_FIFO scheduling.\n");
    }
#endif
}

void pa_reset_priority(void) {
#ifdef _POSIX_PRIORITY_SCHEDULING
    {
        struct sched_param sp;
        sched_getparam(0, &sp);
        sp.sched_priority = 0;
        sched_setscheduler(0, SCHED_OTHER, &sp);
    }
#endif

    setpriority(PRIO_PROCESS, 0, 0);
}

int pa_fd_set_cloexec(int fd, int b) {
    int v;
    assert(fd >= 0);

    if ((v = fcntl(fd, F_GETFD, 0)) < 0)
        return -1;
    
    v = (v & ~FD_CLOEXEC) | (b ? FD_CLOEXEC : 0);
    
    if (fcntl(fd, F_SETFD, v) < 0)
        return -1;
    
    return 0;
}
