/*
 * Copyright (C) 2014 The Android Open Source Project
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

package com.android.tv.mysettings.about;

import android.app.Fragment;

import com.android.tv.mysettings.BaseSettingsFragment;
import com.android.tv.mysettings.TvSettingsActivity;

/**
 * Activity which shows the build / model / legal info / etc.
 */
public class AboutActivity extends TvSettingsActivity {

    @Override
    protected Fragment createSettingsFragment() {
        //return SettingsFragment.newInstance();
        return AboutFragment.newInstance();
    }

    public static class SettingsFragment extends BaseSettingsFragment {

        public static SettingsFragment newInstance() {
            return new SettingsFragment();
        }

        @Override
        public void onPreferenceStartInitialScreen() {
            startPreferenceFragment(AboutFragment.newInstance());
        }
    }
}
