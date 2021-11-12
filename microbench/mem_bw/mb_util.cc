#include <stdlib.h>
#include <string.h>
#include "mb_util.h"

long buffer_pages = 0;

int mbdata_init(char **buffer, int mode, long num_threads) {
    buffer_pages = BUFFER_PAGES_PER_THREAD * num_threads;

    if (posix_memalign((void**)buffer, PAGE_SIZE, buffer_pages * PAGE_SIZE)) {
        return -1;
    }

    return 0;
}

void mbdata(char *buffer, int mode, double time, long num_threads, long tid) {
    long blocks_per_page = PAGE_SIZE / CACHELINE_SIZE;
    long rounds;

    switch (mode) {
        case MODE_LO:
            rounds = (long) (lo_round_10ms * 100 * time);

            for (long i = 0; i < rounds; i++) {
                for (long j = 0; j < blocks_per_page; j++) {
                    for (long k = tid; k < buffer_pages; k += num_threads) {
                        long buffer_offset = (k * PAGE_SIZE) + (j * CACHELINE_SIZE);
                        memset(buffer + buffer_offset, i, CACHELINE_SIZE);
                    }
                }
            }

            break;

        case MODE_HI:
            rounds = (long) (hi_round_10ms * 100 * time);

            for (long i = 0; i < rounds; i++) {
                for (long j = tid; j < buffer_pages; j += num_threads) {
                    long buffer_offset = j * PAGE_SIZE;
                    memset(buffer + buffer_offset, i, PAGE_SIZE);
                }
            }

            break;
    }
}

void mbdata_final(char *buffer) {
    if (buffer!=NULL) free(buffer);
}
