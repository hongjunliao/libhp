/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2019/6/9
 *
 * 2024/4/21 updated:
 * simple c++ wrapper for redis/dict
 * */

#ifndef LIBHP_DICT_H__
#define LIBHP_DICT_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#ifndef LIBHP_WITH_REDISDICT

#include "sdsinc.h"        /* sds */
extern "C" {
#include "redis/src/dict.h" /* dict */
}
#include <stdlib.h>

/////////////////////////////////////////////////////////////////////////////////////////
//sds=>int
class hp_dict_si {
	static dictType sidt;
	dict * dict_;
public:
	hp_dict_si();
	~hp_dict_si();
public:
	int& operator[](char const * k);
};

//sds=>sds
class hp_dict_ss {
	static dictType ssdt;
	dict * dict_;
};
/////////////////////////////////////////////////////////////////////////////////////////
#ifndef NDEBUG
int test_hp_dict_main(int argc, char ** argv);
#endif /* NDEBUG */


#endif /* LIBHP_DICT_H__ */
#endif //LIBHP_WITH_REDISDICT
