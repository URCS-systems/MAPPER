#ifndef MAPPER_H
#define MAPPER_H

enum metric {
    METRIC_ACTIVE,
    METRIC_AVGIPC,
    METRIC_MEM,
    METRIC_INTRA,
    METRIC_INTER,
    METRIC_REMOTE,
	METRIC_IpCOREpS, // Instructions Per Core per Second
    N_METRICS
};

#endif
