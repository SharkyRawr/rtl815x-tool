#ifndef RTL_POWER_H
#define RTL_POWER_H

#include "rtl_core.h"
#include <stdbool.h>

int rtl_power_print_status(rtl_dev_t *dev);
int rtl_power_set_no_sleep(rtl_dev_t *dev, bool disable_sleep);

#endif /* RTL_POWER_H */
