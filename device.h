#ifndef __DEVICE_H__
#define __DEVICE_H__
#include "atchannel.h"

struct product_info
{
    char vendor[256];
    char dev_type[256];
    char revision[256];
};

typedef enum {
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2, /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    SIM_BAD = 6,
} sim_status;

typedef enum {
	RMNET,
	ECM,
	MBIM,
	RNDIS
} net_mode;

int init_device(struct usb_device_info *device, TimeoutCallback timeout, ReaderClosedCallback closed);
void deinit_device(struct usb_device_info *device);
int get_product_info_from_device(struct product_info *pro_info);
int save_product_info(struct product_info *pro_info);
int get_product_info_from_local(struct product_info *pro_info);
int get_sim_ICCID(char *iccid);
int get_device_IMEI(char *imei);
sim_status get_sim_status();
net_mode get_net_mode();
int set_net_mode(net_mode mode);
int reset_device();
int set_sim_detect(int enable, int level);
int set_sim_status_report(int enable);
#endif

