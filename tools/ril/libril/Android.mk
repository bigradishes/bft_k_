# Copyright 2006 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    ril.cpp \
    ril_event.cpp

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libbinder \
    libcutils \
    libhardware_legacy

LOCAL_C_INCLUDES := \
    leadcore/hardware/ril/include


LOCAL_CFLAGS :=
LOCAL_CPPFLAGS :=

ZTE_RIL_EXTENDED_SUPPORT=true
ifeq ($(ZTE_RIL_EXTENDED_SUPPORT),true)
LOCAL_CFLAGS += -DZTE_RIL_EXTEND
LOCAL_CPPFLAGS += -DZTE_RIL_EXTEND
endif

LOCAL_MODULE:= libril

#LOCAL_LDLIBS += -lpthread

include $(BUILD_SHARED_LIBRARY)


# For RdoServD which needs a static library
# =========================================
ifneq ($(ANDROID_BIONIC_TRANSITION),)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
    ril.cpp

LOCAL_STATIC_LIBRARIES := \
    libutils_static \
    libcutils

LOCAL_CFLAGS :=

LOCAL_MODULE:= libril_static

#LOCAL_LDLIBS += -lpthread

include $(BUILD_STATIC_LIBRARY)
endif # ANDROID_BIONIC_TRANSITION
