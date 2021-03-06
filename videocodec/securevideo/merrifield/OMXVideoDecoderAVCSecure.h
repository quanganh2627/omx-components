/*
* Copyright (c) 2009-2012 Intel Corporation.  All rights reserved.
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

#ifndef OMX_VIDEO_DECODER_AVC_SECURE_H_
#define OMX_VIDEO_DECODER_AVC_SECURE_H_


#include "OMXVideoDecoderBase.h"

class OMXVideoDecoderAVCSecure : public OMXVideoDecoderBase {
public:
    OMXVideoDecoderAVCSecure();
    virtual ~OMXVideoDecoderAVCSecure();

protected:
    virtual OMX_ERRORTYPE InitInputPortFormatSpecific(OMX_PARAM_PORTDEFINITIONTYPE *paramPortDefinitionInput);
    virtual OMX_ERRORTYPE ProcessorInit(void);
    virtual OMX_ERRORTYPE ProcessorDeinit(void);
    virtual OMX_ERRORTYPE ProcessorStart(void);
    virtual OMX_ERRORTYPE ProcessorStop(void);
    virtual OMX_ERRORTYPE ProcessorPause(void);
    virtual OMX_ERRORTYPE ProcessorResume(void);
    virtual OMX_ERRORTYPE ProcessorFlush(OMX_U32 portIndex);
    virtual OMX_ERRORTYPE ProcessorProcess(
            OMX_BUFFERHEADERTYPE ***pBuffers,
            buffer_retain_t *retains,
            OMX_U32 numberBuffers);

    virtual OMX_ERRORTYPE PrepareConfigBuffer(VideoConfigBuffer *p);
    virtual OMX_ERRORTYPE PrepareDecodeBuffer(OMX_BUFFERHEADERTYPE *buffer, buffer_retain_t *retain, VideoDecodeBuffer *p);

    virtual OMX_ERRORTYPE BuildHandlerList(void);
    virtual OMX_ERRORTYPE SetMaxOutputBufferCount(OMX_PARAM_PORTDEFINITIONTYPE *p);
    DECLARE_HANDLER(OMXVideoDecoderAVCSecure, ParamVideoAvc);
    DECLARE_HANDLER(OMXVideoDecoderAVCSecure, ParamVideoAVCProfileLevel);

private:
    static OMX_U8* MemAllocDataBuffer(OMX_U32 nSizeBytes, OMX_PTR pUserData);
    static void MemFreeDataBuffer(OMX_U8 *pBuffer, OMX_PTR pUserData);
    OMX_U8* MemAllocDataBuffer(OMX_U32 nSizeBytes);
    void  MemFreeDataBuffer(OMX_U8 *pBuffer);
    static void KeepAliveTimerCallback(sigval v);
    void KeepAliveTimerCallback();

    OMX_ERRORTYPE PrepareClassicWVDecodeBuffer(OMX_BUFFERHEADERTYPE *buffer, buffer_retain_t *retain, VideoDecodeBuffer *p);
    OMX_ERRORTYPE PrepareModularWVDecodeBuffer(OMX_BUFFERHEADERTYPE *buffer, buffer_retain_t *retain, VideoDecodeBuffer *p);
    OMX_ERRORTYPE PrepareMCastSinkWVDecodeBuffer(OMX_BUFFERHEADERTYPE *buffer, buffer_retain_t *retain, VideoDecodeBuffer *p);

private:
    enum {
        // OMX_PARAM_PORTDEFINITIONTYPE
        INPORT_MIN_BUFFER_COUNT = 1,
        INPORT_ACTUAL_BUFFER_COUNT = 5,

        // for OMX_VIDEO_PARAM_INTEL_AVC_DECODE_SETTINGS
        // default number of reference frame
        NUM_REFERENCE_FRAME = 4,

        // a typical value for 1080p clips
        OUTPORT_NATIVE_BUFFER_COUNT = 17,
    };

    OMX_VIDEO_PARAM_AVCTYPE mParamAvc;
    uint32_t mDrmScheme;

    // Keep track of number of allocated inport buffers
    uint32_t mNumInportBuffers ;

    timer_t mKeepAliveTimer;

    bool mSessionPaused;
};

#endif /* OMX_VIDEO_DECODER_AVC_SECURE_H_ */
