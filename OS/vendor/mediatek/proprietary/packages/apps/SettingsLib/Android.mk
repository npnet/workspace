LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_USE_AAPT2 := true

LOCAL_MODULE := MtkSettingsLib

LOCAL_SHARED_ANDROID_LIBRARIES := \
    android-support-annotations \
    android-support-v4 \
    android-support-v7-recyclerview \
    android-support-v7-preference \
    android-support-v7-appcompat \
    android-support-v14-preference

LOCAL_STATIC_JAVA_LIBRARY := legacy-android-test

# M: Add SettingsLib ext
LOCAL_STATIC_JAVA_LIBRARIES += com.mediatek.settingslib.ext

LOCAL_RESOURCE_DIR := $(LOCAL_PATH)/res
LOCAL_RESOURCE_DIR += $(LOCAL_PATH)/res_ext

LOCAL_JAR_EXCLUDE_FILES := none

LOCAL_SRC_FILES := $(call all-java-files-under, src)

include $(BUILD_STATIC_JAVA_LIBRARY)

# For the test package.
include $(call all-makefiles-under, $(LOCAL_PATH))
