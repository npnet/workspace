package com.mediatek.settings.ext;

import android.support.v7.preference.Preference;
import android.support.v7.preference.PreferenceScreen;

public class DefaultDeviceInfoSettingsExt implements IDeviceInfoSettingsExt {
    /**
     * CT E push feature refactory,add Epush in common feature
     * @param root The root PreferenceScreen which add e push entrance to
     * The preference screen
     */
    public void addEpushPreference(PreferenceScreen root){
    }

    public void updateSummary(Preference preference, String value, String defaultValue) {
    }
}