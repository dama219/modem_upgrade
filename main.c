#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <malloc.h>
#include <termios.h>
#include <stdbool.h>
#include <sys/time.h>
#include <dirent.h>
#include <errno.h>
#include <cutils/properties.h>
#include <sys/sysinfo.h>
#include "usb.h"
#include "log.h"
#include "upgrade.h"
#include "device.h"
#include "ecm_manager.h"

enum operator
{
    MOBILE,
    UNICOM,
    TELCOM
};

struct card_info
{
    enum operator oper;
    char ICCID[256];
};

struct card_info g_card[MAX_CARD_NUM];

static int get_module_count_from_sys_conf()
{
    char count[PROP_VALUE_MAX];

    memset(count, 0x0, sizeof(count));
    property_get("ro.boot.modem-count", count, "1");
    return atoi(count);
}

static void config_modules_net_mode(int module_count, int upgraded)
{
    int ret;
    int set_mode, cur_mode;
    int count;
    int sys_conf_count;
    int i = 0, j = 0;
    struct usb_device_info *usb_info = NULL;
    bool upgrade_done = true;

    sys_conf_count = get_module_count_from_sys_conf();

    do
    {
        count = init_usb_devices_info();
        if (count >= module_count)
            break;
        sleep(1);
    } while (upgraded && i++ < 20);

    if (!count)
        return;

    LOGI("%s: count=%d, sys_conf_count=%d", __FUNCTION__, count, sys_conf_count);

    for (i = 0; i < count; i++)
    {
        usb_info = get_usb_device_info(i);
        if (!usb_info || is_usb_download_mode(usb_info->idVendor, usb_info->idProduct))
            continue;

        if (sys_conf_count > 1)
        {
            if (strstr(usb_info->usb_root_name, "fe3c0000"))
                set_mode = RMNET;
            else if (strstr(usb_info->usb_root_name, "fe380000"))
                set_mode = ECM;
            else
            {
                LOGE("no valid usb root:%s", usb_info->usb_root_name);
                continue;
            }
        }
        else
            set_mode = RMNET;

        LOGI("usb root = %s", usb_info->usb_root_name);
        ret = init_device(usb_info, NULL, NULL);
        if (!ret)
        {
            do
            {
                cur_mode = get_net_mode();
                if (cur_mode >= 0)
                    break;
                else
                {
                    LOGE("get module net mode failed, j=%d", j);
                    sleep(1);
                }
            } while(j++ < 5);

            if (cur_mode >= 0)
            {
                LOGI("cur_mode=%d, set_mode=%d", cur_mode, set_mode);
                if (cur_mode != set_mode)
                {
                    do
                    {
                        ret = set_net_mode(set_mode);
                        if (!ret)
                        {
                            reset_device();
                            break;
                        }
                        else
                        {
                            LOGE("set mode failed, j=%d", j);
                            sleep(1);
                        }
                    } while(j++ < 5);
                }
            }
        }
        deinit_device(usb_info);
    }
}

static int upgrade_modules(int *upgraded_count)
{
    int ret = 0;
    int count, i, j;
    struct usb_device_info *usb_info;
    struct product_info pro_info;
    int upgraded = 0;
    int local_saved = 0;
    char upgrade_count[PROP_VALUE_MAX];

    property_set("sys.modem.upgrade.count", "0");

    count = init_usb_devices_info();
    LOGI("%s: count=%d", __FUNCTION__, count);

    for (i = 0; i < count; i++)
    {
        usb_info = get_usb_device_info(i);
        if (!usb_info)
            break;

        memset(&pro_info, 0x0, sizeof(struct product_info));
        if (is_usb_download_mode(usb_info->idVendor, usb_info->idProduct))
        {
            LOGI("device is in download mode, get product info from local file");
            ret = get_product_info_from_local(&pro_info);
        }
        else
        {
            LOGI("ttyAT=%s, usb_root=%s", usb_info->ttyAT_name, usb_info->usb_root_name);
            ret = init_device(usb_info, NULL, NULL);
            if (ret < 0)
                break;

            j = 0;
            do
            {
                ret = get_product_info_from_device(&pro_info);

                if (!ret)
                    break;
                else
                    LOGE("get product info failed, ret=%d, j=%d", ret, j);

                sleep(1);
            } while(j++ < 5);

            deinit_device(usb_info);

            /*save product info to local file once*/
            if (!ret && !local_saved)
            {
                ret = save_product_info(&pro_info);
                if (!ret)
                    local_saved = 1;
            }
        }

        if (ret)
            break;

        LOGI("product info is : %s, %s, revision: %s", pro_info.vendor, pro_info.dev_type, pro_info.revision);
        ret = upgrade_device(&pro_info, usb_info);

        if (ret == UPGRADE_SUCCESS)
        {
            upgraded++;
            sprintf(upgrade_count, "%d", upgraded);
            property_set("sys.modem.upgrade.count", upgrade_count);
            *upgraded_count = upgraded;
            ret = 0;
        }
    }

    return ret;
}

int main(int argc,char **argv)
{
    int ret;
    int count, sys_conf_count;
    int i = 0, j = 0;
    struct usb_device_info *usb_info;
    struct product_info pro_info;
    int fd;
    char at_dev_path[256];
    int upgraded_count = 0;
    int local_saved = 0;
    net_mode mode;
    struct sysinfo info;
    long uptime;

    /*
        The initialization of 4g module may be very slow,
        sometimes it takes 13 seconds to enumerate the usb device.
        So we wait for 20s.
    */
    sys_conf_count = get_module_count_from_sys_conf();
    do
    {
        count = init_usb_devices_info();
        if (count >= sys_conf_count)
            break;

        ret = sysinfo(&info);
        if (ret)
        {
            LOGE("sysinfo failed, ret=%d", ret);
        }
        else
        {
            uptime = info.uptime;
            LOGI("uptime=%ld", uptime);
            if (uptime > 15)
                break;
        }
        sleep(1);
    } while (i++ < 20);

    if (!count)
    {
        LOGE("can't get any usb device");
        property_set("sys.modem_conf.complete", "1");
        return 0;
    }

    LOGI("find %d usb devices, system config count is %d", count, sys_conf_count);

    i = 0;
    do
    {
        ret = upgrade_modules(&upgraded_count);
    } while(ret && i++ < 2);

    if (upgraded_count > 0)
        sleep(5);

    config_modules_net_mode(count, upgraded_count);

    /*
        Set setprop sys.modem_conf.complete = 1
        to start the services ec20_usb and compatible_4G.
    */
    property_set("sys.modem_conf.complete", "1");

    /*If the system has more than two modules, then start to configure another ecm module*/
    if (sys_conf_count > 1)
        start_ecm_manager();
 out:
    return ret;
}


