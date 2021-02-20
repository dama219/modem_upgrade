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
#include <android/log.h>

#include "usb.h"

#define LOG_NDEBUG 0
#include "log.h"

#define USBID_LEN 4

static struct utsname utsname;  /* for the kernel version */
static int kernel_version;
#define KVERSION(j,n,p) ((j)*1000000 + (n)*1000 + (p))

struct usb_id_inf_struct {
    unsigned short vid;
    unsigned short pid;
    unsigned short at_inf;
    unsigned short ppp_inf;
};

struct usb_id_struct {
    unsigned short vid;
    unsigned short pid;
};

static const struct usb_id_inf_struct usb_id_inf_table[] = {
    {0x05c6, 0x9003, 2, 3}, //UC20
    {0x05c6, 0x9090, 2, 3}, //UC15
    {0x05c6, 0x9215, 2, 3}, //EC20
    {0x1519, 0x0331, 6, 0}, //UG95
    {0x1519, 0x0020, 6, 0}, //UG95
    {0x05c6, 0x9025, 2, 3}, //EC25
    {0x2c7c, 0x0125, 2, 3}, //EC25
    {0x2c7c, 0x0121, 2, 3}, //EC25
};

static const struct usb_id_struct usb_id_download_table[] = {
    {0x05c6, 0x9008},
};

static struct usb_device_info s_usb_device_info[MAX_CARD_NUM];
static unsigned int s_usb_device_count = 0;

static int is_usb_match(unsigned short vid, unsigned short pid) {
    size_t i;
    for (i = 0; i < ARRAY_SIZE(usb_id_inf_table); i++)
    {
        if (vid == usb_id_inf_table[i].vid)
        {
            if (pid == 0x0000) //donot check pid
                return 1;
            else if (pid == usb_id_inf_table[i].pid)
                return 1;
        }
    }

    for (i = 0; i < ARRAY_SIZE(usb_id_download_table); i++)
    {
        if (vid == usb_id_download_table[i].vid)
        {
            if (pid == 0x0000) //donot check pid
                return 1;
            else if (pid == usb_id_download_table[i].pid)
                return 1;
        }
    }
    return 0;
}

static int idusb2hex(char idusbinfo[USBID_LEN]) {
    int i;
    int value = 0;
    for (i = 0; i < USBID_LEN; i++) {
        if (idusbinfo[i] < 'a')
            value |= ((idusbinfo[i] - '0') << ((3 - i)*4));
        else
            value |= ((idusbinfo[i] - 'a' + 10) << ((3 - i)*4));
    }
    return value;
}

static int ql_find_usb_device(struct usb_device_info *pusb_device_info, int count)
{
    DIR *pDir;
    int fd;
    char filename[MAX_PATH];
    int find_usb_device = 0;
    struct stat statbuf;
    struct dirent* ent = NULL;
    int idVendor,idProduct;
    char dir[MAX_PATH] = {0};
    char link_content[MAX_PATH] = {0};

    strcat(dir, "/sys/bus/usb/devices");
    if ((pDir = opendir(dir)) == NULL)  {
        LOGE("Cannot open directory:%s/", dir);
        return -1;
    }
    while ((ent = readdir(pDir)) != NULL)  {
        memset(&pusb_device_info[find_usb_device], 0x0, sizeof(struct usb_device_info));
        sprintf(filename, "%s/%s", dir, ent->d_name);
        lstat(filename, &statbuf);
        if (S_ISLNK(statbuf.st_mode)) {
            char idusbinfo[USBID_LEN+1] = {0};
            idVendor = idProduct = 0x0000;
            sprintf(filename, "%s/%s/idVendor", dir, ent->d_name);

            if ((access(filename, F_OK)) < 0)
                continue;
            fd = open(filename, O_RDONLY);
            if (fd < 0) {
                LOGE("open file %s failed, erro=%d", filename, errno);
                break;
            }
            else {
                if (4 == read(fd, idusbinfo, USBID_LEN))
                    idVendor = idusb2hex(idusbinfo);
                close(fd);
            }

            if (!is_usb_match(idVendor, idProduct))
                continue;
            sprintf(filename, "%s/%s/idProduct", dir, ent->d_name);
            fd = open(filename, O_RDONLY);
            if (fd > 0) {
                if (4 == read(fd, idusbinfo, USBID_LEN))
                    idProduct = idusb2hex(idusbinfo);
                close(fd);
            }
            if (!is_usb_match(idVendor, idProduct))
                continue;

            sprintf(filename, "%s/%s", dir, ent->d_name);
            fd = readlink(filename, link_content, sizeof(link_content) - 1);
            if (fd < 0) {
                LOGE("read link %s failed, errno=%d", filename, errno);
            } else {
                char *str= strstr(link_content, "platform/");
                if (str != NULL) {
                    str += strlen("platform/");
                    char *str1 = strstr(str, "/");
                    if (str1 != NULL)
                        memcpy(pusb_device_info[find_usb_device].usb_root_name, str, str1 - str);
                }
            }
            strcpy(pusb_device_info[find_usb_device].usbdevice_path, filename);
            pusb_device_info[find_usb_device].idProduct = idProduct;
            pusb_device_info[find_usb_device].idVendor = idVendor;

            find_usb_device++;
            if (find_usb_device == count) {
                break;
            }
        }
    }
    closedir(pDir);
    return find_usb_device;
}

static int get_usb_inteface(int usb_interface, int idVendor, int idProduct)
{
    size_t i;
    if (usb_interface == USB_AT_INF)
        sleep(1); //wait load usb driver

    for (i = 0; i < ARRAY_SIZE(usb_id_inf_table); i++) {
        if ((idVendor == usb_id_inf_table[i].vid) && (idProduct == usb_id_inf_table[i].pid)) {
            if (usb_interface == USB_AT_INF) {
                usb_interface = usb_id_inf_table[i].at_inf;
                break;
            } else if (usb_interface == USB_PPP_INF) {
                usb_interface = usb_id_inf_table[i].ppp_inf;
                break;
            }
        }
    }

    if (i == ARRAY_SIZE(usb_id_inf_table))
        return -1;

    return usb_interface;
}

int is_usb_download_mode(unsigned short vid, unsigned short pid)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(usb_id_download_table); i++)
    {
        if (vid == usb_id_download_table[i].vid && pid == usb_id_download_table[i].pid)
            return 1;
    }

    return 0;
}
static char *get_out_ttyname(int usb_interface, struct usb_device_info *usb_device_info)
{
    DIR *pDir;
    struct dirent* ent = NULL;
    char usb_inf_path[MAX_PATH] = {0};
    char out_ttyname[32] = {0};
    int interface_no;

    if (!usb_device_info->usbdevice_path[0])
        return NULL;

    int idVendor = usb_device_info->idVendor;
    int idProduct = usb_device_info->idProduct;

    interface_no = get_usb_inteface(usb_interface, idVendor, idProduct);

    if(interface_no < 0){
        return NULL;
    }

    sprintf(usb_inf_path, "%s:1.%d", usb_device_info->usbdevice_path, interface_no);

    if ((pDir = opendir(usb_inf_path)) == NULL) {
        LOGE("Cannot open directory:%s/", usb_inf_path);
        return NULL;
    }

    while ((ent = readdir(pDir)) != NULL) {
        if (strncmp(ent->d_name, "tty", 3) == 0) {
            LOGD("find vid=0x%04x, pid=0x%04x, tty=%s", idVendor, idProduct, ent->d_name);
            strcpy(out_ttyname, ent->d_name);
            break;
        }
    }
    closedir(pDir);

    if (strcmp(out_ttyname, "tty") == 0) { //find tty not ttyUSBx or ttyACMx
        strcat(usb_inf_path, "/tty");
        if ((pDir = opendir(usb_inf_path)) == NULL)  {
            LOGE("Cannot open directory:%s/", usb_inf_path);
            return NULL;
        }

        while ((ent = readdir(pDir)) != NULL)  {
            if (strncmp(ent->d_name, "tty", 3) == 0) {
                LOGD("find vid=0x%04x, pid=0x%04x, tty=%s", idVendor, idProduct, ent->d_name);
                strcpy(out_ttyname, ent->d_name);
                break;
            }
        }
        closedir(pDir);
    }

    if (out_ttyname[0] == 0 && idVendor != usb_id_inf_table[3].vid) {
        if (access("/sys/bus/usb-serial/drivers/option1/new_id", W_OK) == 0) {
            char *cmd;
            LOGE("find usb serial option driver, but donot cantain quectel vid&pid");
            asprintf(&cmd, "echo 0x%x 0x%x > /sys/bus/usb-serial/drivers/option1/new_id", idVendor, idProduct);
            system(cmd);
            free(cmd);
        } else {
            LOGE("can not find usb serial option driver");
        }
    }

    if (out_ttyname[0]) {
        if (usb_interface == USB_AT_INF) {
            strcpy(usb_device_info->ttyAT_name, out_ttyname);
            return usb_device_info->ttyAT_name;
        }
        else if (usb_interface == USB_PPP_INF) {
            strcpy(usb_device_info->ttyndis_name, out_ttyname);
            return usb_device_info->ttyndis_name;
        }
    }

    return NULL;
}

/*return modules count*/
int init_usb_devices_info(void)
{
    struct dirent* ent = NULL;
    DIR *pDir;
    char dir[MAX_PATH], filename[MAX_PATH];
    int idVendor = 0, idProduct = 0;
    int i = 0;

    s_usb_device_count = 0;
    memset(s_usb_device_info, 0x0, MAX_CARD_NUM * sizeof(struct usb_device_info));

    s_usb_device_count = ql_find_usb_device(s_usb_device_info, MAX_CARD_NUM);

    if(!s_usb_device_count) {
        return 0;
    }

    for(i = 0; i < s_usb_device_count; i++) {
        if (!is_usb_download_mode(s_usb_device_info[i].idVendor, s_usb_device_info[i].idProduct)) {
            get_out_ttyname(USB_AT_INF, &s_usb_device_info[i]);
            get_out_ttyname(USB_PPP_INF, &s_usb_device_info[i]);
        }
    }

    return s_usb_device_count;
}

char *get_AT_tty_name_by_root(char *usb_root)
{
    int i;
    if (!s_usb_device_count)
    {
        LOGI("no usb devices find");
        return NULL;
    }

    if (usb_root == NULL || strlen(usb_root) == 0)
        return NULL;

    for (i = 0; i < s_usb_device_count; i++)
    {
        if (!strcmp(s_usb_device_info[i].usb_root_name, usb_root))
            return s_usb_device_info[i].usb_root_name;
    }
    return NULL;
}

void usb_set_autosuspend(int enable)
{
    int index = 0;
    do {
        if (s_usb_device_info[index].usbdevice_path[0]) {
            char shell_command[MAX_PATH+32];
            snprintf(shell_command, sizeof(shell_command), "echo %s > %s/power/control", enable ? "auto" : "on", s_usb_device_info[index].usbdevice_path);
            system(shell_command);
            LOGD("%s", shell_command);
            LOGD("%s %s", __func__, enable ? "auto" : "off");
        }

        index++;
    } while(index < MAX_CARD_NUM);
}

struct usb_device_info *get_usb_device_info(unsigned int index)
{
    if (!s_usb_device_count || index >= s_usb_device_count)
        return NULL;
    return &s_usb_device_info[index];
}

