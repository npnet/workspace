/*
 * Copyright (C) 2011 The Android Open Source Project
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

package com.android.settings.location;

import static com.android.settingslib.RestrictedLockUtils.EnforcedAdmin;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.location.SettingInjectorService;
import android.os.Bundle;
import android.os.UserHandle;
import android.os.UserManager;
import android.support.v7.preference.Preference;
import android.support.v7.preference.PreferenceCategory;
import android.support.v7.preference.PreferenceGroup;
import android.support.v7.preference.PreferenceScreen;
import android.util.Log;
import android.widget.Switch;

import com.android.internal.logging.nano.MetricsProto.MetricsEvent;
import com.android.settings.DimmableIconPreference;
import com.android.settings.R;
import com.android.settings.SettingsActivity;
import com.android.settings.Utils;
import com.android.settings.applications.InstalledAppDetails;
import com.android.settings.dashboard.SummaryLoader;
import com.android.settings.widget.SwitchBar;
import com.android.settingslib.RestrictedLockUtils;
import com.android.settingslib.RestrictedSwitchPreference;
import com.android.settingslib.location.RecentLocationApps;
import android.os.Environment;

import com.mediatek.settings.UtilsExt;
import com.mediatek.settings.ext.ISettingsMiscExt;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import android.os.SystemProperties;

/**
 * System location settings (Settings &gt; Location). The screen has three parts:
 * <ul>
 *     <li>Platform location controls</li>
 *     <ul>
 *         <li>In switch bar: location master switch. Used to toggle
 *         {@link android.provider.Settings.Secure#LOCATION_MODE} between
 *         {@link android.provider.Settings.Secure#LOCATION_MODE_OFF} and another location mode.
 *         </li>
 *         <li>Mode preference: only available if the master switch is on, selects between
 *         {@link android.provider.Settings.Secure#LOCATION_MODE} of
 *         {@link android.provider.Settings.Secure#LOCATION_MODE_HIGH_ACCURACY},
 *         {@link android.provider.Settings.Secure#LOCATION_MODE_BATTERY_SAVING}, or
 *         {@link android.provider.Settings.Secure#LOCATION_MODE_SENSORS_ONLY}.</li>
 *     </ul>
 *     <li>Recent location requests: automatically populated by {@link RecentLocationApps}</li>
 *     <li>Location services: multi-app settings provided from outside the Android framework. Each
 *     is injected by a system-partition app via the {@link SettingInjectorService} API.</li>
 * </ul>
 * <p>
 * Note that as of KitKat, the {@link SettingInjectorService} is the preferred method for OEMs to
 * add their own settings to this page, rather than directly modifying the framework code. Among
 * other things, this simplifies integration with future changes to the default (AOSP)
 * implementation.
 */
public class LocationSettings extends LocationSettingsBase
        implements SwitchBar.OnSwitchChangeListener {

    private static final String TAG = "LocationSettings";

    /**
     * Key for managed profile location switch preference. Shown only
     * if there is a managed profile.
     */
    private static final String KEY_MANAGED_PROFILE_SWITCH = "managed_profile_location_switch";
    /** Key for preference screen "Mode" */
    private static final String KEY_LOCATION_MODE = "location_mode";
    private static final String KEY_SATELLITE_MODE = "satellite_mode";
    /** Key for preference category "Recent location requests" */
    private static final String KEY_RECENT_LOCATION_REQUESTS = "recent_location_requests";
    /** Key for preference category "Location services" */
    private static final String KEY_LOCATION_SERVICES = "location_services";

    private SwitchBar mSwitchBar;
    private Switch mSwitch;
    private boolean mValidListener = false;
    private UserHandle mManagedProfile;
    private RestrictedSwitchPreference mManagedProfileSwitch;
    private Preference mLocationMode,mSatelliteMode;
    private PreferenceCategory mCategoryRecentLocationRequests;
    /** Receives UPDATE_INTENT  */
    private BroadcastReceiver mReceiver;
    private SettingsInjector injector;
    private UserManager mUm;
    private ISettingsMiscExt mExt;

    @Override
    public int getMetricsCategory() {
        return MetricsEvent.LOCATION;
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        final SettingsActivity activity = (SettingsActivity) getActivity();
        mUm = (UserManager) activity.getSystemService(Context.USER_SERVICE);

        setHasOptionsMenu(true);
        mSwitchBar = activity.getSwitchBar();
        mSwitch = mSwitchBar.getSwitch();
        mSwitchBar.show();

        setHasOptionsMenu(true);
        mExt = UtilsExt.getMiscPlugin(activity);
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
        mSwitchBar.hide();
    }

    @Override
    public void onResume() {
        super.onResume();
        createPreferenceHierarchy();
        if (!mValidListener) {
            mSwitchBar.addOnSwitchChangeListener(this);
            mValidListener = true;
        }
    }

    @Override
    public void onPause() {
        try {
            getActivity().unregisterReceiver(mReceiver);
        } catch (RuntimeException e) {
            // Ignore exceptions caused by race condition
            if (Log.isLoggable(TAG, Log.VERBOSE)) {
                Log.v(TAG, "Swallowing " + e);
            }
        }
        if (mValidListener) {
            mSwitchBar.removeOnSwitchChangeListener(this);
            mValidListener = false;
        }
        super.onPause();
    }

    private void addPreferencesSorted(List<Preference> prefs, PreferenceGroup container) {
        // If there's some items to display, sort the items and add them to the container.
        Collections.sort(prefs, new Comparator<Preference>() {
            @Override
            public int compare(Preference lhs, Preference rhs) {
                return lhs.getTitle().toString().compareTo(rhs.getTitle().toString());
            }
        });
        for (Preference entry : prefs) {
            container.addPreference(entry);
        }
    }

    private PreferenceScreen createPreferenceHierarchy() {
        final SettingsActivity activity = (SettingsActivity) getActivity();
        PreferenceScreen root = getPreferenceScreen();
        if (root != null) {
            root.removeAll();
        }
        addPreferencesFromResource(R.xml.location_settings);
        root = getPreferenceScreen();

        setupManagedProfileCategory(root);
        mLocationMode = root.findPreference(KEY_LOCATION_MODE);
        mLocationMode.setOnPreferenceClickListener(
                new Preference.OnPreferenceClickListener() {
                    @Override
                    public boolean onPreferenceClick(Preference preference) {
                        activity.startPreferencePanel(
                                LocationSettings.this,
                                LocationMode.class.getName(), null,
                                R.string.location_mode_screen_title, null, LocationSettings.this,
                                0);
                        return true;
                    }
                });
        mSatelliteMode = root.findPreference(KEY_SATELLITE_MODE);
        mSatelliteMode.setOnPreferenceClickListener(
                new Preference.OnPreferenceClickListener() {
                    @Override
                    public boolean onPreferenceClick(Preference preference) {
                        activity.startPreferencePanel(
                        		LocationSettings.this,
                        		SatelliteMode.class.getName(), null,
                                R.string.location_satellite_mode, null, LocationSettings.this,
                                0);
                        return true;
                    }
                });
        mExt.initCustomizedLocationSettings(root, mLocationMode.getOrder() + 1);
        RecentLocationApps recentApps = new RecentLocationApps(activity);
        List<RecentLocationApps.Request> recentLocationRequests = recentApps.getAppList();

        final AppLocationPermissionPreferenceController preferenceController =
                new AppLocationPermissionPreferenceController(activity);
        preferenceController.displayPreference(root);

        mCategoryRecentLocationRequests =
                (PreferenceCategory) root.findPreference(KEY_RECENT_LOCATION_REQUESTS);

        List<Preference> recentLocationPrefs = new ArrayList<>(recentLocationRequests.size());
        for (final RecentLocationApps.Request request : recentLocationRequests) {
            DimmableIconPreference pref = new DimmableIconPreference(getPrefContext(),
                    request.contentDescription);
            pref.setIcon(request.icon);
            pref.setTitle(request.label);
            pref.setOnPreferenceClickListener(
                    new PackageEntryClickedListener(request.packageName, request.userHandle));
            recentLocationPrefs.add(pref);

        }
        if (recentLocationRequests.size() > 0) {
            addPreferencesSorted(recentLocationPrefs, mCategoryRecentLocationRequests);
        } else {
            // If there's no item to display, add a "No recent apps" item.
            Preference banner = new Preference(getPrefContext());
            banner.setLayoutResource(R.layout.location_list_no_item);
            banner.setTitle(R.string.location_no_recent_apps);
            banner.setSelectable(false);
            mCategoryRecentLocationRequests.addPreference(banner);
        }

        boolean lockdownOnLocationAccess = false;
        // Checking if device policy has put a location access lock-down on the managed
        // profile. If managed profile has lock-down on location access then its
        // injected location services must not be shown.
        if (mManagedProfile != null
                && mUm.hasUserRestriction(UserManager.DISALLOW_SHARE_LOCATION, mManagedProfile)) {
            lockdownOnLocationAccess = true;
        }
        addLocationServices(activity, root, lockdownOnLocationAccess);

        refreshLocationMode();
        return root;
    }

    private String getSatelliteMode(){
		String path = Environment.getDataDirectory()+"/misc/gps/mnl.prop";
		String content = "";
		File file = new File(path);
		Log.d("LocationSettings", "The File exist = " + file.exists());
		if (!file.exists())
		{
			content = "init.speed=115200\nlink.speed=115200\nGNSS_MODE=1\n\n";
			Log.d("LocationSettings", "content ="+content);
			return content;
		}
		else
		{
			try {
				InputStream instream = new FileInputStream(file);
				if (instream != null)
				{
					InputStreamReader inputreader = new InputStreamReader(instream);
					BufferedReader buffreader = new BufferedReader(inputreader);
					String line;
					while (( line = buffreader.readLine()) != null) {
						content += line + "\n";
					}
					instream.close();
				}
			} catch (java.io.FileNotFoundException e) {
				e.printStackTrace();
				Log.d("LocationSettings", "The File doesn't not exist.");
			} catch (IOException e) {
				e.printStackTrace();
				Log.d("LocationSettings", e.getMessage());
			}
		}
		Log.d("LocationSettings", "content ="+content);
		return content;
    }

    private void setupManagedProfileCategory(PreferenceScreen root) {
        // Looking for a managed profile. If there are no managed profiles then we are removing the
        // managed profile category.
        mManagedProfile = Utils.getManagedProfile(mUm);
        if (mManagedProfile == null) {
            // There is no managed profile
            root.removePreference(root.findPreference(KEY_MANAGED_PROFILE_SWITCH));
            mManagedProfileSwitch = null;
        } else {
            mManagedProfileSwitch = (RestrictedSwitchPreference)root
                    .findPreference(KEY_MANAGED_PROFILE_SWITCH);
            mManagedProfileSwitch.setOnPreferenceClickListener(null);
        }
    }

    private void changeManagedProfileLocationAccessStatus(boolean mainSwitchOn) {
        if (mManagedProfileSwitch == null) {
            return;
        }
        mManagedProfileSwitch.setOnPreferenceClickListener(null);
        final EnforcedAdmin admin = RestrictedLockUtils.checkIfRestrictionEnforced(getActivity(),
                UserManager.DISALLOW_SHARE_LOCATION, mManagedProfile.getIdentifier());
        final boolean isRestrictedByBase = isManagedProfileRestrictedByBase();
        if (!isRestrictedByBase && admin != null) {
            mManagedProfileSwitch.setDisabledByAdmin(admin);
            mManagedProfileSwitch.setChecked(false);
        } else {
            boolean enabled = mainSwitchOn;
            mManagedProfileSwitch.setEnabled(enabled);

            int summaryResId = R.string.switch_off_text;
            if (!enabled) {
                mManagedProfileSwitch.setChecked(false);
            } else {
                mManagedProfileSwitch.setChecked(!isRestrictedByBase);
                summaryResId = (isRestrictedByBase ?
                        R.string.switch_off_text : R.string.switch_on_text);
                mManagedProfileSwitch.setOnPreferenceClickListener(
                        mManagedProfileSwitchClickListener);
            }
            mManagedProfileSwitch.setSummary(summaryResId);
        }
    }

    /**
     * Add the settings injected by external apps into the "App Settings" category. Hides the
     * category if there are no injected settings.
     *
     * Reloads the settings whenever receives
     * {@link SettingInjectorService#ACTION_INJECTED_SETTING_CHANGED}.
     */
    private void addLocationServices(Context context, PreferenceScreen root,
            boolean lockdownOnLocationAccess) {
        PreferenceCategory categoryLocationServices =
                (PreferenceCategory) root.findPreference(KEY_LOCATION_SERVICES);
        injector = new SettingsInjector(context);
        // If location access is locked down by device policy then we only show injected settings
        // for the primary profile.
        final Context prefContext = categoryLocationServices.getContext();
        final List<Preference> locationServices = injector.getInjectedSettings(prefContext,
                lockdownOnLocationAccess ? UserHandle.myUserId() : UserHandle.USER_CURRENT);

        mReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                if (Log.isLoggable(TAG, Log.DEBUG)) {
                    Log.d(TAG, "Received settings change intent: " + intent);
                }
                injector.reloadStatusMessages();
            }
        };

        IntentFilter filter = new IntentFilter();
        filter.addAction(SettingInjectorService.ACTION_INJECTED_SETTING_CHANGED);
        context.registerReceiver(mReceiver, filter);

        if (locationServices.size() > 0) {
            addPreferencesSorted(locationServices, categoryLocationServices);
        } else {
            // If there's no item to display, remove the whole category.
            root.removePreference(categoryLocationServices);
        }
    }

    @Override
    public int getHelpResource() {
        return R.string.help_url_location_access;
    }

    @Override
    public void onModeChanged(int mode, boolean restricted) {
        int modeDescription = LocationPreferenceController.getLocationString(mode);
        if (modeDescription != 0) {
            mLocationMode.setSummary(modeDescription);
        }

        // Restricted user can't change the location mode, so disable the master switch. But in some
        // corner cases, the location might still be enabled. In such case the master switch should
        // be disabled but checked.
        final boolean enabled = (mode != android.provider.Settings.Secure.LOCATION_MODE_OFF);
        EnforcedAdmin admin = RestrictedLockUtils.checkIfRestrictionEnforced(getActivity(),
                UserManager.DISALLOW_SHARE_LOCATION, UserHandle.myUserId());
        boolean hasBaseUserRestriction = RestrictedLockUtils.hasBaseUserRestriction(getActivity(),
                UserManager.DISALLOW_SHARE_LOCATION, UserHandle.myUserId());
        // Disable the whole switch bar instead of the switch itself. If we disabled the switch
        // only, it would be re-enabled again if the switch bar is not disabled.
        if (!hasBaseUserRestriction && admin != null) {
            mSwitchBar.setDisabledByAdmin(admin);
        } else {
            mSwitchBar.setEnabled(!restricted);
        }
        mLocationMode.setEnabled(enabled && !restricted);
        mSatelliteMode.setEnabled(enabled && !restricted);
        mExt.updateCustomizedLocationSettings();
        mCategoryRecentLocationRequests.setEnabled(enabled);

        if (enabled != mSwitch.isChecked()) {
            // set listener to null so that that code below doesn't trigger onCheckedChanged()
            if (mValidListener) {
                mSwitchBar.removeOnSwitchChangeListener(this);
            }
            mSwitch.setChecked(enabled);
            if (mValidListener) {
                mSwitchBar.addOnSwitchChangeListener(this);
            }
        }

        changeManagedProfileLocationAccessStatus(enabled);

        // As a safety measure, also reloads on location mode change to ensure the settings are
        // up-to-date even if an affected app doesn't send the setting changed broadcast.
        injector.reloadStatusMessages();

		String s = SystemProperties.get("persist.sys.svmode", "0");
		Log.d("LocationSettings", "---->>>> String s=" + s);
		int svmode = 0;
		try {
			svmode = Integer.parseInt(s);
		} catch (Exception e) {
			e.printStackTrace();
			Log.d("LocationSettings", "---->>>> String e=" + e);
		}
		Log.d("LocationSettings", "---->>>> svmode=" + svmode);
		switch (svmode) {
			case 0:
				mSatelliteMode.setSummary(R.string.location_satellite_gps_beidou);
				break;
			case 1:
				mSatelliteMode.setSummary(R.string.location_satellite_gps);
				break;
			case 2:
				mSatelliteMode.setSummary(R.string.location_satellite_beidou);
				break;
			case 3:
				mSatelliteMode.setSummary(R.string.location_satellite_gps_glonass);
				break;
			case 4:
				mSatelliteMode.setSummary(R.string.location_satellite_glonass);
				break;
			default:
				mSatelliteMode.setSummary(R.string.location_satellite_off);
				break;
		}
    }

    /**
     * Listens to the state change of the location master switch.
     */
    @Override
    public void onSwitchChanged(Switch switchView, boolean isChecked) {
        if (isChecked) {
            setLocationMode(android.provider.Settings.Secure.LOCATION_MODE_PREVIOUS);
        } else {
            setLocationMode(android.provider.Settings.Secure.LOCATION_MODE_OFF);
        }
    }

    private boolean isManagedProfileRestrictedByBase() {
        if (mManagedProfile == null) {
            return false;
        }
        return mUm.hasBaseUserRestriction(UserManager.DISALLOW_SHARE_LOCATION, mManagedProfile);
    }

    private Preference.OnPreferenceClickListener mManagedProfileSwitchClickListener =
            new Preference.OnPreferenceClickListener() {
                @Override
                public boolean onPreferenceClick(Preference preference) {
                    final boolean switchState = mManagedProfileSwitch.isChecked();
                    mUm.setUserRestriction(UserManager.DISALLOW_SHARE_LOCATION,
                            !switchState, mManagedProfile);
                    mManagedProfileSwitch.setSummary(switchState ?
                            R.string.switch_on_text : R.string.switch_off_text);
                    return true;
                }
            };

    private class PackageEntryClickedListener
            implements Preference.OnPreferenceClickListener {
        private String mPackage;
        private UserHandle mUserHandle;

        public PackageEntryClickedListener(String packageName, UserHandle userHandle) {
            mPackage = packageName;
            mUserHandle = userHandle;
        }

        @Override
        public boolean onPreferenceClick(Preference preference) {
            // start new fragment to display extended information
            Bundle args = new Bundle();
            args.putString(InstalledAppDetails.ARG_PACKAGE_NAME, mPackage);
            ((SettingsActivity) getActivity()).startPreferencePanelAsUser(
                    LocationSettings.this,
                    InstalledAppDetails.class.getName(), args,
                    R.string.application_info_label, null, mUserHandle);
            return true;
        }
    }

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
                mSummaryLoader.setSummary(
                    this, LocationPreferenceController.getLocationSummary(mContext));
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
}
