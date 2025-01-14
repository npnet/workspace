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
package com.android.settings.nfc;

import android.content.Context;
import android.database.ContentObserver;
import android.net.Uri;
import android.nfc.NfcAdapter;
import android.nfc.NfcManager;
import android.os.Handler;
import android.provider.Settings;
import android.support.v14.preference.SwitchPreference;
import android.support.v7.preference.Preference;
import android.support.v7.preference.PreferenceScreen;
import android.util.Log;

import com.android.settings.core.PreferenceControllerMixin;
import com.android.settingslib.core.AbstractPreferenceController;
import com.android.settingslib.core.lifecycle.LifecycleObserver;
import com.android.settingslib.RestrictedPreference;
import com.android.settingslib.core.lifecycle.events.OnPause;
import com.android.settingslib.core.lifecycle.events.OnResume;

import com.mediatek.settings.FeatureOption;

import java.util.List;

public class NfcPreferenceController extends AbstractPreferenceController
        implements PreferenceControllerMixin, LifecycleObserver, OnResume, OnPause {

    private static final String TAG = "NfcPreferenceController";
    public static final String KEY_TOGGLE_NFC = "toggle_nfc";
    public static final String KEY_ANDROID_BEAM_SETTINGS = "android_beam_settings";
    /// M: Add MTK nfc seting @{
    private static final String KEY_MTK_TOGGLE_NFC = "toggle_mtk_nfc";
    private static final String ACTION_MTK_NFC = "mediatek.settings.NFC_SETTINGS";
    Preference mMtkNfc;
    /// @}

    private NfcEnabler mNfcEnabler;
    private NfcAdapter mNfcAdapter;
    private int mAirplaneMode;
    private AirplaneModeObserver mAirplaneModeObserver;
    private SwitchPreference mNfcPreference;
    private RestrictedPreference mBeamPreference;

    public NfcPreferenceController(Context context) {
        super(context);
        mNfcAdapter = NfcAdapter.getDefaultAdapter(context);
    }

    @Override
    public void displayPreference(PreferenceScreen screen) {
        Log.d(TAG, "isAvailable()=" + isAvailable());
        if (!isAvailable()) {
            removePreference(screen, KEY_TOGGLE_NFC);
            removePreference(screen, KEY_ANDROID_BEAM_SETTINGS);
            /// M: Remove MTK NFC setting if NFC is unavailable
            removePreference(screen, KEY_MTK_TOGGLE_NFC);
            mNfcEnabler = null;
            return;
        }
        mNfcPreference = (SwitchPreference) screen.findPreference(KEY_TOGGLE_NFC);
        mBeamPreference = (RestrictedPreference) screen.findPreference(
                KEY_ANDROID_BEAM_SETTINGS);
        mNfcEnabler = new NfcEnabler(mContext, mNfcPreference, mBeamPreference);
        // Manually set dependencies for NFC when not toggleable.
        if (!isToggleableInAirplaneMode(mContext)) {
            mAirplaneModeObserver = new AirplaneModeObserver();
            updateNfcPreference();
        }

        if (isAvailable()) {
            /// M: Remove NFC duplicate items @{
            if (FeatureOption.MTK_NFC_ADDON_SUPPORT) {
                Log.d(TAG, "MTK NFC support");
                removePreference(screen, KEY_TOGGLE_NFC);
                removePreference(screen, KEY_ANDROID_BEAM_SETTINGS);
                mNfcEnabler = null;
            } else {
                Log.d(TAG, "MTK NFC not support");
                removePreference(screen, KEY_MTK_TOGGLE_NFC);
            }
            /// @}
        }
    }

    @Override
    public void updateNonIndexableKeys(List<String> keys) {
        final NfcManager manager = (NfcManager) mContext.getSystemService(Context.NFC_SERVICE);
        if (manager != null) {
            NfcAdapter adapter = manager.getDefaultAdapter();
            if (adapter == null) {
                keys.add(KEY_TOGGLE_NFC);
                keys.add(KEY_ANDROID_BEAM_SETTINGS);
                /// M: Remove MTK NFC setting
                keys.add(KEY_MTK_TOGGLE_NFC);
            } else {
                /// M: Remove NFC duplicate items @{
                if (FeatureOption.MTK_NFC_ADDON_SUPPORT) {
                    keys.add(KEY_TOGGLE_NFC);
                    keys.add(KEY_ANDROID_BEAM_SETTINGS);
                } else {
                    keys.add(KEY_MTK_TOGGLE_NFC);
                }
                /// @}
            }
        }
    }

    @Override
    public boolean isAvailable() {
        return mNfcAdapter != null;
    }

    @Override
    public String getPreferenceKey() {
        return null;
    }

    public void onResume() {
        if (mAirplaneModeObserver != null) {
            mAirplaneModeObserver.register();
        }
        if (mNfcEnabler != null) {
            mNfcEnabler.resume();
        }
    }

    @Override
    public void onPause() {
        if (mAirplaneModeObserver != null) {
            mAirplaneModeObserver.unregister();
        }
        if (mNfcEnabler != null) {
            mNfcEnabler.pause();
        }
    }

    private void updateNfcPreference() {
        final int airplaneMode = Settings.Global.getInt(
                mContext.getContentResolver(), Settings.Global.AIRPLANE_MODE_ON, mAirplaneMode);
        if (airplaneMode == mAirplaneMode) {
            return;
        }
        mAirplaneMode = airplaneMode;
        boolean toggleable = mAirplaneMode != 1;
        if (toggleable) {
            mNfcAdapter.enable();
        } else {
            mNfcAdapter.disable();
        }
        mNfcPreference.setEnabled(toggleable);
        mBeamPreference.setEnabled(toggleable);
        /// M: Manually set dependencies for NFC
        mMtkNfc.setEnabled(toggleable);
    }

    public static boolean isToggleableInAirplaneMode(Context context) {
        String toggleable = Settings.Global.getString(context.getContentResolver(),
                Settings.Global.AIRPLANE_MODE_TOGGLEABLE_RADIOS);
        return toggleable != null && toggleable.contains(Settings.Global.RADIO_NFC);
    }

    private final class AirplaneModeObserver extends ContentObserver {
        private final Uri AIRPLANE_MODE_URI =
                Settings.Global.getUriFor(Settings.Global.AIRPLANE_MODE_ON);

        private AirplaneModeObserver() {
            super(new Handler());
        }

        public void register() {
            mContext.getContentResolver().registerContentObserver(AIRPLANE_MODE_URI, false, this);
        }

        public void unregister() {
            mContext.getContentResolver().unregisterContentObserver(this);
        }

        @Override
        public void onChange(boolean selfChange, Uri uri) {
            super.onChange(selfChange, uri);
            updateNfcPreference();
        }
    }

}
