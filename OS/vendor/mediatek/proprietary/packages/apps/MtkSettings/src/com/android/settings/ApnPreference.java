/*
 * Copyright (C) 2009 The Android Open Source Project
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

import android.content.ContentUris;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.provider.Telephony;
import android.support.v7.preference.Preference;
import android.support.v7.preference.PreferenceViewHolder;
import android.telephony.SubscriptionManager;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.CompoundButton;
import android.widget.RadioButton;
import android.widget.RelativeLayout;

public class ApnPreference extends Preference implements
        CompoundButton.OnCheckedChangeListener, OnClickListener {
    final static String TAG = "ApnPreference";

    private int mSubId = SubscriptionManager.INVALID_SUBSCRIPTION_ID;

    public ApnPreference(Context context, AttributeSet attrs, int defStyle) {
        super(context, attrs, defStyle);
    }

    public ApnPreference(Context context, AttributeSet attrs) {
        this(context, attrs, R.attr.apnPreferenceStyle);
    }

    public ApnPreference(Context context) {
        this(context, null);
    }

    private static String mSelectedKey = null;
    private static CompoundButton mCurrentChecked = null;
    private boolean mProtectFromCheckedChange = false;
    private boolean mSelectable = true;

    @Override
    public void onBindViewHolder(PreferenceViewHolder view) {
        super.onBindViewHolder(view);

        View widget = view.findViewById(R.id.apn_radiobutton);
        if ((widget != null) && widget instanceof RadioButton) {
            RadioButton rb = (RadioButton) widget;
            if (mSelectable) {
                rb.setOnCheckedChangeListener(this);

                boolean isChecked = getKey().equals(mSelectedKey);
                if (isChecked) {
                    mCurrentChecked = rb;
                    mSelectedKey = getKey();
                }

                mProtectFromCheckedChange = true;
                rb.setChecked(isChecked);
                mProtectFromCheckedChange = false;
                rb.setVisibility(View.VISIBLE);
            } else {
                rb.setVisibility(View.GONE);
            }
        }

        View textLayout = view.findViewById(R.id.text_layout);
        if ((textLayout != null) && textLayout instanceof RelativeLayout) {
            textLayout.setOnClickListener(this);
        }
    }

    public boolean isChecked() {
        return getKey().equals(mSelectedKey);
    }

    public void setChecked() {
        mSelectedKey = getKey();
    }

    public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
        Log.i(TAG, "ID: " + getKey() + " :" + isChecked);
        if (mProtectFromCheckedChange) {
            return;
        }

        if (isChecked) {
            if (mCurrentChecked != null) {
                mCurrentChecked.setChecked(false);
            }
            mCurrentChecked = buttonView;
            mSelectedKey = getKey();
            callChangeListener(mSelectedKey);
        } else {
            mCurrentChecked = null;
            mSelectedKey = null;
        }
    }

    public void onClick(android.view.View v) {
        if ((v != null) && (R.id.text_layout == v.getId())) {
            Context context = getContext();
            if (context != null) {
                int pos = Integer.parseInt(getKey());
                Uri url = ContentUris.withAppendedId(Telephony.Carriers.CONTENT_URI, pos);
                /// M: for [Read Only APN]
                // context.startActivity(new Intent(Intent.ACTION_EDIT, url));
                Intent intent = new Intent(Intent.ACTION_EDIT, url);
                intent.putExtra("readOnly", !mEditable);
                intent.putExtra("sub_id", mSubId);
                context.startActivity(intent);
                /// @}
            }
        }
    }

    public void setSelectable(boolean selectable) {
        mSelectable = selectable;
    }

    public boolean getSelectable() {
        return mSelectable;
    }

    ///----------------------------------------MTK------------------------------------------------
    private boolean mEditable;

    public int getSubId() {
        return mSubId;
    }

    public void setSubId(int subId) {
        mSubId = subId;
    }

    /**
     * M: for [Read Only APN]
     * set whether the APN is editable, as some preset Apn can be be edited
     * @param isEditable
     */
    public void setApnEditable(boolean isEditable) {
        Log.d(TAG, "setApnEditable " + isEditable);
        mEditable = isEditable;
    }
}
