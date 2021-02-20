LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	main.c \
	atchannel.c \
	at_tok.c \
	device.c \
	misc.c \
	upgrade.c \
	usb.c \
	ecm_manager.c

LOCAL_CFLAGS += -O2 -DANDROID_PLATFORM
LOCAL_SHARED_LIBRARIES := libc libm libcutils libnetutils libnativehelper liblog

LOCAL_MODULE := modem_module_conf
LOCAL_MODULE_TAGS := optional
LOCAL_PROPRIETARY_MODULE := true
LOCAL_INIT_RC := modem_module_conf.rc
include $(BUILD_EXECUTABLE)
