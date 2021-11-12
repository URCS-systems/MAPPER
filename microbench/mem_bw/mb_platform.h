#ifndef _MB_PLATFORM_H_
#define _MB_PLATFORM_H_

#if defined(__x86_64__)

#define CACHELINE_SIZE 64
#define PAGE_SIZE      4096
#define BUFFER_PAGES_PER_THREAD 4096

const unsigned int lo_round_10ms = 2;
const unsigned int hi_round_10ms = 10;

#elif defined(__powerpc64__)

#define CACHELINE_SIZE 128
#define PAGE_SIZE      4096
#define BUFFER_PAGES_PER_THREAD 16384

const unsigned int lo_round_10ms = 1;
const unsigned int hi_round_10ms = 2;

#endif

#endif	/* _MB_PLATFORM_H_ */
