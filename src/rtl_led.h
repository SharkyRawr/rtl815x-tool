#ifndef RTL_LED_H
#define RTL_LED_H

#include "rtl_core.h"

int rtl_led_print_status(rtl_dev_t *dev);
int rtl_led_set_mode(rtl_dev_t *dev, const char *mode);

#endif /* RTL_LED_H */
