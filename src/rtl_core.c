#include "rtl_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_string_property(io_service_t service, CFStringRef key, char *out, size_t size) {
    CFTypeRef value = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    bool ok = value && CFGetTypeID(value) == CFStringGetTypeID() &&
              CFStringGetCString(value, out, (CFIndex)size, kCFStringEncodingUTF8) && out[0];
    if (value) CFRelease(value);
    if (!ok) out[0] = '\0';
    return ok;
}

static bool find_network_interface(io_service_t current, rtl_dev_t *dev) {
    if (IOObjectConformsTo(current, "IONetworkInterface") &&
        read_string_property(current, CFSTR("BSD Name"), dev->bsd_name, sizeof(dev->bsd_name)))
        return true;

    io_iterator_t iter;
    if (IORegistryEntryGetChildIterator(current, kIOServicePlane, &iter) != KERN_SUCCESS)
        return false;

    bool found = false;
    io_service_t child;
    while ((child = IOIteratorNext(iter)) != 0) {
        found = find_network_interface(child, dev);
        /* Only identify the controller bound to USB on this interface's branch. */
        if (found && (current == dev->service || IOObjectConformsTo(current, "IOUSBHostInterface")) &&
            (IOObjectConformsTo(child, "IOUserNetworkEthernet") ||
             IOObjectConformsTo(child, "IONetworkController"))) {
            if (!read_string_property(child, CFSTR("IOUserServerName"), dev->driver, sizeof(dev->driver)))
                read_string_property(child, CFSTR("CFBundleIdentifier"), dev->driver, sizeof(dev->driver));
        }
        IOObjectRelease(child);
        if (found) break;
    }
    IOObjectRelease(iter);
    return found;
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
    if (!list) {
        IOObjectRelease(iter);
        return -1;
    }

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
                    IOObjectRelease(iter);
                    rtl_free_devices(list, count);
                    return -1;
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
                    int64_t bps;
                    if (CFNumberGetValue((CFNumberRef)speedRef, kCFNumberSInt64Type, &bps) && bps > 0)
                        snprintf(entry->usb_speed, sizeof(entry->usb_speed), "%g Mbps", bps / 1000000.0);
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
            find_network_interface(service, entry);
            if (entry->bsd_name[0] == '\0') {
                snprintf(entry->bsd_name, sizeof(entry->bsd_name), "dont know");
            }

            /* Open briefly to read chip version and MAC address */
            if (rtl_open_device(entry) == 0) {
                uint16_t tcr1;
                uint32_t mac_l, mac_h;
                entry->version_info = lookup_version_info(0);
                if (rtl_ocp_read_word(entry, MCU_TYPE_PLA, PLA_TCR1, &tcr1) == 0) {
                    entry->revision_known = true;
                    entry->version_raw = tcr1 & VERSION_MASK;
                    entry->version_info = lookup_version_info(entry->version_raw);
                }
                if (rtl_ocp_read_dword(entry, MCU_TYPE_PLA, PLA_IDR, &mac_l) == 0 &&
                    rtl_ocp_read_dword(entry, MCU_TYPE_PLA, PLA_IDR + 4, &mac_h) == 0) {
                    snprintf(entry->mac_address, sizeof(entry->mac_address),
                             "%02x:%02x:%02x:%02x:%02x:%02x", mac_l & 0xff, (mac_l >> 8) & 0xff,
                             (mac_l >> 16) & 0xff, mac_l >> 24, mac_h & 0xff, (mac_h >> 8) & 0xff);
                }

                rtl_close_device(entry);
            } else {
                entry->version_info = lookup_version_info(0);
            }

            count++;
            continue;
        }

        IOObjectRelease(service);
    }
    if (!IOIteratorIsValid(iter)) {
        IOObjectRelease(iter);
        rtl_free_devices(list, count);
        return -1;
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

    return index == -1 ? &devices[0] : NULL;
}

int rtl_ocp_read_block(rtl_dev_t *dev, uint16_t index, uint16_t size, void *data, uint16_t type) {
    if (!dev || !dev->dev_interface || !data || (index & 3) || (size & 3) ||
        (uint32_t)index + size > 0x10000) return -1;
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
        if (ret != kIOReturnSuccess || req.wLenDone != chunk) {
            fprintf(stderr, "Error: USB register transfer at 0x%04x failed (0x%08x, %u/%u bytes).\n",
                    off, (unsigned)ret, (unsigned)req.wLenDone, chunk);
            return -1;
        }
        rem -= chunk; off += chunk; ptr += chunk;
    }
    return 0;
}

int rtl_ocp_write_block(rtl_dev_t *dev, uint16_t index, uint16_t byteen, uint16_t size, void *data, uint16_t type) {
    if (!dev || !dev->dev_interface || !data || (index & 3) || (size & 3) ||
        (uint32_t)index + size > 0x10000) return -1;
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
        if (ret != kIOReturnSuccess || req.wLenDone != chunk) {
            fprintf(stderr, "Error: USB register transfer at 0x%04x failed (0x%08x, %u/%u bytes).\n",
                    off, (unsigned)ret, (unsigned)req.wLenDone, chunk);
            return -1;
        }
        rem -= chunk; off += chunk; ptr += chunk;
    }
    return 0;
}

int rtl_ocp_read_dword(rtl_dev_t *dev, uint16_t type, uint16_t index, uint32_t *out) {
    uint32_t data;
    if (!out || rtl_ocp_read_block(dev, index & ~3, 4, &data, type) != 0) return -1;
    *out = CFSwapInt32LittleToHost(data);
    return 0;
}

int rtl_ocp_read_word(rtl_dev_t *dev, uint16_t type, uint16_t index, uint16_t *out) {
    uint32_t data;
    if (!out || rtl_ocp_read_dword(dev, type, index & ~3, &data) != 0) return -1;
    *out = (uint16_t)(data >> ((index & 2) * 8));
    return 0;
}

int rtl_ocp_read_byte(rtl_dev_t *dev, uint16_t type, uint16_t index, uint8_t *out) {
    uint32_t data;
    if (!out || rtl_ocp_read_dword(dev, type, index & ~3, &data) != 0) return -1;
    *out = (uint8_t)(data >> ((index & 3) * 8));
    return 0;
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

static int rtl_select_ocp_page(rtl_dev_t *dev, uint16_t addr) {
    if (!dev) return -1;
    uint16_t base = addr & 0xf000;
    if (base != dev->ocp_base) {
        if (rtl_ocp_write_word(dev, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, base) != 0) {
            dev->ocp_base = 0xffff;
            return -1;
        }
        dev->ocp_base = base;
    }
    return 0;
}

int rtl_ocp_reg_read(rtl_dev_t *dev, uint16_t addr, uint16_t *out) {
    if (!out || rtl_select_ocp_page(dev, addr) != 0) return -1;
    return rtl_ocp_read_word(dev, MCU_TYPE_PLA, (addr & 0x0fff) | 0xb000, out);
}

int rtl_ocp_reg_write(rtl_dev_t *dev, uint16_t addr, uint16_t data) {
    if (rtl_select_ocp_page(dev, addr) != 0) return -1;
    return rtl_ocp_write_word(dev, MCU_TYPE_PLA, (addr & 0x0fff) | 0xb000, data);
}

int rtl_mdio_read(rtl_dev_t *dev, uint32_t reg_addr, uint16_t *out) {
    return rtl_ocp_reg_read(dev, (uint16_t)(OCP_BASE_MII + reg_addr * 2), out);
}

int rtl_mdio_write(rtl_dev_t *dev, uint32_t reg_addr, uint16_t data) {
    return rtl_ocp_reg_write(dev, (uint16_t)(OCP_BASE_MII + reg_addr * 2), data);
}
