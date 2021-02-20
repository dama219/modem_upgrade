#ifndef __USB_H__
#define __USB_H__

#define MAX_PATH 256
#define MAX_CARD_NUM 4

#define USB_AT_INF 0
#define USB_PPP_INF 1


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

struct usb_device_info {
    int idVendor;
    int idProduct;
    char usbdevice_path[MAX_PATH];
    char usb_root_name[MAX_PATH];
    char ttyAT_name[MAX_PATH];
    char ttyndis_name[MAX_PATH];
};

/*
Find all 4G modules
return modules count
*/
int init_usb_devices_info(void);
void usb_set_autosuspend(int enable);
char *get_AT_tty_name_by_root(char *usb_root);
struct usb_device_info *get_usb_device_info(unsigned int index);
int is_usb_download_mode(unsigned short vid, unsigned short pid);
#endif

