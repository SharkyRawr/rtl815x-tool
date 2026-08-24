#ifndef RTL_DIAG_H
#define RTL_DIAG_H

#include "rtl_core.h"

void rtl_diag_print_status(rtl_dev_t *dev);
void rtl_diag_print_phy(rtl_dev_t *dev);
int rtl_diag_restart_an(rtl_dev_t *dev);

#endif /* RTL_DIAG_H */
