 /*!
 * This file is PART of libxhhp project
 * @author hongjun.liao <docici@126.com>, @date 2020/08/18
 *
 * --test=test_libim_dispatch_imfwd_main,test_libim_fwdcli_main 
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */


#include <dlfcn.h>			/* dlsym */
#include <stdio.h>
#include <string.h>
#include "hp/hp_test.h"    /* hp_test */
#include "hp/sdsinc.h"		/* sds */
#include "hp/libhp.h"

/////////////////////////////////////////////////////////////////////////////////////////
int hp_test(char const * test, int argc, char ** argv, int (* all)(int argc, char *argv[]), int flags)
{
	if(!(test))
		return -1;
		
	int i, rc;
	int count = 0;

#ifdef _MSC_VER
	HMODULE hmodule = GetModuleHandle(NULL);
#endif //_MSC_VER

	rc = 0;
	sds * tests = sdssplitlen(test, strlen(test), ",", 1, &count);

	int n = 0;
	for(i = 0; i < count; ++i){
		if(sdslen(tests[i]) == 0)
			continue;

		void * fn = dlsym(0/*RTLD_DEFAULT*/, tests[i]);

		if(fn){
			int (* test_fn)(int argc, char *argv[]) = fn;

			rc = test_fn(argc, argv);
            if(rc != 0){
                if(flags & 1)
                    return rc;
            }
			++n;
		}
		else printf("%s: skipped for %s\n", __FUNCTION__, tests[i]);
	}
	sdsfreesplitres(tests, count);

	return n;
}

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <assert.h>
#include <stdlib.h>

int test_only_functionname_fn2(int argc, char ** argv) { return -20; }
#ifdef _WIN32
__declspec(dllexport) int test_only_functionname_fn1(int argc, char ** argv){ return -10; }
__declspec(dllexport) int test_only_functionname_fn3(int argc, char ** argv) { return -30; }
#endif


int test_hp_test_main(int argc, char ** argv)
{
	int rc;
	rc = hp_test(0, 0, 0, 0, 0); assert(rc < 0);

#ifdef _WIN32
	rc = hp_test("this_function_not_exist_haha", 0, 0, 0, 0); assert(rc == 0);
	rc = hp_test("test_only_functionname_fn1", 0, 0, 0, 0); assert(rc == 1);
	rc = hp_test("test_only_functionname_fn1,test_only_functionname_fn2", 0, 0, 0, 0); assert(rc == 1);
	rc = hp_test("test_only_functionname_fn1,test_only_functionname_fn2"
			",test_only_functionname_fn3", 0, 0, 0, 0); assert(rc == 2);

	/* stop on first error */
	rc = hp_test("test_only_functionname_fn1,test_only_functionname_fn2"
		",test_only_functionname_fn3", 0, 0, 0, 1); assert(rc == -10);
#else
	rc = hp_test("this_function_not_exist_haha", 0, 0, 0, 0); assert(rc == 0);
	rc = hp_test("test_only_functionname_fn1,test_only_functionname_fn2", 0, 0, 0, 0); assert(rc == 1);
#endif
	return 0;
}
#endif //NDEBUG
