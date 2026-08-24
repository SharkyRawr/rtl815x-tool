#include "rtl_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void find_bsd_interface_recursive(io_service_t current, char *out_name, size_t max_len) {
    if (out_name[0] != '\0') return;

    CFTypeRef bsdRef = IORegistryEntryCreateCFProperty(current, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
    if (bsdRef) {
        if (CFGetTypeID(bsdRef) == CFStringGetTypeID()) {
            CFStringGetCString((CFStringRef)bsdRef, out_name, (CFIndex)max_len, kCFStringEncodingUTF8);
        }
        CFRelease(bsdRef);
        return;
    }

    io_iterator_t iter;
    if (IORegistryEntryGetChildIterator(current, kIOServicePlane, &iter) == KERN_SUCCESS) {
        io_service_t child;
        while ((child = IOIteratorNext(iter)) != 0) {
            find_bsd_interface_recursive(child, out_name, max_len);
            IOObjectRelease(child);
            if (out_name[0] != '\0') break;
        }
        IOObjectRelease(iter);
    }
}

static const rtl_version_info_t *lookup_version_info(uint16_t version_raw) {
    for (int i = 0; RTL_CHIP_VERSIONS[i].version_code != 0; i++) {
        if (RTL_CHIP_VERSIONS[i].version_code == version_raw) {
            return &RTL_CHIP_VERSIONS[i];
        }
    }
    /* Default to the last fallback entry */
    int i = 0;
    while (RTL_CHIP_VERSIONS[i].version_code != 0) i++;
    return &RTL_CHIP_VERSIONS[i];
}

int rtl_open_device(rtl_dev_t *dev) {
    if (!dev || !dev->service) return -1;
    if (dev->dev_interface) return 0;

    IOCFPlugInInterface **plugin = NULL;
    SInt32 score;
    kern_return_t kr = IOCreatePlugInInterfaceForService(dev->service,
                                                         kIOUSBDeviceUserClientTypeID,
                                                         kIOCFPlugInInterfaceID,
                                                         &plugin,
                                                         &score);
    if (kr != KERN_SUCCESS || !plugin) {
        return -1;
    }

    IOUSBDeviceInterface **intf = NULL;
    kr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID *)&intf);
    (*plugin)->Release(plugin);

    if (kr != KERN_SUCCESS || !intf) {
        return -1;
    }

    dev->dev_interface = intf;
    dev->ocp_base = 0xffff;
    return 0;
}

void rtl_close_device(rtl_dev_t *dev) {
    if (dev && dev->dev_interface) {
        (*dev->dev_interface)->Release(dev->dev_interface);
        dev->dev_interface = NULL;
    }
}

int rtl_find_devices(rtl_dev_t **out_devices, int *out_count) {
    if (!out_devices || !out_count) return -1;
    *out_devices = NULL;
    *out_count = 0;

    CFMutableDictionaryRef matching = IOServiceMatching("IOUSBDevice");
    if (!matching) return -1;

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) != KERN_SUCCESS) {
        return -1;
    }

    int capacity = 4;
    rtl_dev_t *list = calloc(capacity, sizeof(rtl_dev_t));
    int count = 0;

    io_service_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
        CFTypeRef vidRef = IORegistryEntryCreateCFProperty(service, CFSTR("idVendor"), kCFAllocatorDefault, 0);
        CFTypeRef pidRef = IORegistryEntryCreateCFProperty(service, CFSTR("idProduct"), kCFAllocatorDefault, 0);

        int vid = 0, pid = 0;
        if (vidRef) {
            if (CFGetTypeID(vidRef) == CFNumberGetTypeID()) {
                CFNumberGetValue((CFNumberRef)vidRef, kCFNumberIntType, &vid);
            }
            CFRelease(vidRef);
        }
        if (pidRef) {
            if (CFGetTypeID(pidRef) == CFNumberGetTypeID()) {
                CFNumberGetValue((CFNumberRef)pidRef, kCFNumberIntType, &pid);
            }
            CFRelease(pidRef);
        }

        if (vid == REALTEK_VID && (pid == PID_RTL8156 || pid == PID_RTL8153 ||
                                   pid == PID_RTL8152 || pid == PID_RTL8157 ||
                                   pid == PID_RTL8155)) {
            if (count >= capacity) {
                capacity *= 2;
                rtl_dev_t *new_list = realloc(list, capacity * sizeof(rtl_dev_t));
                if (!new_list) {
                    IOObjectRelease(service);
                    break;
                }
                list = new_list;
            }

            rtl_dev_t *entry = &list[count];
            memset(entry, 0, sizeof(rtl_dev_t));
            entry->service = service;
            entry->vid = (uint16_t)vid;
            entry->pid = (uint16_t)pid;
            entry->ocp_base = 0xffff;

            /* Extract Location ID */
            CFTypeRef locRef = IORegistryEntryCreateCFProperty(service, CFSTR("locationID"), kCFAllocatorDefault, 0);
            if (locRef) {
                if (CFGetTypeID(locRef) == CFNumberGetTypeID()) {
                    CFNumberGetValue((CFNumberRef)locRef, kCFNumberSInt32Type, &entry->location_id);
                }
                CFRelease(locRef);
            }

            /* Extract Link Speed */
            CFTypeRef speedRef = IORegistryEntryCreateCFProperty(service, CFSTR("UsbLinkSpeed"), kCFAllocatorDefault, 0);
            if (speedRef) {
                if (CFGetTypeID(speedRef) == CFNumberGetTypeID()) {
                    CFNumberGetValue((CFNumberRef)speedRef, kCFNumberSInt64Type, &entry->usb_link_speed_bps);
                }
                CFRelease(speedRef);
            }

            /* Extract Product / Vendor strings */
            CFTypeRef prodRef = IORegistryEntryCreateCFProperty(service, CFSTR("USB Product Name"), kCFAllocatorDefault, 0);
            if (!prodRef) prodRef = IORegistryEntryCreateCFProperty(service, CFSTR("kUSBProductString"), kCFAllocatorDefault, 0);
            if (prodRef) {
                if (CFGetTypeID(prodRef) == CFStringGetTypeID()) {
                    CFStringGetCString((CFStringRef)prodRef, entry->product_name, sizeof(entry->product_name), kCFStringEncodingUTF8);
                }
                CFRelease(prodRef);
            }

            CFTypeRef vendRef = IORegistryEntryCreateCFProperty(service, CFSTR("USB Vendor Name"), kCFAllocatorDefault, 0);
            if (!vendRef) vendRef = IORegistryEntryCreateCFProperty(service, CFSTR("kUSBVendorString"), kCFAllocatorDefault, 0);
            if (vendRef) {
                if (CFGetTypeID(vendRef) == CFStringGetTypeID()) {
                    CFStringGetCString((CFStringRef)vendRef, entry->vendor_name, sizeof(entry->vendor_name), kCFStringEncodingUTF8);
                }
                CFRelease(vendRef);
            }

            /* Map BSD interface name (enX) */
            find_bsd_interface_recursive(service, entry->bsd_name, sizeof(entry->bsd_name));
            if (entry->bsd_name[0] == '\0') {
                snprintf(entry->bsd_name, sizeof(entry->bsd_name), "N/A");
            }

            /* Open briefly to read chip version and MAC address */
            if (rtl_open_device(entry) == 0) {
                uint16_t tcr1 = rtl_ocp_read_word(entry, MCU_TYPE_PLA, PLA_TCR1);
                entry->version_raw = tcr1 & VERSION_MASK;
                entry->version_info = lookup_version_info(entry->version_raw);

                uint32_t mac_l = rtl_ocp_read_dword(entry, MCU_TYPE_PLA, PLA_IDR);
                uint32_t mac_h = rtl_ocp_read_dword(entry, MCU_TYPE_PLA, PLA_IDR + 4);
                entry->mac_addr[0] = (uint8_t)(mac_l & 0xff);
                entry->mac_addr[1] = (uint8_t)((mac_l >> 8) & 0xff);
                entry->mac_addr[2] = (uint8_t)((mac_l >> 16) & 0xff);
                entry->mac_addr[3] = (uint8_t)((mac_l >> 24) & 0xff);
                entry->mac_addr[4] = (uint8_t)(mac_h & 0xff);
                entry->mac_addr[5] = (uint8_t)((mac_h >> 8) & 0xff);

                rtl_close_device(entry);
            } else {
                entry->version_info = lookup_version_info(0);
            }

            count++;
            continue;
        }

        IOObjectRelease(service);
    }
    IOObjectRelease(iter);

    *out_devices = list;
    *out_count = count;
    return 0;
}

void rtl_free_devices(rtl_dev_t *devices, int count) {
    if (!devices) return;
    for (int i = 0; i < count; i++) {
        rtl_close_device(&devices[i]);
        if (devices[i].service) {
            IOObjectRelease(devices[i].service);
            devices[i].service = 0;
        }
    }
    free(devices);
}

rtl_dev_t *rtl_select_device(rtl_dev_t *devices, int count, const char *bsd_name, int index) {
    if (!devices || count <= 0) return NULL;

    if (bsd_name && strlen(bsd_name) > 0) {
        for (int i = 0; i < count; i++) {
            if (strcasecmp(devices[i].bsd_name, bsd_name) == 0) {
                return &devices[i];
            }
        }
        return NULL;
    }

    if (index >= 0 && index < count) {
        return &devices[index];
    }

    return &devices[0];
}

int rtl_ocp_read_block(rtl_dev_t *dev, uint16_t index, uint16_t size, void *data, uint16_t type) {
    if (!dev || !dev->dev_interface || (index & 3) || (size & 3)) return -1;
    uint8_t *ptr = (uint8_t *)data;
    uint16_t rem = size, off = index;
    while (rem > 0) {
        uint16_t chunk = rem > 64 ? 64 : rem;
        IOUSBDevRequest req = {
            .bmRequestType = RTL8152_REQT_READ,
            .bRequest = RTL8152_REQ_GET_REGS,
            .wValue = off,
            .wIndex = type,
            .wLength = chunk,
            .pData = ptr
        };
        IOReturn ret = (*dev->dev_interface)->DeviceRequest(dev->dev_interface, &req);
        if (ret != kIOReturnSuccess) return -1;
        rem -= chunk; off += chunk; ptr += chunk;
    }
    return 0;
}

int rtl_ocp_write_block(rtl_dev_t *dev, uint16_t index, uint16_t byteen, uint16_t size, void *data, uint16_t type) {
    if (!dev || !dev->dev_interface || (index & 3) || (size & 3)) return -1;
    uint8_t *ptr = (uint8_t *)data;
    uint16_t rem = size, off = index;
    while (rem > 0) {
        uint16_t chunk = rem > 64 ? 64 : rem;
        IOUSBDevRequest req = {
            .bmRequestType = RTL8152_REQT_WRITE,
            .bRequest = RTL8152_REQ_SET_REGS,
            .wValue = off,
            .wIndex = (uint16_t)(type | byteen),
            .wLength = chunk,
            .pData = ptr
        };
        IOReturn ret = (*dev->dev_interface)->DeviceRequest(dev->dev_interface, &req);
        if (ret != kIOReturnSuccess) return -1;
        rem -= chunk; off += chunk; ptr += chunk;
    }
    return 0;
}

uint32_t rtl_ocp_read_dword(rtl_dev_t *dev, uint16_t type, uint16_t index) {
    uint32_t data = 0;
    if (rtl_ocp_read_block(dev, index & ~3, 4, &data, type) != 0) {
        return 0xffffffff;
    }
    return CFSwapInt32LittleToHost(data);
}

uint16_t rtl_ocp_read_word(rtl_dev_t *dev, uint16_t type, uint16_t index) {
    uint32_t data = rtl_ocp_read_dword(dev, type, index & ~3);
    uint8_t shift = (index & 2) * 8;
    return (uint16_t)((data >> shift) & 0xffff);
}

uint8_t rtl_ocp_read_byte(rtl_dev_t *dev, uint16_t type, uint16_t index) {
    uint32_t data = rtl_ocp_read_dword(dev, type, index & ~3);
    uint8_t shift = (index & 3) * 8;
    return (uint8_t)((data >> shift) & 0xff);
}

int rtl_ocp_write_dword(rtl_dev_t *dev, uint16_t type, uint16_t index, uint32_t data) {
    uint32_t val = CFSwapInt32HostToLittle(data);
    return rtl_ocp_write_block(dev, index & ~3, BYTE_EN_DWORD, 4, &val, type);
}

int rtl_ocp_write_word(rtl_dev_t *dev, uint16_t type, uint16_t index, uint16_t data) {
    uint32_t mask = 0xffff;
    uint16_t byen = BYTE_EN_WORD;
    uint8_t shift = index & 2;
    uint32_t data32 = (uint32_t)data & mask;
    if (shift) {
        byen = (uint16_t)(byen << shift);
        data32 <<= 16;
    }
    uint32_t val = CFSwapInt32HostToLittle(data32);
    return rtl_ocp_write_block(dev, index & ~3, byen, 4, &val, type);
}

int rtl_ocp_write_byte(rtl_dev_t *dev, uint16_t type, uint16_t index, uint8_t data) {
    uint16_t byen = BYTE_EN_BYTE;
    uint8_t shift = index & 3;
    uint32_t data32 = (uint32_t)data & 0xff;
    if (shift) {
        byen = (uint16_t)(byen << shift);
        data32 <<= (shift * 8);
    }
    uint32_t val = CFSwapInt32HostToLittle(data32);
    return rtl_ocp_write_block(dev, index & ~3, byen, 4, &val, type);
}

uint16_t rtl_ocp_reg_read(rtl_dev_t *dev, uint16_t addr) {
    uint16_t ocp_base = addr & 0xf000;
    if (ocp_base != dev->ocp_base) {
        rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, ocp_base);
        dev->ocp_base = ocp_base;
    }
    uint16_t ocp_index = (addr & 0x0fff) | 0xb000;
    return rtl_ocp_read_word(dev, MCU_TYPE_PLA, ocp_index);
}

int rtl_ocp_reg_write(rtl_dev_t *dev, uint16_t addr, uint16_t data) {
    uint16_t ocp_base = addr & 0xf000;
    if (ocp_base != dev->ocp_base) {
        rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, ocp_base);
        dev->ocp_base = ocp_base;
    }
    uint16_t ocp_index = (addr & 0x0fff) | 0xb000;
    return rtl_ocp_write_word(dev, MCU_TYPE_PLA, ocp_index, data);
}

uint16_t rtl_mdio_read(rtl_dev_t *dev, uint32_t reg_addr) {
    return rtl_ocp_reg_read(dev, (uint16_t)(OCP_BASE_MII + reg_addr * 2));
}

int rtl_mdio_write(rtl_dev_t *dev, uint32_t reg_addr, uint16_t data) {
    return rtl_ocp_reg_write(dev, (uint16_t)(OCP_BASE_MII + reg_addr * 2), data);
}
