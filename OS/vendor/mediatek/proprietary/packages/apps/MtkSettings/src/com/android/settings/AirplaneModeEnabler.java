/*
 * Copyright (C) 2007 The Android Open Source Project
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

package com.android.settings;

import android.app.ActivityManager;
import android.content.Context;
import android.content.Intent;
import android.database.ContentObserver;
import android.os.Handler;
import android.os.Message;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.provider.Settings;
import android.support.v14.preference.SwitchPreference;
import android.support.v7.preference.Preference;
import android.util.Log;

import com.android.internal.logging.nano.MetricsProto.MetricsEvent;
import com.android.internal.telephony.PhoneStateIntentReceiver;
import com.android.internal.telephony.TelephonyProperties;
import com.android.settings.core.instrumentation.MetricsFeatureProvider;
import com.android.settingslib.WirelessUtils;

public class AirplaneModeEnabler implements Preference.OnPreferenceChangeListener {

    private static final int EVENT_SERVICE_STATE_CHANGED = 3;

    private final Context mContext;
    private final SwitchPreference mSwitchPref;
    private final MetricsFeatureProvider mMetricsFeatureProvider;

    private PhoneStateIntentReceiver mPhoneStateReceiver;

    private Handler mHandler = new Handler() {
        @Override
        public void handleMessage(Message msg) {
            switch (msg.what) {
                case EVENT_SERVICE_STATE_CHANGED:
                    onAirplaneModeChanged();
                    break;
            }
        }
    };

    private ContentObserver mAirplaneModeObserver = new ContentObserver(new Handler()) {
        @Override
        public void onChange(boolean selfChange) {
            /// M: for ALPS02476322, if the same value it means set by self, no need to update
            if (mSwitchPref.isChecked() != WirelessUtils.isAirplaneModeOn(mContext)) {
                Log.d(TAG, "airplanemode changed by others, update UI...");
            onAirplaneModeChanged();
        }
        }
    };

    public AirplaneModeEnabler(Context context, SwitchPreference airplaneModeSwitchPreference,
            MetricsFeatureProvider metricsFeatureProvider) {

        mContext = context;
        mSwitchPref = airplaneModeSwitchPreference;
        mMetricsFeatureProvider = metricsFeatureProvider;

        airplaneModeSwitchPreference.setPersistent(false);

        mPhoneStateReceiver = new PhoneStateIntentReceiver(mContext, mHandler);
        mPhoneStateReceiver.notifyServiceState(EVENT_SERVICE_STATE_CHANGED);
    }

    public void resume() {

        mSwitchPref.setChecked(WirelessUtils.isAirplaneModeOn(mContext));

        mPhoneStateReceiver.registerIntent();
        mSwitchPref.setOnPreferenceChangeListener(this);
        mContext.getContentResolver().registerContentObserver(
                Settings.Global.getUriFor(Settings.Global.AIRPLANE_MODE_ON), true,
                mAirplaneModeObserver);
    }

    public void pause() {
        mPhoneStateReceiver.unregisterIntent();
        mSwitchPref.setOnPreferenceChangeListener(null);
        mContext.getContentResolver().unregisterContentObserver(mAirplaneModeObserver);
    }

    private void setAirplaneModeOn(boolean enabling) {
        // Change the system setting
        Settings.Global.putInt(mContext.getContentResolver(), Settings.Global.AIRPLANE_MODE_ON,
                enabling ? 1 : 0);
        // Update the UI to reflect system setting
        mSwitchPref.setChecked(enabling);

        // Post the intent
        Intent intent = new Intent(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        intent.putExtra("state", enabling);
        mContext.sendBroadcastAsUser(intent, UserHandle.ALL);
    }

    /**
     * Called when we've received confirmation that the airplane mode was set.
     * TODO: We update the checkbox summary when we get notified
     * that mobile radio is powered up/down. We should not have dependency
     * on one radio alone. We need to do the following:
     * - handle the case of wifi/bluetooth failures
     * - mobile does not send failure notification, fail on timeout.
     */
    private void onAirplaneModeChanged() {
        mSwitchPref.setChecked(WirelessUtils.isAirplaneModeOn(mContext));
    }

    /**
     * Called when someone clicks on the checkbox preference.
     */
    public boolean onPreferenceChange(Preference preference, Object newValue) {
        Log.d(TAG, "onPreferenceChange, newValue = " + newValue);
        /// M: need to check ECM for all SIMs, as the property may be like "true,false" @{
        /*
        if (Boolean.parseBoolean(
                SystemProperties.get(TelephonyProperties.PROPERTY_INECM_MODE))) {
        */
        String ecbMode = SystemProperties.get(TelephonyProperties.PROPERTY_INECM_MODE, "false");
        /// M: for 02894779, switch airplane mode anyway in guest mode
        boolean isGuestUser = UserManager.get(mContext).isGuestUser(
                ActivityManager.getCurrentUser());
        if (ecbMode != null && ecbMode.contains("true") && !isGuestUser) {
            Log.d(TAG, "ignore as ecbMode = " + newValue);
        /// @}
            // In ECM mode, do not update database at this point
        } else {
            Boolean value = (Boolean) newValue;
            mMetricsFeatureProvider.action(mContext, MetricsEvent.ACTION_AIRPLANE_TOGGLE, value);
            setAirplaneModeOn(value);
        }
        return true;
    }

    public void setAirplaneModeInECM(boolean isECMExit, boolean isAirplaneModeOn) {
        if (isECMExit) {
            // update database based on the current checkbox state
            setAirplaneModeOn(isAirplaneModeOn);
        } else {
            // update summary
            onAirplaneModeChanged();
        }
    }

    ///-------------------------------------------------MTK---------------------------------------
    private static final String TAG = "AirplaneModeEnabler";
}
