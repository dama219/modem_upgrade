#ifndef __UPGRADE_H__
#define __UPGRADE_H__
#include "device.h"

#define UPGRADE_FAILED  -1      //upgrade failed
#define UPGRADE_NONE    0      //no need upgrade
#define UPGRADE_SUCCESS 1       //upgrade success

int upgrade_device(struct product_info *pro_info, struct usb_device_info *usb_info);
#endif

