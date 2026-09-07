#ifndef RTL_EEE_H
#define RTL_EEE_H

#include "rtl_core.h"
#include <stdbool.h>

int rtl_eee_print_status(rtl_dev_t *dev);
int rtl_eee_set(rtl_dev_t *dev, bool enable);

#endif /* RTL_EEE_H */
