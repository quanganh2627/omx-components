/*
 * Copyright (C) 2012 Intel Corporation.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
//#define LOG_NDEBUG 0

#define LOG_TAG "MultiDisplay"

#include <utils/Log.h>
#include "VPPMds.h"


VPPMDSListener::VPPMDSListener(VPPWorker* vpp)
    : mVppState(false), mMode(0), mListenerId(-1), mMds(NULL), mVpp(vpp) {
    ALOGV("A new Mds listener is created");
}

status_t VPPMDSListener::init() {
    int32_t gListenerId = -1;

    sp<IServiceManager> sm = defaultServiceManager();
    if (sm == NULL) {
        ALOGE("%s: Fail to get service manager", __func__);
        return STATUS_ERROR;
    }

    mMds = interface_cast<IMDService>(sm->getService(String16(INTEL_MDS_SERVICE_NAME)));
    if (mMds == NULL) {
        ALOGE("%s: Failed to get MDS service", __func__);
        return STATUS_ERROR;
    }

    sp<IMultiDisplaySinkRegistrar> sinkRegistrar = NULL;
    if ((sinkRegistrar = mMds->getSinkRegistrar()) == NULL)
        return STATUS_ERROR;
    mListenerId = sinkRegistrar->registerListener(this,
            "VPPSetting", MDS_MSG_MODE_CHANGE);
    ALOGV("MDS listener ID %d", mListenerId);

    sp<IMultiDisplayInfoProvider> mdsInfoProvider = mMds->getInfoProvider();
    if (mdsInfoProvider == NULL) {
        ALOGE("%s: Failed to get info provider", __func__);
        return STATUS_ERROR;
    }
    mMode = mdsInfoProvider->getDisplayMode(false);
    ALOGI("%s: The initial display mode is set to %d", __func__, mMode);

    if (mVpp != NULL)
        mVpp->setDisplayMode(mMode);

    return STATUS_OK;
}

status_t VPPMDSListener::deInit() {
    if ((mListenerId != -1) && mMds != NULL) {
        sp<IMultiDisplaySinkRegistrar> sinkRegistrar = NULL;
        if ((sinkRegistrar = mMds->getSinkRegistrar()) == NULL) {
            ALOGE("Failed to get Mds Sink registrar");
            return STATUS_ERROR;
        }

        sinkRegistrar->unregisterListener(mListenerId);
        ALOGI("A Mds listener Id %d is distroyed", mListenerId);
        mListenerId = -1;
    }
    return STATUS_OK;
}

VPPMDSListener::~VPPMDSListener() {
    ALOGV("A Mds listener %p is distroyed", this);
    mMode = MDS_MODE_NONE;
    mVppState = false;
    mListenerId = -1;
    mMds = NULL;
    mVpp = NULL;
}

status_t VPPMDSListener::onMdsMessage(int msg, void* value, int size) {
    if ((msg & MDS_MSG_MODE_CHANGE) && (size == sizeof(int))) {
        mMode = *((int*)(value));
        ALOGI("%s: Display mode is set to %d", __func__, mMode);
        if (mVpp != NULL) {
            mVpp->setDisplayMode(mMode);
        }
    }

    return NO_ERROR;
}

int VPPMDSListener::getMode() {
    //LOGV("Mds mode 0x%x", mMode);
    return mMode;
}

bool VPPMDSListener::getVppState() {
    ALOGV("MDS Vpp state %d", mVppState);
    return mVppState;
}
