/*
* Copyright (c) 2012 Intel Corporation.  All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/


#define LOG_NDEBUG 0
#define LOG_TAG "OMXVideoDecoderVP9HWR"
#include <utils/Log.h>
#include "OMXVideoDecoderVP9HWR.h"

#include <system/window.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <HardwareAPI.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>


static const char* VP9_MIME_TYPE = "video/x-vnd.on2.vp9";

static int GetCPUCoreCount() {
    int cpuCoreCount = 1;
#if defined(_SC_NPROCESSORS_ONLN)
    cpuCoreCount = sysconf(_SC_NPROCESSORS_ONLN);
#else
    // _SC_NPROC_ONLN must be defined...
    cpuCoreCount = sysconf(_SC_NPROC_ONLN);
#endif
    if (cpuCoreCount < 1) {
        ALOGW("Get CPU Core Count error.");
        cpuCoreCount = 1;
    } 
    ALOGV("Number of CPU cores: %d", cpuCoreCount);
    return cpuCoreCount;
}


OMXVideoDecoderVP9HWR::OMXVideoDecoderVP9HWR() {
    LOGV("OMXVideoDecoderVP9HWR is constructed.");

    mNumFrameBuffer = 0;
    mCtx = NULL;

    mNativeBufferCount = OUTPORT_NATIVE_BUFFER_COUNT;
    extUtilBufferCount = 0;
    extMappedNativeBufferCount = 0;
    BuildHandlerList();

#ifdef DECODE_WITH_GRALLOC_BUFFER
    // setup va
    VAStatus vaStatus = VA_STATUS_SUCCESS;
    mDisplay = new Display;
    *mDisplay = ANDROID_DISPLAY_HANDLE;

    mVADisplay = vaGetDisplay(mDisplay);
    if (mVADisplay == NULL) {
        LOGE("vaGetDisplay failed.");
    }

    int majorVersion, minorVersion;
    vaStatus = vaInitialize(mVADisplay, &majorVersion, &minorVersion);
    if (vaStatus != VA_STATUS_SUCCESS) {
        LOGE("vaInitialize failed.");
    } else {
        LOGV("va majorVersion=%d, minorVersion=%d", majorVersion, minorVersion);
    }
#endif

    // alloc mem info struct
    int i = 0;
    for (i=0; i<OUTPORT_NATIVE_BUFFER_COUNT; i++) {
        extMIDs[i] = (vaapiMemId*)malloc(sizeof(vaapiMemId));
        extMIDs[i]->m_usrAddr = NULL;
        extMIDs[i]->m_surface = new VASurfaceID;
    }

    initDecoder();
}

OMXVideoDecoderVP9HWR::~OMXVideoDecoderVP9HWR() {
    LOGV("OMXVideoDecoderVP9HWR is destructed.");

    vpx_codec_destroy((vpx_codec_ctx_t *)mCtx);
    delete (vpx_codec_ctx_t *)mCtx;
    mCtx = NULL;

    unsigned int i = 0;

#ifdef DECODE_WITH_GRALLOC_BUFFER
    for (i=0; i<mOMXBufferHeaderTypePtrNum; i++) {
        if (extMIDs[i]->m_surface != NULL) {
            vaDestroySurfaces(mVADisplay, extMIDs[i]->m_surface, 1);
        }
    }

    if (mVADisplay) {
        vaTerminate(mVADisplay);
        mVADisplay = NULL;
    }
#else
    for (i = 0; i < mOMXBufferHeaderTypePtrNum; i++ ) {
        if (extMIDs[i]->m_usrAddr != NULL) {
            free(extMIDs[i]->m_usrAddr);
            extMIDs[i]->m_usrAddr = NULL;
        }
    }
#endif
    for (i=0; i<OUTPORT_NATIVE_BUFFER_COUNT; i++) {
        delete extMIDs[i]->m_surface;
        free(extMIDs[i]);
    }
}


// Callback func for vpx decoder to get decode buffer
// Now we map from the vaSurface to deploy gralloc buffer
// as decode buffer
int getVP9FrameBuffer(void *user_priv,
                          unsigned int new_size,
                          vpx_codec_frame_buffer_t *fb)
{
    (void)user_priv;
    if (fb == NULL) {
        return -1; 
    }

    // TODO: Adaptive playback case needs to reconsider
    if (extNativeBufferSize < new_size) {
        LOGE("Provided frame buffer size < asking min size.");
        return -1;
    }

    int i;
    for (i = 0; i < extMappedNativeBufferCount; i++ ) {
        if ((extMIDs[i]->m_render_done == true) &&
            (extMIDs[i]->m_released == true)) {
            fb->data = extMIDs[i]->m_usrAddr;
            fb->size = extNativeBufferSize;
            fb->fb_stride = extNativeBufferStride;
            fb->fb_index = i;
            extMIDs[i]->m_released = false;
            break;
        }
    }

    if (i == extMappedNativeBufferCount) {
        LOGE("No available frame buffer in pool.");
        return -1;
    }
    return 0;
}

// call back function from libvpx to inform frame buffer
// not used anymore.
int releaseVP9FrameBuffer(void *user_priv,
                          vpx_codec_frame_buffer_t *fb)
{
    if (fb == NULL) {
        return -1; 
    }

    int i;
    for (i = 0; i < extMappedNativeBufferCount; i++ ) {
        if (fb->data == extMIDs[i]->m_usrAddr) {
            extMIDs[i]->m_released = true;
            break;
        }
    }

    if (i == extMappedNativeBufferCount) {
        LOGE("Not found matching frame buffer in pool, libvpx's wrong?");
        return -1;
    }
    return 0; 
}


OMX_ERRORTYPE OMXVideoDecoderVP9HWR::initDecoder()
{
    mCtx = new vpx_codec_ctx_t;    
    vpx_codec_err_t vpx_err;
    vpx_codec_dec_cfg_t cfg;
    memset(&cfg, 0, sizeof(vpx_codec_dec_cfg_t));
    cfg.threads = GetCPUCoreCount();
    if ((vpx_err = vpx_codec_dec_init(
                (vpx_codec_ctx_t *)mCtx,       
                 &vpx_codec_vp9_dx_algo,
                 &cfg, 0))) {
        LOGE("on2 decoder failed to initialize. (%d)", vpx_err);
        return OMX_ErrorNotReady;
    }

    mNumFrameBuffer = OUTPORT_NATIVE_BUFFER_COUNT;
        
    if (vpx_codec_set_frame_buffer_functions((vpx_codec_ctx_t *)mCtx,
                                    getVP9FrameBuffer,
                                    releaseVP9FrameBuffer,
                                    NULL)) {                       
      LOGE("Failed to configure external frame buffers");    
      return OMX_ErrorNotReady;
    }


    return OMX_ErrorNone;
}


OMX_ERRORTYPE OMXVideoDecoderVP9HWR::InitInputPortFormatSpecific(OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput) {
    // OMX_PARAM_PORTDEFINITIONTYPE
    paramPortDefinitionInput->nBufferCountActual = INPORT_ACTUAL_BUFFER_COUNT;
    paramPortDefinitionInput->nBufferCountMin = INPORT_MIN_BUFFER_COUNT;
    paramPortDefinitionInput->nBufferSize = INPORT_BUFFER_SIZE;
    paramPortDefinitionInput->format.video.cMIMEType = (OMX_STRING)VP9_MIME_TYPE;
    paramPortDefinitionInput->format.video.eCompressionFormat = OMX_VIDEO_CodingVP9;

/*  [Fixme] No need for now
    // OMX_VIDEO_PARAM_VP9TYPE
    memset(&mParamVp9, 0, sizeof(mParamVp9));
    SetTypeHeader(&mParamVp9, sizeof(mParamVp9));
    mParamVp9.nPortIndex = INPORT_INDEX;
    mParamVp9.eProfile = OMX_VIDEO_VP9ProfileMain;
    mParamVp9.eLevel = OMX_VIDEO_VP9Level_Version0;
*/
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorInit(void) {

#ifdef DECODE_WITH_GRALLOC_BUFFER
    unsigned int i = 0;

    if (mOMXBufferHeaderTypePtrNum > OUTPORT_NATIVE_BUFFER_COUNT) {
        LOGE("Actual OMX outport buffer header type num > %d", OUTPORT_NATIVE_BUFFER_COUNT);
        return OMX_ErrorOverflow;
    }

    for (i=0; i<mOMXBufferHeaderTypePtrNum; i++) {
        OMX_BUFFERHEADERTYPE *buf_hdr = mOMXBufferHeaderTypePtrArray[i];

        extMIDs[i]->m_key = (unsigned int)(buf_hdr->pBuffer);
        extMIDs[i]->m_render_done = false;
        extMIDs[i]->m_released = true;
        extNativeBufferSize = mGraphicBufferParam.graphicBufferStride *
                              mGraphicBufferParam.graphicBufferHeight * 1.5;
        extNativeBufferStride = mGraphicBufferParam.graphicBufferStride;


        VAStatus va_res; 
        unsigned int buffer;
        VASurfaceAttrib attribs[2];                   
        VASurfaceAttribExternalBuffers* surfExtBuf = new VASurfaceAttribExternalBuffers;
        int32_t format = VA_RT_FORMAT_YUV420;

        surfExtBuf->buffers= (unsigned long *)&buffer;
        surfExtBuf->num_buffers = 1;
        surfExtBuf->pixel_format = VA_FOURCC_NV12;
        surfExtBuf->width = mGraphicBufferParam.graphicBufferWidth;
        surfExtBuf->height = mGraphicBufferParam.graphicBufferHeight;
        surfExtBuf->data_size = mGraphicBufferParam.graphicBufferStride * mGraphicBufferParam.graphicBufferHeight * 1.5; 
        surfExtBuf->num_planes = 2; 
        surfExtBuf->pitches[0] = mGraphicBufferParam.graphicBufferStride;
        surfExtBuf->pitches[1] = mGraphicBufferParam.graphicBufferStride;
        surfExtBuf->pitches[2] = 0;
        surfExtBuf->pitches[3] = 0;
        surfExtBuf->offsets[0] = 0;
        surfExtBuf->offsets[1] = mGraphicBufferParam.graphicBufferStride * mGraphicBufferParam.graphicBufferHeight;
        surfExtBuf->offsets[2] = 0;
        surfExtBuf->offsets[3] = 0;
        //surfExtBuf->private_data = (void *)mConfigBuffer.nativeWindow;
        surfExtBuf->flags = VA_SURFACE_ATTRIB_MEM_TYPE_ANDROID_GRALLOC;

        surfExtBuf->buffers[0] = (unsigned int)buf_hdr->pBuffer;

        attribs[0].type = (VASurfaceAttribType)VASurfaceAttribMemoryType;
        attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[0].value.type = VAGenericValueTypeInteger;
        attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_ANDROID_GRALLOC;

        attribs[1].type = (VASurfaceAttribType)VASurfaceAttribExternalBufferDescriptor;
        attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
        attribs[1].value.type = VAGenericValueTypePointer;
        attribs[1].value.value.p = (void *)surfExtBuf;

        va_res = vaCreateSurfaces(
            mVADisplay,
            format,
            mGraphicBufferParam.graphicBufferWidth,
            mGraphicBufferParam.graphicBufferHeight,
            extMIDs[i]->m_surface,
            1,
            attribs,
            2);

        if (va_res != VA_STATUS_SUCCESS) {
            LOGE("Failed to create vaSurface!");
            return OMX_ErrorUndefined;
        }

        delete surfExtBuf;

        VAImage image;
        unsigned char* usrptr;

        va_res = vaDeriveImage(mVADisplay, *(extMIDs[i]->m_surface), &image);
        if (VA_STATUS_SUCCESS == va_res) {
            va_res = vaMapBuffer(mVADisplay, image.buf, (void **) &usrptr);
            if (VA_STATUS_SUCCESS == va_res) {
                extMIDs[i]->m_usrAddr = usrptr;
                vaUnmapBuffer(mVADisplay, image.buf);
            }
            vaDestroyImage(mVADisplay, image.image_id);
        }
        
        extMappedNativeBufferCount++;
    }
    return OMX_ErrorNone;
#else
    extNativeBufferSize = mGraphicBufferParam.graphicBufferStride *
                          mGraphicBufferParam.graphicBufferHeight * 1.5;
    extNativeBufferStride = mGraphicBufferParam.graphicBufferStride;

    unsigned int i = 0;
    for (i = 0; i < mOMXBufferHeaderTypePtrNum; i++ ) {
        OMX_BUFFERHEADERTYPE *buf_hdr = mOMXBufferHeaderTypePtrArray[i];

        extMIDs[i]->m_key = (unsigned int)(buf_hdr->pBuffer);
        extMIDs[i]->m_usrAddr = (unsigned char*)malloc(sizeof(unsigned char) * extNativeBufferSize);
        extMIDs[i]->m_render_done = true;
        extMIDs[i]->m_released = true;
    }
    extMappedNativeBufferCount = mOMXBufferHeaderTypePtrNum;
    return OMX_ErrorNone;
#endif
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorDeinit(void) {
    //return OMXVideoDecoderBase::ProcessorDeinit();
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorStop(void) {
    return OMXComponentCodecBase::ProcessorStop();
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorFlush(OMX_U32 portIndex) {
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorPreFillBuffer(OMX_BUFFERHEADERTYPE* buffer) {
    if (buffer->nOutputPortIndex == OUTPORT_INDEX){
        unsigned int handle = (unsigned int)buffer->pBuffer;
        unsigned int i = 0;

        for (i=0; i<mOMXBufferHeaderTypePtrNum; i++) {
            if (handle == extMIDs[i]->m_key) {
                extMIDs[i]->m_render_done = true;
                break;
            }
        }
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::ProcessorProcess(
        OMX_BUFFERHEADERTYPE ***pBuffers,
        buffer_retain_t *retains,
        OMX_U32 numberBuffers) 
{

    OMX_ERRORTYPE ret;
    OMX_BUFFERHEADERTYPE *inBuffer = *pBuffers[INPORT_INDEX];
    OMX_BUFFERHEADERTYPE *outBuffer = *pBuffers[OUTPORT_INDEX];


    if (inBuffer->pBuffer == NULL) {
        LOGE("Buffer to decode is empty.");
        return OMX_ErrorBadParameter;
    }

    if (inBuffer->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
        LOGI("Buffer has OMX_BUFFERFLAG_CODECCONFIG flag.");
    }   

    if (inBuffer->nFlags & OMX_BUFFERFLAG_DECODEONLY) {
        LOGW("Buffer has OMX_BUFFERFLAG_DECODEONLY flag.");
    }

    if (inBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
        if (inBuffer->nFilledLen == 0) {
            (*pBuffers[OUTPORT_INDEX])->nFilledLen = 0;
            (*pBuffers[OUTPORT_INDEX])->nFlags = OMX_BUFFERFLAG_EOS;
            return OMX_ErrorNone;
        }
    }

    if (vpx_codec_decode(
                (vpx_codec_ctx_t *)mCtx,       
                inBuffer->pBuffer + inBuffer->nOffset,
                inBuffer->nFilledLen,          
                NULL,
                0)) {
        LOGE("on2 decoder failed to decode frame.");
        return OMX_ErrorBadParameter;
    }

    ret = FillRenderBuffer(pBuffers[OUTPORT_INDEX], &retains[OUTPORT_INDEX], ((*pBuffers[INPORT_INDEX]))->nFlags);

    if (ret == OMX_ErrorNone) {
        (*pBuffers[OUTPORT_INDEX])->nTimeStamp = inBuffer->nTimeStamp;
    }

    bool inputEoS = ((*pBuffers[INPORT_INDEX])->nFlags & OMX_BUFFERFLAG_EOS);
    bool outputEoS = ((*pBuffers[OUTPORT_INDEX])->nFlags & OMX_BUFFERFLAG_EOS);
    // if output port is not eos, retain the input buffer until all the output buffers are drained.
    if (inputEoS && !outputEoS) {      
        retains[INPORT_INDEX] = BUFFER_RETAIN_GETAGAIN;
        // the input buffer is retained for draining purpose. Set nFilledLen to 0 so buffer will not be decoded again.
        (*pBuffers[INPORT_INDEX])->nFilledLen = 0;                                                                                           
    }  

    if (ret == OMX_ErrorNotReady) {    
        retains[OUTPORT_INDEX] = BUFFER_RETAIN_GETAGAIN;
        ret = OMX_ErrorNone;
    }  

    return ret;
}

static int ALIGN(int x, int y) {
    // y must be a power of 2.
    return (x + y - 1) & ~(y - 1); 
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::FillRenderBuffer(OMX_BUFFERHEADERTYPE **pBuffer,
                                                      buffer_retain_t *retain,
                                                      OMX_U32 inportBufferFlags)
{
    OMX_BUFFERHEADERTYPE *buffer = *pBuffer;
    OMX_BUFFERHEADERTYPE *buffer_orign = buffer;

    OMX_ERRORTYPE ret = OMX_ErrorNone;

    if (mWorkingMode != GRAPHICBUFFER_MODE) {
        LOGE("Working Mode is not GRAPHICBUFFER_MODE");
        ret = OMX_ErrorBadParameter;
    }

    vpx_codec_iter_t iter = NULL;
    vpx_image_t *img = NULL;
    img = vpx_codec_get_frame((vpx_codec_ctx_t *)mCtx, &iter);

#ifdef DECODE_WITH_GRALLOC_BUFFER
    if (NULL != img) {
        buffer = *pBuffer = mOMXBufferHeaderTypePtrArray[img->fb_index];

        if ((unsigned int)(buffer->pBuffer) != extMIDs[img->fb_index]->m_key) {
            LOGE("There is gralloc handle mismatching between pool and mOMXBufferHeaderTypePtrArray.");
            return OMX_ErrorNotReady;
        }

        extMIDs[img->fb_index]->m_render_done = false;

        buffer->nOffset = 0;
        size_t dst_y_size = mGraphicBufferParam.graphicBufferStride * mGraphicBufferParam.graphicBufferHeight;
        size_t dst_c_stride = ALIGN(mGraphicBufferParam.graphicBufferStride / 2, 16);
        size_t dst_c_size = dst_c_stride * mGraphicBufferParam.graphicBufferHeight / 2;
        buffer->nFilledLen = dst_y_size + dst_c_size * 2;
        if (inportBufferFlags & OMX_BUFFERFLAG_EOS) {
            buffer->nFlags = OMX_BUFFERFLAG_EOS;
        }

        if (buffer_orign != buffer) {
            *retain = BUFFER_RETAIN_OVERRIDDEN;
        }

        return OMX_ErrorNone;
    } else {
        LOGE("vpx_codec_get_frame return NULL.");
        return OMX_ErrorNotReady;
    }
#else
    if (img == NULL) {
        LOGE("vpx_codec_get_frame return NULL.");
        return OMX_ErrorNotReady;
    }

    buffer = *pBuffer = mOMXBufferHeaderTypePtrArray[img->fb_index];

    if ((unsigned int)(buffer->pBuffer) != extMIDs[img->fb_index]->m_key) {
        LOGE("There is gralloc handle mismatching between pool and mOMXBufferHeaderTypePtrArray.");
        return OMX_ErrorNotReady;
    }

    extMIDs[img->fb_index]->m_render_done = false;

    android::GraphicBufferMapper &mapper = android::GraphicBufferMapper::get();
    void *dst = NULL;

    android::Rect bounds((int32_t)mGraphicBufferParam.graphicBufferWidth,
                         (int32_t)mGraphicBufferParam.graphicBufferHeight);

    if (mapper.lock((buffer_handle_t)buffer->pBuffer,
                     GRALLOC_USAGE_SW_WRITE_OFTEN,
                     bounds,
                     &dst) != 0) {
        LOGE("Error when mapping GraphicBuffer");
        return OMX_ErrorNotReady;
    }
    uint8_t *dst_y = (uint8_t *)dst;
    const OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput 
                                  = this->ports[INPORT_INDEX]->GetPortDefinition();

    size_t inBufferWidth = paramPortDefinitionInput->format.video.nFrameWidth;
    size_t inBufferHeight = paramPortDefinitionInput->format.video.nFrameHeight;

    size_t dst_y_size = mGraphicBufferParam.graphicBufferStride * mGraphicBufferParam.graphicBufferHeight;
    size_t dst_c_stride = ALIGN(mGraphicBufferParam.graphicBufferStride / 2, 16);
    size_t dst_c_size = dst_c_stride * mGraphicBufferParam.graphicBufferHeight / 2;
    uint8_t *dst_v = dst_y + dst_y_size;
    uint8_t *dst_u = dst_v + dst_c_size;

    //test border
    dst_y += VPX_DECODE_BORDER * mGraphicBufferParam.graphicBufferStride + VPX_DECODE_BORDER; 
    dst_v += (VPX_DECODE_BORDER/2) * dst_c_stride + (VPX_DECODE_BORDER/2);
    dst_u += (VPX_DECODE_BORDER/2) * dst_c_stride + (VPX_DECODE_BORDER/2);

    const uint8_t *srcLine = (const uint8_t *)img->planes[PLANE_Y];

    for (size_t i = 0; i < img->d_h; ++i) {
        memcpy(dst_y, srcLine, img->d_w);

        srcLine += img->stride[PLANE_Y];
        dst_y += mGraphicBufferParam.graphicBufferStride;
    }

    srcLine = (const uint8_t *)img->planes[PLANE_U];
    for (size_t i = 0; i < img->d_h / 2; ++i) {
        memcpy(dst_u, srcLine, img->d_w / 2);

        srcLine += img->stride[PLANE_U];
        dst_u += dst_c_stride;
    }

    srcLine = (const uint8_t *)img->planes[PLANE_V];
    for (size_t i = 0; i < img->d_h / 2; ++i) {
        memcpy(dst_v, srcLine, img->d_w / 2);

        srcLine += img->stride[PLANE_V];
        dst_v += dst_c_stride;
    }

    buffer->nOffset = 0;
    buffer->nFilledLen = dst_y_size + dst_c_size * 2;
    if (inportBufferFlags & OMX_BUFFERFLAG_EOS) {
        buffer->nFlags = OMX_BUFFERFLAG_EOS;
    }

    if (buffer_orign != buffer) {
        *retain = BUFFER_RETAIN_OVERRIDDEN;
    }
    ret = OMX_ErrorNone;

    mapper.unlock((buffer_handle_t)buffer->pBuffer);
    return ret;

#endif
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::PrepareConfigBuffer(VideoConfigBuffer *p) {
    //return OMXVideoDecoderBase::PrepareConfigBuffer(p);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::PrepareDecodeBuffer(OMX_BUFFERHEADERTYPE *buffer, buffer_retain_t *retain, VideoDecodeBuffer *p) {
    //return OMXVideoDecoderBase::PrepareDecodeBuffer(buffer, retain, p);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::BuildHandlerList(void) {
    OMXVideoDecoderBase::BuildHandlerList();
    //AddHandler((OMX_INDEXTYPE)OMX_IndexParamVideoVp9, GetParamVideoVp9, SetParamVideoVp9);
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::GetParamVideoVp9(OMX_PTR pStructure) {
/*
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_VP9TYPE *p = (OMX_VIDEO_PARAM_VP9TYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, INPORT_INDEX);

    memcpy(p, &mParamVp9, sizeof(*p));
*/
    return OMX_ErrorNone;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::SetParamVideoVp9(OMX_PTR pStructure) {
/*
    OMX_ERRORTYPE ret;
    OMX_VIDEO_PARAM_VP9TYPE *p = (OMX_VIDEO_PARAM_VP9TYPE *)pStructure;
    CHECK_TYPE_HEADER(p);
    CHECK_PORT_INDEX(p, INPORT_INDEX);
    CHECK_SET_PARAM_STATE();

    memcpy(&mParamVp9, p, sizeof(mParamVp9));
*/
    return OMX_ErrorNone;
}

OMX_COLOR_FORMATTYPE OMXVideoDecoderVP9HWR::GetOutputColorFormat(int width, int height) {
    //return OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar; 
    return OMX_INTEL_COLOR_FormatHalYV12; //HAL_PIXEL_FORMAT_YV12;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::GetDecoderOutputCropSpecific(OMX_PTR pStructure) {
    //return OMX_ErrorFormatNotDetected;

    OMX_ERRORTYPE ret = OMX_ErrorNone;
    OMX_CONFIG_RECTTYPE *rectParams = (OMX_CONFIG_RECTTYPE *)pStructure;

    CHECK_TYPE_HEADER(rectParams);

    if (rectParams->nPortIndex != OUTPORT_INDEX) {
        return OMX_ErrorUndefined;
    }

    const OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput 
                                      = this->ports[INPORT_INDEX]->GetPortDefinition();

    rectParams->nLeft = VPX_DECODE_BORDER;
    rectParams->nTop = VPX_DECODE_BORDER;
    rectParams->nWidth = paramPortDefinitionInput->format.video.nFrameWidth;
    rectParams->nHeight = paramPortDefinitionInput->format.video.nFrameHeight;

    return ret;
}

OMX_ERRORTYPE OMXVideoDecoderVP9HWR::GetNativeBufferUsageSpecific(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    android::GetAndroidNativeBufferUsageParams *param = (android::GetAndroidNativeBufferUsageParams*)pStructure;
    CHECK_TYPE_HEADER(param);
    param->nUsage |= (GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_NEVER \
                     | GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_EXTERNAL_DISP);
    return OMX_ErrorNone;

}
OMX_ERRORTYPE OMXVideoDecoderVP9HWR::SetNativeBufferModeSpecific(OMX_PTR pStructure) {
    OMX_ERRORTYPE ret;
    EnableAndroidNativeBuffersParams *param = (EnableAndroidNativeBuffersParams*)pStructure;

    CHECK_TYPE_HEADER(param);
    CHECK_PORT_INDEX_RANGE(param);
    CHECK_SET_PARAM_STATE();

    if (!param->enable) {
        mWorkingMode = RAWDATA_MODE;
        return OMX_ErrorNone;
    }
    mWorkingMode = GRAPHICBUFFER_MODE;
    PortVideo *port = NULL;
    port = static_cast<PortVideo *>(this->ports[OUTPORT_INDEX]);

    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    memcpy(&port_def,port->GetPortDefinition(),sizeof(port_def));
    port_def.nBufferCountMin = 1;
    port_def.nBufferCountActual = mNativeBufferCount;
    port_def.format.video.cMIMEType = (OMX_STRING)VA_VED_RAW_MIME_TYPE;
    port_def.format.video.eColorFormat = OMX_INTEL_COLOR_FormatYUV420PackedSemiPlanar;

    // add borders for libvpx decode need.
    port_def.format.video.nFrameHeight += VPX_DECODE_BORDER * 2;
    port_def.format.video.nFrameWidth += VPX_DECODE_BORDER * 2;
    // make heigth 32bit align
    port_def.format.video.nFrameHeight = (port_def.format.video.nFrameHeight + 0x1f) & ~0x1f;
    port_def.format.video.eColorFormat = GetOutputColorFormat(
                        port_def.format.video.nFrameWidth,
                        port_def.format.video.nFrameHeight);
    port->SetPortDefinition(&port_def,true);

     return OMX_ErrorNone;
}


bool OMXVideoDecoderVP9HWR::IsAllBufferAvailable(void) {
    bool b = ComponentBase::IsAllBufferAvailable();
    if (b == false) {
        return false;
    }

    PortVideo *port = NULL;
    port = static_cast<PortVideo *>(this->ports[OUTPORT_INDEX]);
    const OMX_PARAM_PORTDEFINITIONTYPE* port_def = port->GetPortDefinition();
     // if output port is disabled, retain the input buffer
    if (!port_def->bEnabled) {
        return false;
    }

    unsigned int i = 0;
    int found = 0;
    for (i=0; i<mOMXBufferHeaderTypePtrNum; i++) {
        if ((extMIDs[i]->m_render_done == true) && (extMIDs[i]->m_released == true)) {
           found ++;
           if (found > 1) { //libvpx sometimes needs 2 buffer when calling decode once.
               return true;
           }
        }
    }
    b = false;

    return b;
}

DECLARE_OMX_COMPONENT("OMX.Intel.VideoDecoder.VP9.hwr", "video_decoder.vp9.hwr", OMXVideoDecoderVP9HWR);

