#ifndef _MB_UTIL_H_
#define _MB_UTIL_H_

#include "mb_platform.h"

// execution modes
enum {
    MODE_LO,
    MODE_HI
};

int mbdata_init(char **buffer, int mode, long num_threads);
void mbdata(char *buffer, int mode, double time, long num_threads, long tid);
void mbdata_final(char *buffer);

#endif  /* _MB_UTIL_H_ */
