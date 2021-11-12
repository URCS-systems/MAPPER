#ifndef HW_TIMER_H
#define HW_TIMER_H

#include <sys/time.h>

/*
    The hardware support is the assemble instruction "mfspr".
    Ticks is increased by one every 8 cycles.
    See the architecture manual for reference.

    The libary support is "time_base_to_time" and "read_real_time".
    http://publibn.boulder.ibm.com/doc_link/en_US/a_doc_lib/libs/basetrf2/read_real_time.htm

    Here gethrtime() takes about 55 ns(8.5 cycles). the rollover happens
    every 26.4306 seconds. The return value is in ticks.

*/

#define TICKS_PER_NANO (0.5145)  /* 4.116G/8 */
#define NANOS_PER_TICK (1.9436) /* 8/4.116G */

#define gethrtime() ticks2nano(get_ticks())

static inline unsigned int get_ticks() {
  unsigned int ticks;
  asm volatile ( "mfspr %0, 268"      /* lower time base register */
                 :"=r" (ticks));
  return ticks;
}

static inline double  ticks2nano(unsigned int ticks) {
  return ticks * NANOS_PER_TICK;
}
static inline unsigned int second2tick(unsigned int seconds) {
  return (unsigned int)(TICKS_PER_NANO * seconds * 1000 * 1000 * 1000);
}

#endif /* HW_TIMER_H */
