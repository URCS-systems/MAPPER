#ifndef MAPPER_H
#define MAPPER_H

enum metric {
    METRIC_ACTIVE,
    METRIC_AVGIPC,
    METRIC_MEM,
    METRIC_INTRA,
    METRIC_INTER,
    N_METRICS,
};

/*
 * These other metrics aren't used for sorting.
 */
enum extra_metric {
    EXTRA_METRIC_REMOTE,
    EXTRA_METRIC_IpCOREpS,
    EXTRA_METRIC_IPS,
    N_EXTRA_METRICS,
};

#endif
