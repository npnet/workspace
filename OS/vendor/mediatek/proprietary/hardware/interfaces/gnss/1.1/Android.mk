# This file is autogenerated by hidl-gen. Do not edit manually.

LOCAL_PATH := $(call my-dir)

################################################################################

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.mediatek.hardware.gnss-V1.1-java
LOCAL_MODULE_CLASS := JAVA_LIBRARIES

intermediates := $(call local-generated-sources-dir, COMMON)

HIDL := $(HOST_OUT_EXECUTABLES)/hidl-gen$(HOST_EXECUTABLE_SUFFIX)

LOCAL_JAVA_LIBRARIES := \
    android.hardware.gnss-V1.0-java \
    android.hidl.base-V1.0-java \


#
# Build IMtkGnss.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IMtkGnss.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IMtkGnss.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IMtkGnss

$(GEN): $(LOCAL_PATH)/IMtkGnss.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build IVzwDebug.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IVzwDebug.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IVzwDebug

$(GEN): $(LOCAL_PATH)/IVzwDebug.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build IVzwDebugCallback.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IVzwDebugCallback.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IVzwDebugCallback

$(GEN): $(LOCAL_PATH)/IVzwDebugCallback.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build ISatelliteMode.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/ISatelliteMode.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::ISatelliteMode

$(GEN): $(LOCAL_PATH)/ISatelliteMode.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)
include $(BUILD_JAVA_LIBRARY)


################################################################################

include $(CLEAR_VARS)
LOCAL_MODULE := vendor.mediatek.hardware.gnss-V1.1-java-static
LOCAL_MODULE_CLASS := JAVA_LIBRARIES

intermediates := $(call local-generated-sources-dir, COMMON)

HIDL := $(HOST_OUT_EXECUTABLES)/hidl-gen$(HOST_EXECUTABLE_SUFFIX)

LOCAL_STATIC_JAVA_LIBRARIES := \
    android.hardware.gnss-V1.0-java-static \
    android.hidl.base-V1.0-java-static \


#
# Build IMtkGnss.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IMtkGnss.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IMtkGnss.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IMtkGnss

$(GEN): $(LOCAL_PATH)/IMtkGnss.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build IVzwDebug.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IVzwDebug.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IVzwDebug.hal
$(GEN): PRIVATE_DEPS += $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IVzwDebug

$(GEN): $(LOCAL_PATH)/IVzwDebug.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build IVzwDebugCallback.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/IVzwDebugCallback.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/IVzwDebugCallback.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::IVzwDebugCallback

$(GEN): $(LOCAL_PATH)/IVzwDebugCallback.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

#
# Build ISatelliteMode.hal
#
GEN := $(intermediates)/vendor/mediatek/hardware/gnss/V1_1/ISatelliteMode.java
$(GEN): $(HIDL)
$(GEN): PRIVATE_HIDL := $(HIDL)
$(GEN): PRIVATE_DEPS := $(LOCAL_PATH)/ISatelliteMode.hal
$(GEN): PRIVATE_OUTPUT_DIR := $(intermediates)
$(GEN): PRIVATE_CUSTOM_TOOL = \
        $(PRIVATE_HIDL) -o $(PRIVATE_OUTPUT_DIR) \
        -Ljava \
        -randroid.hardware:hardware/interfaces \
        -randroid.hidl:system/libhidl/transport \
        -rvendor.mediatek.hardware:vendor/mediatek/proprietary/hardware/interfaces \
        vendor.mediatek.hardware.gnss@1.1::ISatelliteMode

$(GEN): $(LOCAL_PATH)/ISatelliteMode.hal
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)
include $(BUILD_STATIC_JAVA_LIBRARY)



include $(call all-makefiles-under,$(LOCAL_PATH))
