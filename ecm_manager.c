#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>
#include <pthread.h>
#include <sys/utsname.h>
#include <stdbool.h>
#include<pthread.h>
#include <cutils/properties.h>
#include "log.h"
#include "usb.h"
#include "device.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

struct ecm_usb_info
{
    unsigned short vid;
    unsigned short pid;
    char usb_root[256];
};

static bool s_closed = false;

static struct ecm_usb_info ecm_usb_info_table[] = {
    {0x2c7c, 0x0125, "fe380000"},
};

static bool is_usb_match_ecm(unsigned short vid, unsigned short pid, char *usb_root)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(ecm_usb_info_table); i++)
    {
        if (vid == ecm_usb_info_table[i].vid && pid == ecm_usb_info_table[i].pid &&
            strstr(usb_root, ecm_usb_info_table[i].usb_root))
            return true;
    }

    return false;
}

struct usb_device_info *get_ecm_usb_info()
{
	int count;
	struct usb_device_info *usb_info = NULL;
    int i;

    count = init_usb_devices_info();
    if (count)
    {
        for (i = 0; i < count; i++)
        {
            usb_info = get_usb_device_info(i);
            if (is_usb_match_ecm(usb_info->idVendor, usb_info->idProduct, usb_info->usb_root_name))
                break;
            else
                usb_info = NULL;
        }
    }

	return usb_info;
}

static void on_at_reader_closed()
{
	s_closed = true;
	deinit_device(NULL);
}

static void on_at_timeout()
{
	s_closed = true;
	deinit_device(NULL);
}

void start_ecm_manager()
{
    int ret = 0;
    int count = 0;
    int i = 0;
    struct usb_device_info *usb_info = NULL;
    net_mode mode;
    char iccid[256];
    char imei[256];

    while(1)
    {
        property_set("sys.ecm.imei", "");
        property_set("sys.ecm.sim.iccid", "");
        usb_info = get_ecm_usb_info();
        if (!usb_info)
        {
            if (i++ > 12)
                return;

            LOGI("can't find ecm usb device");
            sleep(5);
            continue;
        }
        i = 0;
        LOGI("init ecm device ...");
        ret = init_device(usb_info, on_at_timeout, on_at_reader_closed);
        if (ret)
        {
            LOGE("init ecm device failed");
            continue;
        }

        mode = get_net_mode();
        if (mode < 0)
        {
            LOGE("get net mode failed");
            continue;
        }
        if (mode != ECM)
        {
            ret = set_net_mode(ECM);
            if (ret)
            {
                LOGE("set ecm mode failed");
                continue;
            }
            reset_device();
            deinit_device(usb_info);
            sleep(5);
            continue;
        }

        memset(imei, 0x0, sizeof(imei));
        ret = get_device_IMEI(imei);
        if (!ret)
        {
            property_set("sys.ecm.imei", imei);
            LOGI("ecm IMEI is %s", imei);
        }
        else
        {
            LOGE("get ecm IMEI failed");
            property_set("sys.ecm.imei", "");
            continue;
        }

        //use esim, must disabled card inserted detection
        ret = set_sim_detect(0, 1);
        if (ret)
            continue;
        ret = set_sim_status_report(0);
        if (ret)
            continue;

        memset(iccid, 0x0, sizeof(iccid));
        ret = get_sim_ICCID(iccid);
        if (!ret)
        {
            LOGI("ecm sim iccid is %s", iccid);
            property_set("sys.ecm.sim.iccid", iccid);
        }
        else
        {
            property_set("sys.ecm.sim.iccid", "");
        }
        s_closed = false;

        while(!s_closed)
        {
            at_send_command("AT+CPIN?", NULL);
            at_send_command("AT+CSQ", NULL);
            at_send_command("AT+QCSQ", NULL);
            at_send_command("AT+CGREG?", NULL);
            at_send_command("AT+COPS?", NULL);
            sleep(3);
        }
    }
}

