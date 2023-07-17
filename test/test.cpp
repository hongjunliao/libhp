 /*!
 * This file is PART of ilbhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/8
 *
 * */

/////////////////////////////////////////////////////////////////////////////////////////////

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include "hp/libhp.h"

/////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
#ifndef NDEBUG
	return libhp_all_tests_main(argc, argv);
#endif
}

/////////////////////////////////////////////////////////////////////////////////////////
