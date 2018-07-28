#ifndef MAPPER_H
#define MAPPER_H

enum metric {
    METRIC_ACTIVE,
    METRIC_AVGIPC,
    METRIC_MEM,
    METRIC_INTRA,
    METRIC_INTER,
    N_METRICS,
    /*
     * These other metrics aren't used for sorting.
     */
    METRIC_REMOTE,
    METRIC_IpCOREpS,
    METRIC_IPS,
};

#endif
