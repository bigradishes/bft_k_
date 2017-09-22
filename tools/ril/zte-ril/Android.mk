# Copyright 2006 The Android Open Source Project

#/******************************************************************************
#*(C) Copyright 2008 Marvell International Ltd.
#* All Rights Reserved
#******************************************************************************/

# XXX using libutils for simulator build only...
#
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    zte-ril.c \
    ril-cc.c \
    ril-mm.c \
    ril-ps.c \
    ril-ss.c \
    ril-msg.c \
    ril-sim.c \
    ril-dev.c \
    atchannel.c \
    dataapi.c \
    misc.c \
    work-queue.c \
    ril-requestdatahandler.c \
    at_tok.c

LOCAL_SHARED_LIBRARIES := \
	libcutils libutils libril libnetutils

# for asprinf
LOCAL_CFLAGS := -D_GNU_SOURCE 

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

ifeq ($(MARVELL_EXTENDED_RIL_REQUSTS),true)
LOCAL_CFLAGS += -DMARVELL_EXTENDED
endif


# Android added by GanHuiliang_2012-2-14 *** BEGIN
ZTE_RIL_EXTENDED_SUPPORT=true
ifeq ($(ZTE_RIL_EXTENDED_SUPPORT),true)
LOCAL_CFLAGS += -DZTE_RIL_EXTEND
LOCAL_CPPFLAGS += -DZTE_RIL_EXTEND
endif
# Android added by GanHuiliang_2012-2-14 END


LOCAL_C_INCLUDES := \
        leadcore/hardware/ril/include leadcore/hardware/ril/libril/

#build shared library
LOCAL_SHARED_LIBRARIES += \
	libcutils libutils
#LOCAL_LDLIBS += -lpthread
LOCAL_CFLAGS += -DRIL_SHLIB
LOCAL_MODULE:= libril-v7r1
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
