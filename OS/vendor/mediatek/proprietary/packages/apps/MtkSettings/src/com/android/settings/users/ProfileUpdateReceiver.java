/*
 * Copyright (C) 2012 The Android Open Source Project
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

package com.android.settings.users;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.os.SystemProperties;
import android.os.UserHandle;
import android.os.UserManager;
import android.util.Log;

import com.android.settings.Utils;


/**
 * Watches for changes to Me Profile in Contacts and writes the photo to the User Manager.
 */
public class ProfileUpdateReceiver extends BroadcastReceiver {

    private static final String KEY_PROFILE_NAME_COPIED_ONCE = "name_copied_once";

    @Override
    public void onReceive(final Context context, Intent intent) {
        Log.d("ProfileUpdateReceiver", "Profile photo changed, get the PROFILE_CHANGED receiver.");
        // Profile changed, lets get the photo and write to user manager
        new Thread() {
            public void run() {
                Utils.copyMeProfilePhoto(context, null);
                /// M: Fix ALPS01262605, Since the profile is not equal to userinfo,
                //we do not need to change the userinfo(nick name) while we modify the profile
                String isGms = SystemProperties.get("ro.com.google.gmsversion", null);
                if (isGms != null && !isGms.isEmpty()) {
                    copyProfileName(context);
                }
            }
        }.start();
    }

    static void copyProfileName(Context context) {
        SharedPreferences prefs = context.getSharedPreferences("profile", Context.MODE_PRIVATE);
        if (prefs.contains(KEY_PROFILE_NAME_COPIED_ONCE)) {
            return;
        }

        int userId = UserHandle.myUserId();
        UserManager um = (UserManager) context.getSystemService(Context.USER_SERVICE);
        String profileName = Utils.getMeProfileName(context, false /* partial name */);
        if (profileName != null && profileName.length() > 0) {
            um.setUserName(userId, profileName);
            // Flag that we've written the profile one time at least. No need to do it in the future.
            prefs.edit().putBoolean(KEY_PROFILE_NAME_COPIED_ONCE, true).commit();
        }
    }
}
