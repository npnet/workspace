/*
 * Copyright (C) 2016 The Android Open Source Project
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
 */

#ifndef VENDOR_MEDIATEK_HARDWARE_GNSS_V1_1_MTKGNSS_H
#define VENDOR_MEDIATEK_HARDWARE_GNSS_V1_1_MTKGNSS_H

#include <AGnss.h>
#include <AGnssRil.h>
#include <GnssBatching.h>
#include <GnssConfiguration.h>
#include <GnssDebug.h>
#include <GnssGeofencing.h>
#include <GnssMeasurement.h>
#include <GnssNavigationMessage.h>
#include <GnssNi.h>
#include <GnssXtra.h>

#include <ThreadCreationWrapper.h>
//#include <android/hardware/gnss/1.0/IGnss.h>
#include <vendor/mediatek/hardware/gnss/1.1/IMtkGnss.h>
#include <hardware/fused_location.h>
#include <hardware/gps.h>
#include <hidl/Status.h>

#include <VzwDebug.h>
#include <SatelliteMode.h>
#include <semaphore.h>

namespace vendor {
namespace mediatek {
namespace hardware {
namespace gnss {
namespace V1_1 {
namespace implementation {

/// Mtk added to include more namespaces
using namespace android::hardware::gnss::V1_0;
using namespace android::hardware::gnss::V1_0::implementation;
using ::vendor::mediatek::hardware::gnss::V1_1::IMtkGnss;
using ::vendor::mediatek::hardware::gnss::V1_1::IVzwDebug;
using ::vendor::mediatek::hardware::gnss::V1_1::ISatelliteMode;
using ::android::wp;
/// Mtk add end
using ::android::hardware::hidl_death_recipient;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::hidl_vec;
using ::android::hardware::hidl_string;
using ::android::sp;

using LegacyGnssSystemInfo = ::GnssSystemInfo;

/*
 * Represents the standard GNSS interface. Also contains wrapper methods to allow methods from
 * IGnssCallback interface to be passed into the conventional implementation of the GNSS HAL.
 */
class MtkGnss : public IMtkGnss {
  public:
    MtkGnss(gps_device_t* gnss_device);
    ~MtkGnss();

    /*
     * Methods from ::android::hardware::gnss::V1_0::IGnss follow.
     * These declarations were generated from Gnss.hal.
     */
    Return<bool> setCallback(const sp<IGnssCallback>& callback)  override;
    Return<bool> start()  override;
    Return<bool> stop()  override;
    Return<void> cleanup()  override;
    Return<bool> injectLocation(double latitudeDegrees,
                                double longitudeDegrees,
                                float accuracyMeters)  override;
    Return<bool> injectTime(int64_t timeMs,
                            int64_t timeReferenceMs,
                            int32_t uncertaintyMs) override;
    Return<void> deleteAidingData(IGnss::GnssAidingData aidingDataFlags)  override;
    Return<bool> setPositionMode(IGnss::GnssPositionMode mode,
                                 IGnss::GnssPositionRecurrence recurrence,
                                 uint32_t minIntervalMs,
                                 uint32_t preferredAccuracyMeters,
                                 uint32_t preferredTimeMs)  override;
    Return<sp<IAGnssRil>> getExtensionAGnssRil() override;
    Return<sp<IGnssGeofencing>> getExtensionGnssGeofencing() override;
    Return<sp<IAGnss>> getExtensionAGnss() override;
    Return<sp<IGnssNi>> getExtensionGnssNi() override;
    Return<sp<IGnssMeasurement>> getExtensionGnssMeasurement() override;
    Return<sp<IGnssNavigationMessage>> getExtensionGnssNavigationMessage() override;
    Return<sp<IGnssXtra>> getExtensionXtra() override;
    Return<sp<IGnssConfiguration>> getExtensionGnssConfiguration() override;
    Return<sp<IGnssDebug>> getExtensionGnssDebug() override;
    Return<sp<IGnssBatching>> getExtensionGnssBatching() override;
    ///M: add to get and wrap hal vzw debug interface
    Return<sp<IVzwDebug>> getExtensionVzwDebug() override;
	Return<sp<ISatelliteMode>> getExtensionSatelliteMode() override;

    /*
     * Callback methods to be passed into the conventional GNSS HAL by the default
     * implementation. These methods are not part of the IGnss base class.
     */
    static void locationCb(GpsLocation* location);
    static void statusCb(GpsStatus* gnss_status);
    static void nmeaCb(GpsUtcTime timestamp, const char* nmea, int length);
    static void setCapabilitiesCb(uint32_t capabilities);
    static void acquireWakelockCb();
    static void releaseWakelockCb();
    static void requestUtcTimeCb();
    static pthread_t createThreadCb(const char* name, void (*start)(void*), void* arg);
    static void gnssSvStatusCb(GnssSvStatus* status);
    /*
     * Deprecated callback added for backward compatibility to devices that do
     * not support GnssSvStatus.
     */
    static void gpsSvStatusCb(GpsSvStatus* status);
    static void setSystemInfoCb(const LegacyGnssSystemInfo* info);

    /*
     * Wakelock consolidation, only needed for dual use of a gps.h & fused_location.h HAL
     *
     * Ensures that if the last call from either legacy .h was to acquire a wakelock, that a
     * wakelock is held.  Otherwise releases it.
     */
    static void acquireWakelockFused();
    static void releaseWakelockFused();

    /*
     * Holds function pointers to the callback methods.
     */
    static GpsCallbacks sGnssCb;

 private:
    /*
     * For handling system-server death while GNSS service lives on.
     */
    class GnssHidlDeathRecipient : public hidl_death_recipient {
      public:
        GnssHidlDeathRecipient(const sp<MtkGnss> gnss) : mGnss(gnss) {
        }

        virtual void serviceDied(uint64_t /*cookie*/,
                const wp<::android::hidl::base::V1_0::IBase>& /*who*/) {
            mGnss->handleHidlDeath();
        }
      private:
        sp<MtkGnss> mGnss;
    };

    // for wakelock consolidation, see above
    static void acquireWakelockGnss();
    static void releaseWakelockGnss();
    static void updateWakelock();
    static bool sWakelockHeldGnss;
    static bool sWakelockHeldFused;

    /*
     * Cleanup for death notification
     */
    void handleHidlDeath();

    sp<GnssXtra> mGnssXtraIface = nullptr;
    sp<AGnssRil> mGnssRil = nullptr;
    sp<GnssGeofencing> mGnssGeofencingIface = nullptr;
    sp<AGnss> mAGnssIface = nullptr;
    sp<GnssNi> mGnssNi = nullptr;
    /// Mtk added to specify namespace
    sp<implementation::GnssMeasurement> mGnssMeasurement = nullptr;
    sp<implementation::GnssNavigationMessage> mGnssNavigationMessage = nullptr;
    /// Mtk add end
    sp<GnssDebug> mGnssDebug = nullptr;
    sp<GnssConfiguration> mGnssConfig = nullptr;
    sp<GnssBatching> mGnssBatching = nullptr;

    ///M: add for vzw debug hidl imlementation instance
    sp<VzwDebug> mVzwDebug = nullptr;
	sp<SatelliteMode> mSatelliteMode = nullptr;	
    static sem_t sSem;

    sp<GnssHidlDeathRecipient> mDeathRecipient;

    const GpsInterface* mGnssIface = nullptr;
    static sp<IGnssCallback> sGnssCbIface;
    static std::vector<std::unique_ptr<ThreadFuncArgs>> sThreadFuncArgsList;
    static bool sInterfaceExists;

    // Values saved for resend
    static uint32_t sCapabilitiesCached;
    static uint16_t sYearOfHwCached;
};

extern "C" IMtkGnss* HIDL_FETCH_IMtkGnss(const char* name);

}  // namespace implementation
}  // namespace V1_1
}  // namespace gnss
}  // namespace hardware
}  // namespace mediatek
}  // namespace vendor

#endif  // VENDOR_MEDIATEK_HARDWARE_GNSS_V1_1_MTKGNSS_H
