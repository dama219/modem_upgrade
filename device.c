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
#include <stdbool.h>
#include "log.h"
#include "usb.h"
#include "device.h"
#include "atchannel.h"
#include "at_tok.h"

#ifndef FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   1
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif

#define PRODUCT_INFO_FILE "/private/modem/product_info"

struct device_config
{
    unsigned short vid;
    unsigned short pid;
    int speed;
    unsigned char databits;
    unsigned char stopbits;
    char parity;
    unsigned int rtscts;
};

static int speed_arr[] = {
    B0, B50, B75, B110, B134, B150, B200, B300, B600,
	B1200, B1800, B2400, B4800, B9600, B19200, B38400,
	B57600, B115200, B230400, B460800, B500000, B576000,
	B921600, B1000000, B1152000, B1500000, B2000000, B2500000,
	B3000000, B3500000, B4000000
};

static int name_arr[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600,
    1200, 1800, 2400, 4800, 9600, 19200, 38400,
    57600, 115200, 230400, 460800, 500000, 576000,
    921600, 1000000, 1152000, 1500000, 2000000,	2500000,
    3000000, 3500000, 4000000
};

static struct device_config dev_config[] =
{
    {0x2c7c, 0x0125, 115200, 8, 1, 'N', 0}, //EC20
};

static bool set_speed(int fd, int speed){
  int   i;
  int   status;
  struct termios   Opt;
  tcgetattr(fd, &Opt);
  for( i= 0;  i < ARRAY_SIZE(speed_arr);  i++) {
    if(speed == name_arr[i]) {
      tcflush(fd, TCIOFLUSH);
      cfsetispeed(&Opt, speed_arr[i]);
      cfsetospeed(&Opt, speed_arr[i]);
      status = tcsetattr(fd, TCSANOW, &Opt);
      if  (status != 0) {
        LOGE("tcsetattr fd1\n");
        return FALSE;
      }
      tcflush(fd,TCIOFLUSH);
      break;
    }
  }
  return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
static bool set_parity(int fd, unsigned char databits, unsigned char stopbits, char parity, int rtscts)
{
	struct termios options;

    tcflush(fd,TCIFLUSH);

	if(tcgetattr(fd, &options) !=  0) {
		return FALSE;
	}
	cfmakeraw(&options);

	switch (databits)
	{
	case 7:
		options.c_cflag |= CS7;
		break;
	case 8:
		options.c_cflag |= CS8;
		break;
	default:
		fprintf(stderr,"Unsupported data size\n"); return (FALSE);
	}

	switch (parity)
	{
		case 'n':
		case 'N':
			options.c_cflag &= ~PARENB;   /* Clear parity enable */
			options.c_iflag &= ~INPCK;     /* Enable parity checking */
			break;
		case 'o':
		case 'O':
			options.c_cflag |= (PARODD | PARENB); /*Odd parity*/
			options.c_iflag |= INPCK;             /* Disnable parity checking */
			break;
		case 'e':
		case 'E':
			options.c_cflag |= PARENB;     /* Enable parity */
			options.c_cflag &= ~PARODD;   /* Even parity*/
			options.c_iflag |= INPCK;       /* Disnable parity checking */
			break;
		case 'S':
		case 's':  /*as no parity*/
			options.c_cflag &= ~PARENB;
			options.c_cflag &= ~CSTOPB;
            break;
		default:
			fprintf(stderr,"Unsupported parity\n");
			return (FALSE);
		}

	switch (stopbits)
	{
		case 1:
			options.c_cflag &= ~CSTOPB;
			break;
		case 2:
			options.c_cflag |= CSTOPB;
		   break;
		default:
			 fprintf(stderr,"Unsupported stop bits\n");
			 return FALSE;
	}

    if(rtscts)
        options.c_cflag |= CRTSCTS;
    else
        options.c_cflag &= ~CRTSCTS;

	/* Set input parity option */
	if (parity != 'n')
		options.c_iflag |= INPCK;


	if (tcsetattr(fd, TCSANOW, &options) != 0)
	{
		printf("SetupSerial 3\n");
		return FALSE;
	}
    tcflush(fd,TCIOFLUSH);
	return TRUE;
}

int init_device(struct usb_device_info *device, TimeoutCallback timeout, ReaderClosedCallback closed)
{
    int i;
    int fd;
    int ret;
    char at_dev_path[256];
    ATResponse *p_response = NULL;

    for (i = 0; i < ARRAY_SIZE(dev_config); i++)
    {
        if (dev_config[i].vid == device->idVendor &&
            dev_config[i].pid == device->idProduct)
        {
            break;
        }
    }
    if (i == ARRAY_SIZE(dev_config))
    {
        LOGE("%s: no com config for device vid=0%x, pid=0x%x", __FUNCTION__,
            device->idVendor, device->idProduct);
        return -1;
    }

    if (!device->ttyAT_name[0])
    {
        LOGI("AT com is null");
        return -1;
    }

    sprintf(at_dev_path, "/dev/%s", device->ttyAT_name);
    LOGI("init AT com %s ...", at_dev_path);
    fd = open (at_dev_path, O_RDWR);
    if (fd < 0)
    {
        LOGE("open file %s failed, error=%d", at_dev_path, errno);
        return fd;
    }

    if (!set_parity (fd, dev_config[i].databits, dev_config[i].stopbits,
        dev_config[i].parity, dev_config[i].rtscts))
    {
        close(fd);
        LOGE("set parity failed");
        return -1;
    }

    if (!set_speed(fd, dev_config[i].speed))
    {
        close(fd);
        LOGE("set speed failed");
        return -1;
    }

    ret = at_open(fd, NULL);
    if (ret < 0)
    {
        LOGE("at_open faild");
        at_close(fd);
        return -1;
    }

    at_set_on_timeout(timeout);
    at_set_on_reader_closed(closed);

    ret = at_handshake();
    if (ret)
    {
        LOGE("at handshake failed, ret=%d", ret);
        at_close();
    }
    return ret;
}

int get_product_info_from_device(struct product_info *pro_info)
{
    int ret = 0;
    int i = 0, j = 0;
    ATResponse *p_response = NULL;
    int found = 0;

    if (pro_info == NULL)
        return -1;

    ret = at_send_command_multiline("ATI", "\0", &p_response);
    if (!ret && p_response && p_response->success)
    {
        ATLine *p_cur = p_response->p_intermediates;
        while (p_cur)
        {
            if (!strncmp(p_cur->line, "Revision: ", strlen("Revision: ")))
            {
                found = 1;
                break;
            }
            p_cur = p_cur->p_next;
            i++;
        }

        if (found && i >= 2)
        {
            p_cur = p_response->p_intermediates;
            while (p_cur)
            {
                if (j == (i-2))
                    strcpy(pro_info->vendor, p_cur->line);
                else if (j == (i-1))
                    strcpy(pro_info->dev_type, p_cur->line);
                else if (j == i)
                {
                    strcpy(pro_info->revision, p_cur->line + strlen("Revision: "));
                    break;
                }

                p_cur = p_cur->p_next;
                j++;
            }
        }
        else
            ret = -1;
    }
    else
        ret = -1;
    at_response_free(p_response);
    return ret;
}

void deinit_device(struct usb_device_info *device)
{
    at_close();
}

int save_product_info(struct product_info *pro_info)
{
    int fd;
    int ret = 0;

    fd = open(PRODUCT_INFO_FILE, O_WRONLY | O_CREAT, 0644);
    if (fd < 0)
    {
        LOGE("%s: open file %s failed, errno=%d", __FUNCTION__, PRODUCT_INFO_FILE, errno);
        return -1;
    }

    ret = write(fd, pro_info, sizeof(struct product_info));
    fsync(fd);
    close(fd);
    if (ret != sizeof(struct product_info))
    {
        LOGE("%s: write file %s failed, errno=%d", __FUNCTION__, PRODUCT_INFO_FILE, errno);
        return -1;
    }

    return 0;
}

int get_product_info_from_local(struct product_info *pro_info)
{
    int fd;
    int ret = 0;

    fd = open(PRODUCT_INFO_FILE, O_RDONLY);
    if (fd < 0)
    {
        LOGE("%s: open file %s failed, errno=%d", __FUNCTION__, PRODUCT_INFO_FILE, errno);
        return -1;
    }

    ret = read(fd, pro_info, sizeof(struct product_info));
    close(fd);
    if (ret != sizeof(struct product_info))
    {
        LOGE("%s: read file %s failed, errno=%d", __FUNCTION__, PRODUCT_INFO_FILE, errno);
        return -1;
    }

    return 0;
}

int get_sim_ICCID(char *iccid)
{
    ATResponse *p_response = NULL;
    char *line = NULL;
    int ret = 0;
    char *tmp = NULL;

    ret = at_send_command_singleline("AT+QCCID", "+QCCID:", &p_response);
    if (!ret && p_response && p_response->success)
    {
        line = p_response->p_intermediates->line;
		ret = at_tok_start(&line);
		if (!ret){
			ret = at_tok_nextstr(&line, &tmp);
            strcpy(iccid, tmp);
		}
    }
    else
        ret = -1;

    at_response_free(p_response);
    return ret;
}

int get_device_IMEI(char *imei)
{
    ATResponse *p_response = NULL;
    int ret = 0;

    ret = at_send_command_numeric("AT+CGSN", &p_response);
    if (!ret && p_response && p_response->success)
        strcpy(imei, p_response->p_intermediates->line);
    else
        ret = -1;

    at_response_free(p_response);
    return ret;
}

/** Returns SIM_NOT_READY on error */
sim_status get_sim_status()
{
    ATResponse *p_response = NULL;
    int err;
    int ret;
    char *cpinLine;
    char *cpinResult;

    err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);

    if (err != 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    switch (at_get_cme_error(p_response)) {
        case CME_SUCCESS:
            break;

        case CME_SIM_NOT_INSERTED:
            ret = SIM_ABSENT;
            goto done;

        default:
            ret = SIM_NOT_READY;
            goto done;
    }

    /* CPIN? has succeeded, now look at the result */

    cpinLine = p_response->p_intermediates->line;
    err = at_tok_start(&cpinLine);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    err = at_tok_nextstr(&cpinLine, &cpinResult);

    if (err < 0) {
        ret = SIM_NOT_READY;
        goto done;
    }

    if (0 == strcmp (cpinResult, "SIM PIN")) {
        ret = SIM_PIN;
        goto done;
    } else if (0 == strcmp (cpinResult, "SIM PUK")) {
        ret = SIM_PUK;
        goto done;
    } else if (0 == strcmp (cpinResult, "PH-NET PIN")) {
        return SIM_NETWORK_PERSONALIZATION;
    } else if (0 != strcmp (cpinResult, "READY"))  {
        /* we're treating unsupported lock types as "sim absent" */
        ret = SIM_ABSENT;
        goto done;
    } else
        ret = SIM_READY;

done:
    at_response_free(p_response);
    return ret;
}

net_mode get_net_mode()
{
    ATResponse *p_response = NULL;
    char *line = NULL;
    int ret = 0;
    int mode;

    ret = at_send_command_singleline("AT+QCFG=\"usbnet\"", "+QCFG:", &p_response);
    if (!ret && p_response && p_response->success)
    {
        line = p_response->p_intermediates->line;
        ret = at_tok_start(&line);
        if (ret)
            goto out;
        ret = skipComma(&line);
        if(ret)
            goto out;
        ret = at_tok_nextint(&line, &mode);
        if (ret)
            goto out;
        else
            return mode;
    }
    else
        ret = -1;
out:
    at_response_free(p_response);
    return ret;
}

int set_net_mode(net_mode mode)
{
    ATResponse *p_response = NULL;
    char cmd[256];
    int ret;

    sprintf(cmd, "AT+QCFG=\"usbnet\",%d", mode);
    ret = at_send_command(cmd, &p_response);
    if (!ret && p_response && p_response->success)
        ret = 0;
    else
        ret = -1;
    at_response_free(p_response);
    return ret;
}

int reset_device()
{
    ATResponse *p_response = NULL;
    int ret;

    ret = at_send_command("AT+CFUN=1,1", &p_response);
    if (!ret && p_response && p_response->success)
        ret = 0;
    else
        ret = -1;
    at_response_free(p_response);
    return ret;
}


int set_sim_detect(int enable, int level)
{
    ATResponse *p_response = NULL;
    int ret;
    char cmd[256];

    sprintf(cmd, "AT+QSIMDET=%d,%d", enable, level);
    ret = at_send_command(cmd, &p_response);
    if (!ret && p_response && p_response->success)
        ret = 0;
    else
        ret = -1;
    return ret;
}

int set_sim_status_report(int enable)
{
    ATResponse *p_response = NULL;
    int ret;
    char cmd[256];

    sprintf(cmd, "AT+QSIMSTAT=%d", enable);
    ret = at_send_command(cmd, &p_response);
    if (!ret && p_response && p_response->success)
        ret = 0;
    else
        ret = -1;
    return ret;
}
