/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2023/7/31
 *
 * random on Win32
 * */
/////////////////////////////////////////////////////////////////////////////////////////

#ifdef _MSC_VER
#include "hp/hp_stdlib.h"
#include <Windows.h>
/* Replace MS C rtl rand which is 15bit with 32 bit */
typedef BOOLEAN(_stdcall* RtlGenRandomFunc)(void * RandomBuffer, ULONG RandomBufferLength);
static RtlGenRandomFunc RtlGenRandom = 0;

int random()
{
    unsigned int x = 0;
    if (RtlGenRandom == NULL) {
        // Load proc if not loaded
        HMODULE lib = LoadLibraryA("advapi32.dll");
        RtlGenRandom = (RtlGenRandomFunc) GetProcAddress(lib, "SystemFunction036");
        if (RtlGenRandom == NULL) return 1;
    }
    RtlGenRandom(&x, sizeof(unsigned int));
    return (int) (x >> 1);
}

#endif /* _MSC_VER */
