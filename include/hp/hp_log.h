/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2018/11/1
 *
 * log
 * */

/////////////////////////////////////////////////////////////////////////////////////

#ifndef LIBHP_LOG_H__
#define LIBHP_LOG_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */
#include <stdio.h>
#include "hp/sdsinc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * libhp global log level
 * */
extern int hp_log_level;
sds hp_log_hdr();
/*
 * you can simply replace
 *  hp_log(f, fmt, ...); to fprintf(f, fmt, ...);
 */
void hp_log(void * f, char const * fmt, ...);

#ifndef NDEBUG
int test_hp_log_main(int argc, char ** argv);
#endif /* NDEBUG */


#ifdef __cplusplus
}
#endif

/////////////////////////////////////////////////////////////////////////////////////
/**
 * C++ version of hp_log:
 * hp_log(std::cout, "%s '%s'", "hello", std::string("world"));
 */
#ifdef __cplusplus

#include <cstring> 	//strstr
#include <string>  	//std::string
#include <iostream>	//std::ostream

#define HP_LOG_CHR_IN "diouxXeEfFgGaAcsp"

static bool hp_log_is_chr_in(char c, char const * str)
{
	for(char const * p = str; *p; ++p){ if(*p == c) return true;  }
	return false;
}
static void hp_log(std::ostream & f, char const * fmt)
{
	for(char const * c; ;){
		c = strstr(fmt, "%%");
		if(c){
			f << std::string(fmt, c + 1 - fmt);
			fmt += (c - fmt + 2);
		}
		else {
			f << fmt;
			break;
		}
	}
}

template <typename T>
static void hp_log(std::ostream & f, char const * fmt, T t)
{
	for(char const * c; ;){
		c = strchr(fmt, '%');
		if(c){
			if(*(c + 1)){
				f << std::string(fmt, c - fmt);
				if(hp_log_is_chr_in(*(c + 1), HP_LOG_CHR_IN)){
					f << t;
					hp_log(f, c + 2);
					break;
				}
				else {
					f << '%';
					if(*(c + 1) != '%'){
						f << *(c + 1);
					}
					fmt += (c - fmt + 2);
				}
			}
			else {
				f << *c;
				break;
			}
		}
		else {
			f << fmt;
			break;
		}
	}
}

template <typename T, typename...Arg>
static void hp_log(std::ostream & f, char const * fmt, T t, Arg... args)
{
	for(char const * c; ;){
		c = strchr(fmt, '%');
		if(c){
			if(*(c + 1)){
				f << std::string(fmt, c - fmt);
				if(hp_log_is_chr_in(*(c + 1), HP_LOG_CHR_IN)){
					f << t;
					hp_log(f, c + 2, args...);
					break;
				}
				else{
					f << '%';
					if(*(c + 1) != '%'){
						f << *(c + 1);
					}
					fmt += (c - fmt + 2);
				}
			}
			else {
				f << *c;
				break;
			}
		}
		else {
			f << fmt;
			break;
		}
	}
}

/**
 * use HP_LOG instead of hp_log for c++
 * */
#define hp_log(f,fmt,args...) do {         \
	sds fmt_ = sdscat(sdsnew("%s"), (fmt));\
	sds hdr = hp_log_hdr();                \
	hp_log((f), fmt_, hdr, ##args);        \
	sdsfree(fmt_);                         \
	sdsfree(hdr);                          \
}while(0)
#endif //__cplusplus

/////////////////////////////////////////////////////////////////////////////////////

#endif /* LIBHP_LOG_H__ */
