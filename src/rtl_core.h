#ifndef RTL_CORE_H
#define RTL_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include "rtl_regs.h"

typedef struct {
    IOUSBDeviceInterface **dev_interface;
    io_service_t service;
    uint16_t vid;
    uint16_t pid;
    uint16_t version_raw;
    const rtl_version_info_t *version_info;
    bool revision_known;
    char mac_address[18];
    char driver[256];
    char bsd_name[32];
    char product_name[128];
    char vendor_name[128];
    char usb_speed[48];
    int64_t usb_bps;
    uint32_t location_id;
    uint16_t ocp_base;
} rtl_dev_t;

/* Device Enumeration & Lifecycle */
bool rtl_is_adapter_usb_id(uint16_t vid, uint16_t pid);
bool rtl_is_8157_usb_id(uint16_t vid, uint16_t pid);
void rtl_read_version(rtl_dev_t *dev);
int rtl_find_devices(rtl_dev_t **out_devices, int *out_count);
int rtl_open_device(rtl_dev_t *dev);
void rtl_close_device(rtl_dev_t *dev);
void rtl_free_devices(rtl_dev_t *devices, int count);
rtl_dev_t *rtl_select_device(rtl_dev_t *devices, int count, const char *bsd_name, int index);

/* Low-level OCP transfers */
int rtl_ocp_read_block(rtl_dev_t *dev, uint16_t index, uint16_t size, void *data, uint16_t type);
int rtl_ocp_write_block(rtl_dev_t *dev, uint16_t index, uint16_t byteen, uint16_t size, void *data, uint16_t type);

/* Reads return 0 on success and leave *out unchanged on failure. */
int rtl_ocp_read_dword(rtl_dev_t *dev, uint16_t type, uint16_t index, uint32_t *out);
int rtl_ocp_read_word(rtl_dev_t *dev, uint16_t type, uint16_t index, uint16_t *out);
int rtl_ocp_read_byte(rtl_dev_t *dev, uint16_t type, uint16_t index, uint8_t *out);

int rtl_ocp_write_dword(rtl_dev_t *dev, uint16_t type, uint16_t index, uint32_t data);
int rtl_ocp_write_word(rtl_dev_t *dev, uint16_t type, uint16_t index, uint16_t data);
int rtl_ocp_write_byte(rtl_dev_t *dev, uint16_t type, uint16_t index, uint8_t data);

/* RTL8157 indirect IP and BMU register access */
int rtl_adv_read(rtl_dev_t *dev, uint16_t space, uint16_t addr, uint32_t *out);
int rtl_adv_write(rtl_dev_t *dev, uint16_t space, uint16_t addr, uint32_t value);

/* GPHY / MII register access */
int rtl_ocp_reg_read(rtl_dev_t *dev, uint16_t addr, uint16_t *out);
int rtl_ocp_reg_write(rtl_dev_t *dev, uint16_t addr, uint16_t data);
int rtl_mdio_read(rtl_dev_t *dev, uint32_t reg_addr, uint16_t *out);
int rtl_mdio_write(rtl_dev_t *dev, uint32_t reg_addr, uint16_t data);

#endif /* RTL_CORE_H */
