/*!
* This file is PART of lbhp project
* @author hongjun.liao <docici@126.com>, @date 2021/3/31
*
* utility for getopt
* */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifdef LIBHP_WITH_OPTPARSE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define OPTPARSE_IMPLEMENTATION
#define OPTPARSE_API static
#include "optparse/optparse.h"		/* option */
#define	no_argument			OPTPARSE_NONE	
#define required_argument	OPTPARSE_REQUIRED	
#define optional_argument	OPTPARSE_OPTIONAL

#include "hp_opt.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////////////////////////////////////

int hp_opt_argv(char * cmdline, char * argv[])
{
	if(!(cmdline && argv))
		return -1;

	int argc = 0;
	char * p = cmdline, *q = p;
	for (;;) {
		if (*q == ' ' || *q == '\0') {
			if(q - p > 0)
				argv[argc++] = p;
			if (*q == '\0')
				break;
			*q = '\0';
			p = q + 1;
		}
		++q;
	}
	argv[argc] = 0;

	return argc;
}

#ifdef __cplusplus
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "hp_libc.h"

int test_optparse_main(int argc, char ** argv)
{
	struct optparse_long longopts[] = {
		{"amend", 'a', OPTPARSE_NONE},
		{"brief", 'b', OPTPARSE_NONE},
		{"color", 'c', OPTPARSE_REQUIRED},
		{"delay", 'd', OPTPARSE_OPTIONAL},
		{0}
	};

	bool amend = false;
	bool brief = false;
	const char *color = "white";
	int delay = 0;
	HP_UNUSED(amend);
	HP_UNUSED(brief);
	HP_UNUSED(color);
	HP_UNUSED(delay);

	char *arg;
	int option;
	struct optparse options;

	optparse_init(&options, argv);
	while ((option = optparse_long(&options, longopts, NULL)) != -1) {
		switch (option) {
		case 'a':
			amend = true;
			break;
		case 'b':
			brief = true;
			break;
		case 'c':
			color = options.optarg;
			break;
		case 'd':
			delay = options.optarg ? atoi(options.optarg) : 1;
			break;
		case '?':
			fprintf(stderr, "%s: %s %s\n", __FUNCTION__, argv[0], options.errmsg);
		}
	}

	/* Print remaining arguments. */
	while ((arg = optparse_arg(&options)))
		printf("%s: %s\n", __FUNCTION__, arg);

	return 0;
}

int test_hp_opt_main(int argc, char ** argv)
{
	int i, rc;

	rc = test_optparse_main(argc, argv);
	//atoi(0);//
	//assert(atoi(0) == 0);
	assert(atoi("") == 0);

	/* hp_opt_argv */
	{
		assert(hp_opt_argv(0, 0) < 0);
		//check blanks
		char cmdline[512] = " --url  index -F  -i 5 ";
		char * argv_[128] = { 0 };
		int argc_ = hp_opt_argv(cmdline, argv_);
		assert(argc_ == 5);
	}
	{
		char cmdline[512] = "--url index -F  -i 5";
		char * argv_[128] = { 0 };
		int argc_ = hp_opt_argv(cmdline, argv_);
		assert(argc_ == 5);

		assert(argv_[0] && strcmp(argv_[0], "--url") == 0);
		assert(argv_[3] && strcmp(argv_[3], "-i") == 0);
		assert(argv_[4] && strcmp(argv_[4], "5") == 0);
	}
	return rc;
}
#endif //NDEBUG
#endif //LIBHP_WITH_OPTPARSE
