#ifndef RTL_TALLY_H
#define RTL_TALLY_H

#include "rtl_core.h"
#include <stdbool.h>

int rtl_tally_read(rtl_dev_t *dev, rtl_tally_counters_t *out_counters);
void rtl_tally_print_stats(rtl_dev_t *dev, bool json_format);
int rtl_tally_reset(rtl_dev_t *dev);
void rtl_tally_watch(rtl_dev_t *dev, double interval_sec);

#endif /* RTL_TALLY_H */
