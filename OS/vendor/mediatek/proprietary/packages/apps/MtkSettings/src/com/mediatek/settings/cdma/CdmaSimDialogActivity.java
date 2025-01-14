
package com.mediatek.settings.cdma;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.StatusBarManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.DialogInterface.OnCancelListener;
import android.content.DialogInterface.OnClickListener;
import android.content.DialogInterface.OnDismissListener;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Bundle;
import android.telecom.PhoneAccountHandle;
import android.telephony.SubscriptionInfo;
import android.telephony.SubscriptionManager;
import android.util.Log;
import android.view.KeyEvent;
import android.widget.Toast;

import com.android.settings.R;
import com.android.settings.sim.SimDialogActivity;
import com.mediatek.settings.sim.SimHotSwapHandler;
import com.mediatek.settings.sim.SimHotSwapHandler.OnSimHotSwapListener;


/**
 * To show a dialog if two CDMA cards inserted.
 */
public class CdmaSimDialogActivity extends Activity {

    private static final String TAG = "CdmaSimDialogActivity";
    public final static String DIALOG_TYPE_KEY = "dialog_type";
    public final static String TARGET_SUBID_KEY = "target_subid";
    public final static String ACTION_TYPE_KEY = "action_type";
    public static final int TWO_CDMA_CARD = 0;
    public static final int ALERT_CDMA_CARD = 1;
    // for [C2K OMH Warning]
    public static final int ALERT_OMH_WARNING = 2;
    public static final int ALERT_OMH_DATA_PICK = 3;
    private StatusBarManager mStatusBarManager;

    public static final int INVALID_PICK = -1;

    private int mTargetSubId = SubscriptionManager.INVALID_SUBSCRIPTION_ID;
    private int mActionType = SimDialogActivity.INVALID_PICK;
    private PhoneAccountHandle mHandle = null;
    private SimHotSwapHandler mSimHotSwapHandler;
    private IntentFilter mIntentFilter;
    private int mDialogType = -1;
    private Dialog mDialog;

    // Receiver to handle different actions
    private BroadcastReceiver mSubReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d(TAG, "mSubReceiver action = " + action);
            finish();
        }
    };

    private void init() {
        /// M: for [SIM Hot Swap] @{
        mSimHotSwapHandler = new SimHotSwapHandler(getApplicationContext());
        mSimHotSwapHandler.registerOnSimHotSwap(new OnSimHotSwapListener() {
            @Override
            public void onSimHotSwap() {
                Log.d(TAG, "onSimHotSwap, finish Activity~~");
                finish();
            }
        });
        /// @};
        mIntentFilter = new IntentFilter(Intent.ACTION_AIRPLANE_MODE_CHANGED);
        registerReceiver(mSubReceiver, mIntentFilter);

        mStatusBarManager = (StatusBarManager) getSystemService(Context.STATUS_BAR_SERVICE);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "onCreate");
        super.onCreate(savedInstanceState);
        final Bundle extras = getIntent().getExtras();
        init();
        if (extras != null) {
            final int dialogType = extras.getInt(DIALOG_TYPE_KEY, INVALID_PICK);
            mTargetSubId = extras.
                    getInt(TARGET_SUBID_KEY, SubscriptionManager.INVALID_SUBSCRIPTION_ID);
            mActionType = extras.getInt(ACTION_TYPE_KEY, SimDialogActivity.INVALID_PICK);
            mDialogType = dialogType;
            Log.d(TAG, "dialogType: " + dialogType + " targetSubId: " + mTargetSubId
                    + " actionType: " + mActionType);
            switch (dialogType) {
                case TWO_CDMA_CARD:
                    displayDualCdmaDialog();
                    break;
                case ALERT_CDMA_CARD:
                    displayAlertCdmaDialog();
                    break;
                case ALERT_OMH_WARNING:
                    displayOmhWarningDialog();
                    break;
                case ALERT_OMH_DATA_PICK:
                    displayOmhDataPickDialog();
                    break;
                default:
                    throw new IllegalArgumentException("Invalid dialog type " +
                dialogType + " sent.");
            }
        } else {
            Log.e(TAG, "unexpect happend");
            finish();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        mSimHotSwapHandler.unregisterOnSimHotSwap();
        unregisterReceiver(mSubReceiver);
        // dismiss the dialog to avoid window leak
        if (mDialog != null && mDialog.isShowing()) {
            mDialog.dismiss();
            mDialog = null;
        }
    }

    private void displayDualCdmaDialog() {
        Log.d(TAG, "displayDualCdmaDialog...");
        AlertDialog.Builder alertDialogBuilder = new AlertDialog.Builder(this);
        alertDialogBuilder.setMessage(R.string.two_cdma_dialog_msg);
        alertDialogBuilder.setPositiveButton(android.R.string.ok, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    dialog.dismiss();
                }
                finish();
            }

        });
        alertDialogBuilder.setOnCancelListener(new OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialog) {
                if (dialog != null) {
                    dialog.dismiss();
                }
                finish();
            }

        });
        alertDialogBuilder.setOnKeyListener(new Dialog.OnKeyListener() {
            public boolean onKey(DialogInterface dialog, int keyCode, KeyEvent event) {
                if (keyCode == KeyEvent.KEYCODE_BACK) {
                    finish();
                    return true;
                }
                return false;
            }
        });
        mDialog = alertDialogBuilder.create();
        mDialog.show();
    }

    private void displayAlertCdmaDialog() {
        Log.d(TAG, "displayAlertCdmaDialog...");
        String switchDataAlertMessage = "";
        SubscriptionInfo defaultSir = null;
        int[] list = SubscriptionManager.from(this).getActiveSubscriptionIdList();
        for (int i : list) {
            if (i != mTargetSubId) {
                defaultSir = SubscriptionManager.from(this).getActiveSubscriptionInfo(i);
            }
        }

        if (defaultSir != null) {
            switchDataAlertMessage = this.getResources().getString(R.string.default_data_switch_msg,
                    defaultSir.getDisplayName());
        } else {
            Log.d(TAG, "no need to show the alert dialog");
            return;
        }
        AlertDialog.Builder alertDialogBuilder = new AlertDialog.Builder(this);
        alertDialogBuilder.setMessage(switchDataAlertMessage);
        alertDialogBuilder.setPositiveButton(android.R.string.ok, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    Log.d(TAG, "displayAlertCdmaDialog, set data sub to " + mTargetSubId);
                    setDefaultDataSubId(CdmaSimDialogActivity.this, mTargetSubId);
                    dialog.dismiss();
                }
                finish();
            }
        });
        alertDialogBuilder.setNegativeButton(android.R.string.cancel, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    dialog.dismiss();
                }
                finish();
            }

        });
        alertDialogBuilder.setOnKeyListener(new Dialog.OnKeyListener() {
            public boolean onKey(DialogInterface dialog, int keyCode, KeyEvent event) {
                if (keyCode == KeyEvent.KEYCODE_BACK) {
                    finish();
                    return true;
                }
                return false;
            }
        });
        alertDialogBuilder.setOnCancelListener(new OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialog) {
                finish();
            }
        });
        mDialog = alertDialogBuilder.create();
        mDialog.show();
    }

    private void displayOmhWarningDialog() {
        Log.d(TAG, "displayOmhWarningDialog...");
        AlertDialog.Builder alertDialogBuilder = new AlertDialog.Builder(this);
        alertDialogBuilder.setMessage(R.string.omh_warning_dialog_msg);
        alertDialogBuilder.setPositiveButton(android.R.string.ok, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    dialog.dismiss();
                }
                finish();
            }

        });
        alertDialogBuilder.setOnDismissListener(new OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialog) {
                Log.d(TAG, "OMH warning dialog dismissed...");
                OmhEventHandler.getInstance(CdmaSimDialogActivity.this).sendEmptyMessage(
                        OmhEventHandler.FINISH_REQUEST);
                // re-enable the navigation
                enableStatusBarNavigation(true);
            }
        });
        // this dialog can not be canceled
        alertDialogBuilder.setCancelable(false);
        mDialog = alertDialogBuilder.create();
        mDialog.show();
        // need to disable navigation bar for this dialog
        enableStatusBarNavigation(false);
    }

    @Override
    protected void onResume() {
        // for [C2K OMH Warning] @{
        if (mDialogType == ALERT_OMH_WARNING) {
            enableStatusBarNavigation(false);
        }
        // @}
        super.onResume();
    }

    @Override
    protected void onPause() {
        // for [C2K OMH Warning], ensure to re-enable the navigation @{
        if (mDialogType == ALERT_OMH_WARNING) {
            OmhEventHandler.getInstance(this).sendEmptyMessage(
                    OmhEventHandler.FINISH_REQUEST);
            enableStatusBarNavigation(true);
        }
        // @}
        super.onPause();
    }

    private void displayOmhDataPickDialog() {
        Log.d(TAG, "displayOmhDataPickDialog...");
        AlertDialog.Builder alertDialogBuilder = new AlertDialog.Builder(this);
        alertDialogBuilder.setMessage(R.string.omh_data_pick_dialog_msg);
        alertDialogBuilder.setPositiveButton(R.string.yes, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    Log.d(TAG, "OMH data pick dialog, set data sub to " + mTargetSubId);
                    setDefaultDataSubId(CdmaSimDialogActivity.this, mTargetSubId);
                    dialog.dismiss();
                }
                finish();
            }
        });
        alertDialogBuilder.setNegativeButton(R.string.no, new OnClickListener() {
            @Override
            public void onClick(DialogInterface dialog, int which) {
                if (dialog != null) {
                    dialog.dismiss();
                }
                finish();
            }

        });

        // if user click the space outside of the dialog, should finish the activity
        alertDialogBuilder.setOnCancelListener(new OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialog) {
                finish();
            }
        });

        mDialog = alertDialogBuilder.create();
        mDialog.show();
    }

    // enable or disable the navigation bar
    private void enableStatusBarNavigation(boolean enable) {
        int state = StatusBarManager.DISABLE_NONE;
        if (!enable) {
            // Disable *all* possible navigation via the system bar.
            state |= StatusBarManager.DISABLE_HOME;
            state |= StatusBarManager.DISABLE_RECENT;
            state |= StatusBarManager.DISABLE_BACK;
            state |= StatusBarManager.DISABLE_SEARCH;
        }
        Log.d(TAG, "enableStatusBarNavigation, enable = " + enable);
        mStatusBarManager.disable(state);
    }

    private void setDefaultDataSubId(final Context context, final int subId) {
        final SubscriptionManager subscriptionManager = SubscriptionManager.from(context);
        subscriptionManager.setDefaultDataSubId(subId);
        if (mActionType == SimDialogActivity.DATA_PICK) {
            Toast.makeText(context, R.string.data_switch_started, Toast.LENGTH_LONG).show();
        }
    }
}
