/*
 * Copyright (C) 2015 The Android Open Source Project
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

package com.mediatek.settings;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.res.Configuration;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.provider.Settings;
import android.support.v4.view.ViewPager;
import android.support.v4.view.ViewPager.OnPageChangeListener;
import android.text.TextUtils;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnClickListener;
import android.view.ViewGroup;
import android.view.accessibility.AccessibilityEvent;
import android.widget.CheckBox;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;

import com.android.settings.R;
import com.android.settings.SettingsPreferenceFragment;
import com.android.settings.PreviewPagerAdapter;
import com.android.settings.widget.DotsPageIndicator;
import com.android.settings.widget.LabeledSeekBar;
import com.mediatek.settings.accessibility.CustomToggleFontSizePreferenceFragment;

import java.util.Locale;

/**
 * Preference fragment shows a preview and a seek bar to adjust a specific settings.
 */
public abstract class CustomPreviewSeekBarPreferenceFragment extends SettingsPreferenceFragment {

    /** List of entries corresponding the settings being set. */
    protected String[] mEntries;

    /** Index of the entry corresponding to initial value of the settings. */
    protected int mInitialIndex;

    /** Index of the entry corresponding to current value of the settings. */
    protected int mCurrentIndex;

    /** Resource id of the layout for this preference fragment. */
    protected int mActivityLayoutResId;

    /** Resource id of the layout that defines the contents inside preview screen. */
    protected int[] mPreviewSampleResIds;

    private ViewPager mPreviewPager;
    private PreviewPagerAdapter mPreviewPagerAdapter;
    private DotsPageIndicator mPageIndicator;

    private TextView mLabel;
    private View mLarger;
    private View mSmaller;
    private boolean mResponse;
    private int mOldProgress = 2;
    private CheckBox mDontShowAgain;
    private AlertDialog mDialog;
    private static final String PREFS_NAME = "check_box_pref";
    private static final String TAG = "CustomPreviewSeekBarPreferenceFragment";
    private class onPreviewSeekBarChangeListener implements OnSeekBarChangeListener {
    private boolean mSeekByTouch;
    private Context context = getPrefContext();
    private static final String KEY_CHECKBOX_DONOTSHOW = "skipMessage";

        @Override
        public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            SharedPreferences settings =
                    context.getSharedPreferences(PREFS_NAME, 0);
            String skipMessage = settings.getString(KEY_CHECKBOX_DONOTSHOW, "NOT checked");
            Log.d(TAG, "@M_onProgressChanged progress: " + progress + "fromUser" + fromUser);
            if (skipMessage.equals("NOT checked") && ((progress == 0) || (progress == 4))) {
                Activity activity = getActivity();
                showDialog(activity, seekBar, progress);
            } else {
               setPreviewLayer(progress, true);
               if (!mSeekByTouch) {
                  commit();
               }
            }
        }

          public String[] getFontEntries() {
                return context.getResources().getStringArray(R.array.custom_entries_font_size);
          }

          public String[] getFontEntryValues() {
                return context.getResources().getStringArray(R.array.custom_entryvalues_font_size);

          }
        private void showDialog(final Activity activity, final SeekBar seekBar, final int value) {
            final String checked = "checked";
            final String notChecked = "NOT checked";

            AlertDialog.Builder builder = new AlertDialog.Builder(context);
            //Log.d(TAG, "@M_ShowDialog");
            SharedPreferences settings =
                    context.getSharedPreferences(PREFS_NAME, 0);
            String skipMessage = settings.getString(KEY_CHECKBOX_DONOTSHOW, notChecked);
            Log.d(TAG, "@M_ShowDialog skip checkbox value from SharedPref is " + skipMessage);
            mDontShowAgain = new CheckBox(context);
            mDontShowAgain.setText(context.getString(R.string.do_not_show));
            float dpi = context.getResources().getDisplayMetrics().density;
            //if (mDontShowAgain == null) { Log.d(TAG, "@M_mDontShowAgain is null"); }
            builder.setView(mDontShowAgain,
                    (int)(19*dpi), (int)(5*dpi), (int)(14*dpi), (int)(5*dpi));
            if (value == 4) {
                builder.setMessage(context.getString(R.string.large_font_warning));
            } else {
                builder.setMessage(context.getString(R.string.small_font_warning));
            }
            builder.setTitle(context.getString(R.string.warning_dialog_title));
            builder.setOnCancelListener(new DialogInterface.OnCancelListener() {
                @Override
                public void onCancel(DialogInterface dialog) {
                    //Log.d(TAG, "@M_showDialogonCancel");
                    mResponse = false;
                    String checkBoxResult = notChecked;
                    if (mDontShowAgain.isChecked()) {
                        checkBoxResult = checked;
                    }
                    SharedPreferences settings =
                           context.getSharedPreferences(PREFS_NAME, 0);
                    SharedPreferences.Editor editor = settings.edit();
                    editor.putString(KEY_CHECKBOX_DONOTSHOW, checkBoxResult);
                    editor.commit();

                    final float currentScale = Settings.System
                            .getFloat(context.getContentResolver(),
                            Settings.System.FONT_SCALE, 1.0f);
                    mOldProgress = CustomToggleFontSizePreferenceFragment
                            .fontSizeValueToIndex(currentScale, getFontEntryValues());
                    Log.d(TAG, "@M_onCancel mOldProgress: " + mOldProgress);
                    seekBar.setProgress(mOldProgress);
                    //setPreviewLayer(mOldProgress, false);
                    Intent intent = activity.getIntent();
                    activity.finish();
                    activity.startActivity(intent);
                }
            });
            builder.setPositiveButton(context.getString(R.string.positive_button_title),
                    new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            //Log.d(TAG, "@M_PositiveButtonClick");
                            mResponse = true;
                             String checkBoxResult = notChecked;
                             if (mDontShowAgain.isChecked()) {
                                 checkBoxResult = checked;
                             }
                             SharedPreferences settings =
                             context.getSharedPreferences(PREFS_NAME, 0);
                                 SharedPreferences.Editor editor = settings.edit();
                                 editor.putString(KEY_CHECKBOX_DONOTSHOW, checkBoxResult);
                                 editor.commit();

                             commit();
                             final float currentScale =
                                     Settings.System.getFloat(context.getContentResolver(),
                                     Settings.System.FONT_SCALE, 1.0f);
                             mOldProgress =
                                     CustomToggleFontSizePreferenceFragment
                                     .fontSizeValueToIndex(currentScale, getFontEntryValues());
                                 Log.d(TAG, "@M_onPositiveClick mOldProgress: " + mOldProgress);
                             setPreviewLayer(value, true);
                                 if (!mSeekByTouch) {
                             commit();
                        }
                        }
                    });
            builder.setNegativeButton(context.getString(R.string.negative_button_title),
                    new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            //Log.d(TAG, "@M_NegativeButtonClick");
                            mResponse = false;
                            String checkBoxResult = notChecked;
                             if (mDontShowAgain.isChecked()) {
                                 checkBoxResult = checked;
                             }
                             SharedPreferences settings =
                             context.getSharedPreferences(PREFS_NAME, 0);
                                 SharedPreferences.Editor editor = settings.edit();
                                 editor.putString(KEY_CHECKBOX_DONOTSHOW, checkBoxResult);
                                 editor.commit();

                                     final float currentScale =
                                             Settings.System.getFloat(context.getContentResolver(),
                                     Settings.System.FONT_SCALE, 1.0f);
                             mOldProgress =
                                     CustomToggleFontSizePreferenceFragment
                                     .fontSizeValueToIndex(currentScale, getFontEntryValues());
                                     Log.d(TAG, "@M_onNegativeClick mOldProgress: " + mOldProgress);
                                     seekBar.setProgress(mOldProgress);
                             //setPreviewLayer(mOldProgress, false);
                             Intent intent = activity.getIntent();
                             activity.finish();
                             activity.startActivity(intent);
                        }
                    });
            if (!skipMessage.equals(checked) && mOldProgress != value) {
                mDialog = builder.show();
            } else {
                mResponse = true;
            }
        }


        @Override
        public void onStartTrackingTouch(SeekBar seekBar) {
            mSeekByTouch = true;
            //Log.d(TAG, "@M_onStartTrackingTouch");
        }

        @Override
        public void onStopTrackingTouch(SeekBar seekBar) {
            //Log.d(TAG, "@M_onStopTrackingTouch isAnimating: "
                //      + mPreviewPagerAdapter.isAnimating());
            if (mPreviewPagerAdapter.isAnimating()) {
            mPreviewPagerAdapter.setAnimationEndAction(new Runnable() {
            @Override
                    public void run() {
                        commit();
                    }
            });
            } else {
                commit();
            }
            mSeekByTouch = false;
        }
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container,
                             Bundle savedInstanceState) {
        final View root = super.onCreateView(inflater, container, savedInstanceState);
        final ViewGroup listContainer =
                (ViewGroup) root.findViewById(android.R.id.list_container);
        listContainer.removeAllViews();

        final View content = inflater.inflate(mActivityLayoutResId, listContainer, false);
        listContainer.addView(content);

        mLabel = (TextView) content.findViewById(R.id.current_label);

        // The maximum SeekBar value always needs to be non-zero. If there's
        // only one available value, we'll handle this by disabling the
        // seek bar.
        final int max = Math.max(1, mEntries.length - 1);

        final LabeledSeekBar seekBar = (LabeledSeekBar) content.findViewById(R.id.seek_bar);
        seekBar.setLabels(mEntries);
        seekBar.setMax(max);
        seekBar.setProgress(mInitialIndex);
        seekBar.setOnSeekBarChangeListener(new onPreviewSeekBarChangeListener());

        mSmaller = content.findViewById(R.id.smaller);
        mSmaller.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                final int progress = seekBar.getProgress();
                if (progress > 0) {
                    seekBar.setProgress(progress - 1, true);
                }
            }
        });

        mLarger = content.findViewById(R.id.larger);
        mLarger.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                final int progress = seekBar.getProgress();
                if (progress < seekBar.getMax()) {
                    seekBar.setProgress(progress + 1, true);
                }
            }
        });

        if (mEntries.length == 1) {
            // The larger and smaller buttons will be disabled when we call
            // setPreviewLayer() later in this method.
            seekBar.setEnabled(false);
        }

        final Context context = getContext();
        final Configuration origConfig = context.getResources().getConfiguration();
        final boolean isLayoutRtl = origConfig.getLayoutDirection() == View.LAYOUT_DIRECTION_RTL;
        Configuration[] configurations = new Configuration[mEntries.length];
        for (int i = 0; i < mEntries.length; ++i) {
            configurations[i] = createConfig(origConfig, i);
        }

        mPreviewPager = (ViewPager) content.findViewById(R.id.preview_pager);
        mPreviewPagerAdapter = new PreviewPagerAdapter(context, isLayoutRtl,
                mPreviewSampleResIds, configurations);
        mPreviewPager.setAdapter(mPreviewPagerAdapter);
        mPreviewPager.setCurrentItem(isLayoutRtl ? mPreviewSampleResIds.length - 1 : 0);
        mPreviewPager.addOnPageChangeListener(mPreviewPageChangeListener);

        mPageIndicator = (DotsPageIndicator) content.findViewById(R.id.page_indicator);
        if (mPreviewSampleResIds.length > 1) {
            mPageIndicator.setViewPager(mPreviewPager);
            mPageIndicator.setVisibility(View.VISIBLE);
            mPageIndicator.setOnPageChangeListener(mPageIndicatorPageChangeListener);
        } else {
            mPageIndicator.setVisibility(View.GONE);
        }

        setPreviewLayer(mInitialIndex, false);
        return root;
    }

    /**
     * Creates new configuration based on the current position of the SeekBar.
     */
    protected abstract Configuration createConfig(Configuration origConfig, int index);

    /**
     * Persists the selected value and sends a configuration change.
     */
    protected abstract void commit();

    private void setPreviewLayer(int index, boolean animate) {
        Log.d(TAG, "setPreviewLayer mCurrentIndex: " + mCurrentIndex + "newIndex" + index);
        mLabel.setText(mEntries[index]);
        mSmaller.setEnabled(index > 0);
        mLarger.setEnabled(index < mEntries.length - 1);
        setPagerIndicatorContentDescription(mPreviewPager.getCurrentItem());
        mPreviewPagerAdapter.setPreviewLayer(index, mCurrentIndex,
                mPreviewPager.getCurrentItem(), animate);

        mCurrentIndex = index;
    }

    private void setPagerIndicatorContentDescription(int position) {
        mPageIndicator.setContentDescription(
                getPrefContext().getString(R.string.preview_page_indicator_content_description,
                        position + 1, mPreviewSampleResIds.length));
    }

    private OnPageChangeListener mPreviewPageChangeListener = new OnPageChangeListener() {
        @Override
        public void onPageScrollStateChanged(int state) {
            // Do nothing.
        }

        @Override
        public void onPageScrolled(int position, float positionOffset,
                int positionOffsetPixels) {
            // Do nothing.
        }

        @Override
        public void onPageSelected(int position) {
            mPreviewPager.sendAccessibilityEvent(AccessibilityEvent.TYPE_ANNOUNCEMENT);
        }
    };

    private OnPageChangeListener mPageIndicatorPageChangeListener = new OnPageChangeListener() {
        @Override
        public void onPageScrollStateChanged(int state) {
            // Do nothing.
        }

        @Override
        public void onPageScrolled(int position, float positionOffset,
                int positionOffsetPixels) {
            // Do nothing.
        }

        @Override
        public void onPageSelected(int position) {
            setPagerIndicatorContentDescription(position);
        }
    };
}
