#include "../sam.h"
#include <stdio.h>

void
sam_policy_fair(const int         j,
                struct appinfo   *apps_sorted[],
                int               per_app_cpu_budget[],
                int               fair_share)
{
    /*If application has already been sorted, then just check for adjusting fair share*/
    if (apps_sorted[j]->times_allocated > SAM_INITIAL_ALLOCS) {
        //If fair share has changed then adjust to new fair share
        if (apps_sorted[j]->curr_fair_share != fair_share && apps_sorted[j]->perf_history[fair_share] != 0) {
            apps_sorted[j]->curr_fair_share = fair_share;
            printf("FAIR SHARE [APP %6d] changing fair share\n", apps_sorted[j]->pid);
        }

    } else {
        //Give the application fair share
        per_app_cpu_budget[j] = fair_share;
        printf("FAIR SHARE [APP %6d] setting fair share \n", apps_sorted[j]->pid);
    }
}
