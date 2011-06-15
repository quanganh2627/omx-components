ifeq ($(strip $(BOARD_USES_WRS_OMXIL_CORE)),true)
LOCAL_PATH := $(call my-dir)

VENDORS_INTEL_MRST_LIBMIX_ROOT := hardware/intel/PRIVATE/libmix

include $(CLEAR_VARS)

LOCAL_CPPFLAGS :=
LOCAL_LDFLAGS :=

LOCAL_SHARED_LIBRARIES := \
libwrs_omxil_common \
    libva_videodecoder \
    liblog

LOCAL_C_INCLUDES := \
    $(WRS_OMXIL_CORE_ROOT)/utils/inc \
    $(WRS_OMXIL_CORE_ROOT)/base/inc \
    $(WRS_OMXIL_CORE_ROOT)/core/inc/khronos/openmax/include \
    $(PV_INCLUDES) \
    $(VENDORS_INTEL_MRST_LIBMIX_ROOT)/videodecoder \
    $(TARGET_OUT_HEADERS)/libva

LOCAL_SRC_FILES := \
    OMXComponentCodecBase.cpp\
    OMXVideoDecoderBase.cpp\
    OMXVideoDecoderAVC.cpp

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libOMXVideoDecoderAVC
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_CPPFLAGS :=
LOCAL_LDFLAGS :=

LOCAL_SHARED_LIBRARIES := \
    libwrs_omxil_common \
    libva_videodecoder \
    liblog

LOCAL_C_INCLUDES := \
    $(WRS_OMXIL_CORE_ROOT)/utils/inc \
    $(WRS_OMXIL_CORE_ROOT)/base/inc \
    $(WRS_OMXIL_CORE_ROOT)/core/inc/khronos/openmax/include \
    $(PV_INCLUDES) \
    $(VENDORS_INTEL_MRST_LIBMIX_ROOT)/videodecoder \
    $(TARGET_OUT_HEADERS)/libva

LOCAL_SRC_FILES := \
    OMXComponentCodecBase.cpp\
    OMXVideoDecoderBase.cpp\
    OMXVideoDecoderMPEG4.cpp

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libOMXVideoDecoderMPEG4
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_CPPFLAGS :=
LOCAL_LDFLAGS :=

LOCAL_SHARED_LIBRARIES := \
    libwrs_omxil_common \
    libva_videodecoder \
    liblog

LOCAL_C_INCLUDES := \
    $(WRS_OMXIL_CORE_ROOT)/utils/inc \
    $(WRS_OMXIL_CORE_ROOT)/base/inc \
    $(WRS_OMXIL_CORE_ROOT)/core/inc/khronos/openmax/include \
    $(PV_INCLUDES) \
    $(VENDORS_INTEL_MRST_LIBMIX_ROOT)/videodecoder \
    $(TARGET_OUT_HEADERS)/libva

LOCAL_SRC_FILES := \
    OMXComponentCodecBase.cpp\
    OMXVideoDecoderBase.cpp\
    OMXVideoDecoderH263.cpp

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libOMXVideoDecoderH263
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)

LOCAL_CPPFLAGS :=
LOCAL_LDFLAGS :=

LOCAL_SHARED_LIBRARIES := \
    libwrs_omxil_common \
    libva_videodecoder \
    liblog

LOCAL_C_INCLUDES := \
    $(WRS_OMXIL_CORE_ROOT)/utils/inc \
    $(WRS_OMXIL_CORE_ROOT)/base/inc \
    $(WRS_OMXIL_CORE_ROOT)/core/inc/khronos/openmax/include \
    $(PV_INCLUDES) \
    $(VENDORS_INTEL_MRST_LIBMIX_ROOT)/videodecoder \
    $(TARGET_OUT_HEADERS)/libva

LOCAL_SRC_FILES := \
    OMXComponentCodecBase.cpp\
    OMXVideoDecoderBase.cpp\
    OMXVideoDecoderWMV.cpp

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libOMXVideoDecoderWMV
include $(BUILD_SHARED_LIBRARY)


endif
