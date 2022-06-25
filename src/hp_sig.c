/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/9/13
 *
 * signal and handler
 * NOTE: the main code is from redis, redis: https://github.com/antirez/redis
 * copyright below:
 *
 * Copyright (c) 2009-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#if !defined(_WIN32) && !defined(_MSC_VER)


#include "hp_sig.h"   /* hp_sig */
#include <unistd.h>   /* getpid */
#include <signal.h>   /* sigaction, ... */
#include <stdlib.h>   /* exit */
#include <limits.h>    /* exit */
#include <stdint.h>   /* exit */
#include <string.h>   /* */
#include <time.h>     /* */

/*  */
static hp_sig * ghp_sig = 0;

/* Return the number of digits of 'v' when converted to string in radix 10.
 * See ll2string() for more information. */
uint32_t digits10(uint64_t v) {
    if (v < 10) return 1;
    if (v < 100) return 2;
    if (v < 1000) return 3;
    if (v < 1000000000000UL) {
        if (v < 100000000UL) {
            if (v < 1000000) {
                if (v < 10000) return 4;
                return 5 + (v >= 100000);
            }
            return 7 + (v >= 10000000UL);
        }
        if (v < 10000000000UL) {
            return 9 + (v >= 1000000000UL);
        }
        return 11 + (v >= 100000000000UL);
    }
    return 12 + digits10(v / 1000000000000UL);
}

/* Convert a long long into a string. Returns the number of
 * characters needed to represent the number.
 * If the buffer is not big enough to store the string, 0 is returned.
 *
 * Based on the following article (that apparently does not provide a
 * novel approach but only publicizes an already used technique):
 *
 * https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
 *
 * Modified in order to handle signed integers since the original code was
 * designed for unsigned integers. */
int ll2string(char *dst, size_t dstlen, long long svalue) {
    static const char digits[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
    int negative;
    unsigned long long value;

    /* The main loop works with 64bit unsigned integers for simplicity, so
     * we convert the number here and remember if it is negative. */
    if (svalue < 0) {
        if (svalue != LLONG_MIN) {
            value = -svalue;
        } else {
            value = ((unsigned long long) LLONG_MAX)+1;
        }
        negative = 1;
    } else {
        value = svalue;
        negative = 0;
    }

    /* Check length. */
    uint32_t const length = digits10(value)+negative;
    if (length >= dstlen) return 0;

    /* Null term. */
    uint32_t next = length;
    dst[next] = '\0';
    next--;
    while (value >= 100) {
        int const i = (value % 100) * 2;
        value /= 100;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
        next -= 2;
    }

    /* Handle last 1-2 digits. */
    if (value < 10) {
        dst[next] = '0' + (uint32_t) value;
    } else {
        int i = (uint32_t) value * 2;
        dst[next] = digits[i + 1];
        dst[next - 1] = digits[i];
    }

    /* Add sign. */
    if (negative) dst[0] = '-';
    return length;
}

/* Log a fixed message without printf-alike capabilities, in a way that is
 * safe to call from a signal handler.
 *
 * We actually use this only for signals that are not fatal from the point
 * of view of Redis. Signals that are going to kill the server anyway and
 * where we need printf-alike features are served by serverLog(). */
void serverLogFromHandler(const char *msg) {
    int fd;
    int log_to_stdout = 1;
    char buf[64];

    fd = STDERR_FILENO;

    if (fd == -1) return;

    if (write(fd,"[",1) == -1) goto err;
    ll2string(buf,sizeof(buf),time(NULL));
    if (write(fd,buf,strlen(buf)) == -1) goto err;
    if (write(fd,"]/",2) == -1) goto err;

    ll2string(buf,sizeof(buf),getpid());
    if (write(fd,buf,strlen(buf)) == -1) goto err;

    if (write(fd," serverLogFromHandler: ", 23) == -1) goto err;

    if (write(fd,msg,strlen(msg)) == -1) goto err;

    if (write(fd,"\n",1) == -1) goto err;
err:
    if (!log_to_stdout) close(fd);
}

/////////////////////////////////////////////////////////////////////////////////////////

static void sigShutdownHandler(int sig) {
    char const * msg;
    int is_chld = 0, sigusr = 0;

    switch (sig) {
    case SIGINT:
        msg = "Received SIGINT scheduling shutdown...";
        break;
    case SIGTERM:
        msg = "Received SIGTERM scheduling shutdown...";
        break;
    case SIGCHLD:
    	msg = "Received SIGCHLD, call wait";
    	is_chld = 1;
    	break;
    case SIGUSR1:
    	msg = "Received SIGUSR1";
    	sigusr = 1;
    	break;
    case SIGUSR2:
    	msg = "Received SIGUSR2";
    	sigusr = 2;
    	break;
    default:
        msg = "Received shutdown signal, scheduling shutdown...";
    };

    /* SIGINT is often delivered via Ctrl+C in an interactive session.
     * If we receive the signal the second time, we interpret this as
     * the user really wanting to quit ASAP without waiting to persist
     * on disk. */
    serverLogFromHandler(msg);

	if(sigusr == 1){
		if(ghp_sig->on_usr1)
			ghp_sig->on_usr1(ghp_sig->arg);
		return;
	}
	else if(sigusr == 2){
		if(ghp_sig->on_usr2)
			ghp_sig->on_usr2(ghp_sig->arg);
		return;
	}

	if(is_chld && ghp_sig->on_chld)
		ghp_sig->on_chld(ghp_sig->arg);

	if(!is_chld){
		if(ghp_sig->on_exit)
			ghp_sig->on_exit(ghp_sig->arg);
		else exit(0);
	}
}

void setupSignalHandlers(void) {
    struct sigaction act;

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
     * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigShutdownHandler;
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGCHLD, &act, NULL);
    sigaction(SIGUSR1, &act, NULL);
    sigaction(SIGUSR2, &act, NULL);

#ifdef HAVE_BACKTRACE
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = sigsegvHandler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
#endif
    return;
}

int hp_sig_init(hp_sig * sig
		, void (*on_chld)(void * arg)
		, void (*on_exit)(void * arg)
		, void (*on_usr1)(void * arg)
		, void (*on_usr2)(void * arg)
		, void * arg)
{
	if(!sig) return -1;

	sig->init = hp_sig_init;
	sig->uninit = hp_sig_uninit;

	/* see redis/server.c/initServer */
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
	setupSignalHandlers();


	sig->on_chld = on_chld;
	sig->on_exit = on_exit;
	sig->on_usr1 = on_usr1;
	sig->on_usr2 = on_usr2;
	sig->arg = arg;

	ghp_sig = sig;

    if(!sig->on_chld){
        signal(SIGCHLD, SIG_IGN);
    }

	return 0;
}

void hp_sig_uninit(hp_sig * sig)
{

}
#endif /* _MSC_VER */
