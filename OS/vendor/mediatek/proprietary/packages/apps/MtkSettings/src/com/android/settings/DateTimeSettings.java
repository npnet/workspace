/*
 * Copyright (C) 2008 The Android Open Source Project
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

import android.app.Activity;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.location.LocationManager;
import android.os.UserManager;
import android.provider.SearchIndexableResource;
import android.util.Log;

import com.android.internal.logging.nano.MetricsProto.MetricsEvent;
import com.android.settings.dashboard.DashboardFragment;
import com.android.settings.dashboard.SummaryLoader;
import com.android.settings.datetime.AutoTimePreferenceController;
import com.android.settings.datetime.AutoTimeZonePreferenceController;
import com.android.settings.datetime.DatePreferenceController;
import com.android.settings.datetime.TimeChangeListenerMixin;
import com.android.settings.datetime.TimeFormatPreferenceController;
import com.android.settings.datetime.TimePreferenceController;
import com.android.settings.datetime.TimeZonePreferenceController;
import com.android.settings.search.BaseSearchIndexProvider;
import com.android.settings.search.Indexable;
import com.android.settingslib.core.AbstractPreferenceController;
import com.android.settingslib.datetime.ZoneGetter;
import com.mediatek.settings.datetime.AutoTimeExtPreferenceController;

import java.util.ArrayList;
import java.util.Calendar;
import java.util.List;

public class DateTimeSettings extends DashboardFragment implements
        TimePreferenceController.TimePreferenceHost, DatePreferenceController.DatePreferenceHost,
        AutoTimeExtPreferenceController.GPSPreferenceHost, DialogInterface.OnCancelListener {

    private static final String TAG = "DateTimeSettings";

    // have we been launched from the setup wizard?
    protected static final String EXTRA_IS_FROM_SUW = "firstRun";

    /// M: add for auto GPS time
    private LocationManager mLocationManager = null;
    private boolean isGPSSupport;

    @Override
    public int getMetricsCategory() {
        return MetricsEvent.DATE_TIME;
    }

    @Override
    protected String getLogTag() {
        return TAG;
    }

    @Override
    protected int getPreferenceScreenResId() {
        Log.d(TAG, "getPreferenceScreenResId, isGPSSupport= " + isGPSSupport);
        /// M: Use RestrictedListPreference replace RestrictedSwitchPreference if GPS time support.
        if(isGPSSupport) {
            return R.xml.date_time_ext_prefs;
        } else {
            return R.xml.date_time_prefs;
        }
    }

    @Override
    public void onAttach(Context context) {
        /// M: add for auto GPS time @{
        mLocationManager = (LocationManager)getSystemService(Context.LOCATION_SERVICE);
        isGPSSupport = (mLocationManager.getProvider(LocationManager.GPS_PROVIDER) != null);
        /// @}
        super.onAttach(context);
        getLifecycle().addObserver(new TimeChangeListenerMixin(context, this));
    }

    @Override
    protected List<AbstractPreferenceController> getPreferenceControllers(Context context) {
        final List<AbstractPreferenceController> controllers = new ArrayList<>();
        final Activity activity = getActivity();
        final Intent intent = activity.getIntent();
        final boolean isFromSUW = intent.getBooleanExtra(EXTRA_IS_FROM_SUW, false);

        final AutoTimeZonePreferenceController autoTimeZonePreferenceController =
                new AutoTimeZonePreferenceController(
                        activity, this /* UpdateTimeAndDateCallback */, isFromSUW);
        /// M: add auto GPS time if it support, or use the default. @{
        final AutoTimePreferenceController autoTimePreferenceController;
        if(isGPSSupport) {
            autoTimePreferenceController = new AutoTimeExtPreferenceController(
                    activity, this /* UpdateTimeAndDateCallback */);
        } else {
            autoTimePreferenceController = new AutoTimePreferenceController(
                    activity, this /* UpdateTimeAndDateCallback */);
        }
        /// @}
        controllers.add(autoTimeZonePreferenceController);
        controllers.add(autoTimePreferenceController);

        controllers.add(new TimeFormatPreferenceController(
                activity, this /* UpdateTimeAndDateCallback */, isFromSUW));
        controllers.add(new TimeZonePreferenceController(
                activity, autoTimeZonePreferenceController));
        controllers.add(new TimePreferenceController(
                activity, this /* UpdateTimeAndDateCallback */, autoTimePreferenceController));
        controllers.add(new DatePreferenceController(
                activity, this /* UpdateTimeAndDateCallback */, autoTimePreferenceController));
        return controllers;
    }

    @Override
    public void updateTimeAndDateDisplay(Context context) {
        updatePreferenceStates();
    }

    @Override
    public Dialog onCreateDialog(int id) {
        switch (id) {
            case DatePreferenceController.DIALOG_DATEPICKER:
                return getPreferenceController(DatePreferenceController.class)
                        .buildDatePicker(getActivity());
            case TimePreferenceController.DIALOG_TIMEPICKER:
                return getPreferenceController(TimePreferenceController.class)
                        .buildTimePicker(getActivity());
            /// M: add for auto GPS time. @{
            case AutoTimeExtPreferenceController.DIALOG_GPS_CONFIRM:
                return getPreferenceController(AutoTimeExtPreferenceController.class)
                        .buildGPSConfirmDialog(getActivity());
            /// @}
            default:
                throw new IllegalArgumentException();
        }
    }

    @Override
    public int getDialogMetricsCategory(int dialogId) {
        switch (dialogId) {
            case DatePreferenceController.DIALOG_DATEPICKER:
                return MetricsEvent.DIALOG_DATE_PICKER;
            case TimePreferenceController.DIALOG_TIMEPICKER:
                return MetricsEvent.DIALOG_TIME_PICKER;
            /// M: add for auto GPS time. @{
            case AutoTimeExtPreferenceController.DIALOG_GPS_CONFIRM:
                return MetricsEvent.DATE_TIME;
            /// @}
            default:
                return 0;
        }
    }

    @Override
    public void showTimePicker() {
        removeDialog(TimePreferenceController.DIALOG_TIMEPICKER);
        showDialog(TimePreferenceController.DIALOG_TIMEPICKER);
    }

    @Override
    public void showDatePicker() {
        showDialog(DatePreferenceController.DIALOG_DATEPICKER);
    }

    /// M: add for auto GPS time @{
    @Override
    public void showGPSConfirmDialog() {
        removeDialog(AutoTimeExtPreferenceController.DIALOG_GPS_CONFIRM);
        showDialog(AutoTimeExtPreferenceController.DIALOG_GPS_CONFIRM);
        setOnCancelListener(this);
    }

    public void onCancel(DialogInterface arg0) {
        if(isGPSSupport) {
            Log.d(TAG, "onCancel Dialog, Reset AutoTime Settings");
            getPreferenceController(AutoTimeExtPreferenceController.class).reSetAutoTimePref();
        }
    }
    /// @}

    private static class SummaryProvider implements SummaryLoader.SummaryProvider {

        private final Context mContext;
        private final SummaryLoader mSummaryLoader;

        public SummaryProvider(Context context, SummaryLoader summaryLoader) {
            mContext = context;
            mSummaryLoader = summaryLoader;
        }

        @Override
        public void setListening(boolean listening) {
            if (listening) {
                final Calendar now = Calendar.getInstance();
                mSummaryLoader.setSummary(this, ZoneGetter.getTimeZoneOffsetAndName(mContext,
                        now.getTimeZone(), now.getTime()));
            }
        }
    }

    public static final SummaryLoader.SummaryProviderFactory SUMMARY_PROVIDER_FACTORY
            = new SummaryLoader.SummaryProviderFactory() {
        @Override
        public SummaryLoader.SummaryProvider createSummaryProvider(Activity activity,
                SummaryLoader summaryLoader) {
            return new SummaryProvider(activity, summaryLoader);
        }
    };


    public static final Indexable.SearchIndexProvider SEARCH_INDEX_DATA_PROVIDER =
            new DateTimeSearchIndexProvider();

    private static class DateTimeSearchIndexProvider extends BaseSearchIndexProvider {

        @Override
        public List<SearchIndexableResource> getXmlResourcesToIndex(
                Context context, boolean enabled) {
            List<SearchIndexableResource> result = new ArrayList<>();
            // Remove data/time settings from search in demo mode
            if (UserManager.isDeviceInDemoMode(context)) {
                return result;
            }

            SearchIndexableResource sir = new SearchIndexableResource(context);
            sir.xmlResId = R.xml.date_time_prefs;
            result.add(sir);

            return result;
        }
    }
}
