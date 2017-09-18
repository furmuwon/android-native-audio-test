ifeq ($(TARGET_ARCH),arm)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE:= native_audio
LOCAL_SRC_FILES := \
		   native_audio.cpp
LOCAL_SHARED_LIBRARIES :=  \
		libc \
        libcutils \
        libutils \
        libbinder \
        libhardware_legacy \
		libmedia
LOCAL_MODULE_TAGS := tests

include $(BUILD_EXECUTABLE)

endif
