#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
#include "log.h"
#include "usb.h"
#include "upgrade.h"


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#define FIRMWARE_PATH "/system/etc/firmware/modem"
#define UPGRADE_LOG_PATH "/data/logs/modem"

struct upgrade_id
{
    char vendor[256];
    char dev_type[256];
    unsigned int prefix_count;
};

static struct upgrade_id upgrade_id_table[] = {
    {"Quectel", "EC20F", 10},//EC20CEHCLGR06A03M1G  or  EC20CEFHLGR06A06M1G
    {"Quectel", "EC25", 7},//EC25AFAR05A06M4G
};

static inline int get_compare_revision_count(struct product_info *pro_info)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(upgrade_id_table); i++) {
        if (!strcmp(pro_info->vendor, upgrade_id_table[i].vendor) &&
            !strcmp(pro_info->dev_type, upgrade_id_table[i].dev_type))
            return upgrade_id_table[i].prefix_count;
    }
    return -1;
}

static int ql_upgrade(struct usb_device_info *usb_info, char *file_name)
{
    int ret = 0;
    int status = 0;
    char command[256] = {0};
    char *exe= "/vendor/bin/qfirehose";

    if ((access(exe, F_OK)) < 0) {
        LOGE("no file %s", exe);
        return -1;
    }

    sprintf(command, "%s -s %s -f %s -l %s", exe, usb_info->usbdevice_path, file_name, UPGRADE_LOG_PATH);
    LOGI("%s: command= %s", __FUNCTION__, command);

    status = system(command);
    if (status == -1) {
        LOGI("system return -1");
        ret = -1;
    } else {
        if(WIFEXITED(status)) {//run command over
            if(WEXITSTATUS(status) == 0) {//command return 0
                ret = 0;
            } else {//command return other code
                LOGI("command exit code is %d",WEXITSTATUS(status));
                ret = -1;
            }
        } else {//can't run command
            LOGI("run command fail, exit status = %d\n",WEXITSTATUS(status));
            ret = -1;
        }
    }
    if (!ret)
        LOGI("upgrade success");
    else
        LOGI("upgrade failed");
    return ret;
}

static int get_firmware_file(struct product_info *pro_info, char *file_name)
{
    struct dirent* ent = NULL;
    DIR *pDir;
    int prefix;
    int found = 0;
    int len;

    if(!pro_info || !file_name)
        return -1;

    if ((prefix = get_compare_revision_count(pro_info)) < 0) {
        LOGE("Does not support upgrading this device");
        return -1;
    }

    if ((pDir = opendir(FIRMWARE_PATH)) == NULL)  {
        LOGE("Cannot open directory:%s", FIRMWARE_PATH);
        return -1;
    }

    while ((ent = readdir(pDir)) != NULL)  {
        if(ent->d_type & DT_DIR) {
            if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
        }

        if (!strncmp(ent->d_name, pro_info->revision, prefix)) {//same device type
            /*
                Maybe a beta version.
                The length of the beta version is longer.
                For example:
                (1)release version: EC20CEFHLGR06A06M1G
                (2)beta version: EC20CEFHLGR06A06M1G_BETA0703
            */
            if (strlen(ent->d_name) < strlen(pro_info->revision))
                len = strlen(ent->d_name);
            else
                len = strlen(pro_info->revision);

            //compare version
            if (strncmp(ent->d_name, pro_info->revision, len) > 0) {
                found = 1;
                break;
            } else if (!strncmp(ent->d_name, pro_info->revision, len) &&
                    strlen(pro_info->revision) > strlen(ent->d_name)) {
                /*Firmware in device is a beta version*/
                found = 1;
                break;
            }
        }
    }

    if (found) {
        sprintf(file_name, "%s/%s", FIRMWARE_PATH, ent->d_name);
        LOGI("find new version firmware file %s", file_name);
    } else
        LOGI("no new version firmware file found");

    closedir(pDir);
    return found;
}

/*
return value:
< 0 upgrade failed
==0 no need upgrade
1   upgrade success
*/
int upgrade_device(struct product_info *pro_info, struct usb_device_info *usb_info)
{
    int ret;
    char file_name[256];

    memset(file_name, 0x0, sizeof(file_name));
    ret = get_firmware_file(pro_info, file_name);
    if (ret <= 0)
        return UPGRADE_NONE;

    if (!strcmp(pro_info->vendor, "Quectel"))
    {
        LOGI("upgrade Quectel module...");
        ret = ql_upgrade(usb_info, file_name);

        if (!ret)
            ret = UPGRADE_SUCCESS;
        else
            ret = UPGRADE_FAILED;
    } else {
        LOGE("Don't support upgrade %s module", pro_info->vendor);
        ret = UPGRADE_NONE;
    }

    return ret;
}

