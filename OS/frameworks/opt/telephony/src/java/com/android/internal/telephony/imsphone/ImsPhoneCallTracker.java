/*
 * Copyright (C) 2013 The Android Open Source Project
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

package com.android.internal.telephony.imsphone;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.net.NetworkStats;
import android.net.Uri;
import android.os.AsyncResult;
import android.os.Bundle;
import android.os.Handler;
import android.os.Message;
import android.os.PersistableBundle;
import android.os.Registrant;
import android.os.RegistrantList;
import android.os.RemoteException;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.preference.PreferenceManager;
import android.provider.Settings;
import android.telecom.ConferenceParticipant;
import android.telecom.TelecomManager;
import android.telecom.VideoProfile;
import android.telephony.CarrierConfigManager;
import android.telephony.DisconnectCause;
import android.telephony.PhoneNumberUtils;
import android.telephony.PreciseDisconnectCause;
import android.telephony.Rlog;
import android.telephony.ServiceState;
import android.telephony.SubscriptionManager;
import android.telephony.TelephonyManager;
import android.telephony.ims.ImsServiceProxy;
import android.telephony.ims.feature.ImsFeature;
import android.text.TextUtils;
import android.util.ArrayMap;
import android.util.Log;
import android.util.Pair;
import android.util.SparseIntArray;

import com.android.ims.ImsCall;
import com.android.ims.ImsCallProfile;
import com.android.ims.ImsConfig;
import com.android.ims.ImsConfigListener;
import com.android.ims.ImsConnectionStateListener;
import com.android.ims.ImsEcbm;
import com.android.ims.ImsException;
import com.android.ims.ImsManager;
import com.android.ims.ImsMultiEndpoint;
import com.android.ims.ImsReasonInfo;
import com.android.ims.ImsServiceClass;
import com.android.ims.ImsSuppServiceNotification;
import com.android.ims.ImsUtInterface;
import com.android.ims.internal.IImsVideoCallProvider;
import com.android.ims.internal.ImsVideoCallProviderWrapper;
import com.android.ims.internal.VideoPauseTracker;
import com.android.internal.annotations.VisibleForTesting;
import com.android.internal.os.SomeArgs;
import com.android.internal.telephony.Call;
import com.android.internal.telephony.CallStateException;
import com.android.internal.telephony.CallTracker;
import com.android.internal.telephony.CommandException;
import com.android.internal.telephony.CommandsInterface;
import com.android.internal.telephony.Connection;
import com.android.internal.telephony.Phone;
import com.android.internal.telephony.PhoneConstants;
import com.android.internal.telephony.TelephonyProperties;
import com.android.internal.telephony.dataconnection.DataEnabledSettings;
import com.android.internal.telephony.gsm.SuppServiceNotification;
import com.android.internal.telephony.metrics.TelephonyMetrics;
import com.android.internal.telephony.nano.TelephonyProto.ImsConnectionState;
import com.android.internal.telephony.nano.TelephonyProto.TelephonyCallSession;
import com.android.internal.telephony.nano.TelephonyProto.TelephonyCallSession.Event.ImsCommand;
import com.android.server.net.NetworkStatsService;

import java.io.FileDescriptor;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;

/**
 * {@hide}
 */
public class ImsPhoneCallTracker extends CallTracker implements ImsPullCall {
    static final String LOG_TAG = "ImsPhoneCallTracker";
    static final String VERBOSE_STATE_TAG = "IPCTState";

    public interface PhoneStateListener {
        void onPhoneStateChanged(PhoneConstants.State oldState, PhoneConstants.State newState);
    }

    public interface SharedPreferenceProxy {
        SharedPreferences getDefaultSharedPreferences(Context context);
    }

    public interface PhoneNumberUtilsProxy {
        boolean isEmergencyNumber(String number);
    }

    protected static final boolean DBG = true;

    // When true, dumps the state of ImsPhoneCallTracker after changes to foreground and background
    // calls.  This is helpful for debugging.  It is also possible to enable this at runtime by
    // setting the IPCTState log tag to VERBOSE.
    private static final boolean FORCE_VERBOSE_STATE_LOGGING = false; /* stopship if true */
    private static final boolean VERBOSE_STATE_LOGGING = FORCE_VERBOSE_STATE_LOGGING ||
            Rlog.isLoggable(VERBOSE_STATE_TAG, Log.VERBOSE);

    //Indices map to ImsConfig.FeatureConstants
    protected boolean[] mImsFeatureEnabled = {false, false, false, false, false, false};
    protected final String[] mImsFeatureStrings = {"VoLTE", "ViLTE", "VoWiFi", "ViWiFi",
            "UTLTE", "UTWiFi"};

    protected TelephonyMetrics mMetrics;

    protected BroadcastReceiver mReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (intent.getAction().equals(ImsManager.ACTION_IMS_INCOMING_CALL)) {
                if (DBG) log("onReceive : incoming call intent");

                if (mImsManager == null) return;

                if (mServiceId < 0) return;

                try {
                    // Network initiated USSD will be treated by mImsUssdListener
                    boolean isUssd = intent.getBooleanExtra(ImsManager.EXTRA_USSD, false);
                    if (isUssd) {
                        if (DBG) log("onReceive : USSD");
                        mUssdSession = mImsManager.takeCall(mServiceId, intent, mImsUssdListener);
                        if (mUssdSession != null) {
                            mUssdSession.accept(ImsCallProfile.CALL_TYPE_VOICE);
                        }
                        return;
                    }

                    boolean isUnknown = intent.getBooleanExtra(ImsManager.EXTRA_IS_UNKNOWN_CALL,
                            false);
                    if (DBG) {
                        log("onReceive : isUnknown = " + isUnknown +
                                " fg = " + mForegroundCall.getState() +
                                " bg = " + mBackgroundCall.getState());
                    }

                    // Normal MT/Unknown call
                    /// M: @{
                    //ImsCall imsCall = mImsManager.takeCall(mServiceId, intent, mImsCallListener);
                    ImsCall imsCall = takeCall(intent);
                    //ImsPhoneConnection conn = new ImsPhoneConnection(mPhone, imsCall,
                    //        ImsPhoneCallTracker.this,
                    //        (isUnknown? mForegroundCall: mRingingCall), isUnknown);
                    ImsPhoneConnection conn = makeImsPhoneConnectionForMT(imsCall, isUnknown);
                    /// @}

                    // If there is an active call.
                    if (mForegroundCall.hasConnections()) {
                        ImsCall activeCall = mForegroundCall.getFirstConnection().getImsCall();
                        if (activeCall != null && imsCall != null) {
                            // activeCall could be null if the foreground call is in a disconnected
                            // state.  If either of the calls is null there is no need to check if
                            // one will be disconnected on answer.
                            boolean answeringWillDisconnect =
                                    shouldDisconnectActiveCallOnAnswer(activeCall, imsCall);
                            conn.setActiveCallDisconnectedOnAnswer(answeringWillDisconnect);
                        }
                    }
                    conn.setAllowAddCallDuringVideoCall(mAllowAddCallDuringVideoCall);
                    addConnection(conn);

                    setVideoCallProvider(conn, imsCall);

                    /// M: ALPS02136981. Prints debug logs for ImsPhone.
                    logDebugMessagesWithDumpFormat("CC", conn, "");

                    //M: for RTT
                    checkIncomingRtt(intent, imsCall, conn);

                    TelephonyMetrics.getInstance().writeOnImsCallReceive(mPhone.getPhoneId(),
                            imsCall.getSession());

                    if (isUnknown) {
                        mPhone.notifyUnknownConnection(conn);
                    } else {
                        if ((mForegroundCall.getState() != ImsPhoneCall.State.IDLE) ||
                                (mBackgroundCall.getState() != ImsPhoneCall.State.IDLE)) {
                            conn.update(imsCall, ImsPhoneCall.State.WAITING);
                        }

                        mPhone.notifyNewRingingConnection(conn);
                        mPhone.notifyIncomingRing();
                    }

                    updatePhoneState();
                    mPhone.notifyPreciseCallStateChanged();
                } catch (ImsException e) {
                    loge("onReceive : exception " + e);
                } catch (RemoteException e) {
                }
            } else if (intent.getAction().equals(
                    CarrierConfigManager.ACTION_CARRIER_CONFIG_CHANGED)) {
                int subId = intent.getIntExtra(PhoneConstants.SUBSCRIPTION_KEY,
                        SubscriptionManager.INVALID_SUBSCRIPTION_ID);
                if (subId == mPhone.getSubId()) {
                    cacheCarrierConfiguration(subId);
                    log("onReceive : Updating mAllowEmergencyVideoCalls = " +
                            mAllowEmergencyVideoCalls);
                }
            } else if (TelecomManager.ACTION_CHANGE_DEFAULT_DIALER.equals(intent.getAction())) {
                mDefaultDialerUid.set(getPackageUid(context, intent.getStringExtra(
                        TelecomManager.EXTRA_CHANGE_DEFAULT_DIALER_PACKAGE_NAME)));
            }
        }
    };

    //***** Constants

    static final int MAX_CONNECTIONS = 7;
    static final int MAX_CONNECTIONS_PER_CALL = 5;

    protected static final int EVENT_HANGUP_PENDINGMO = 18;
    protected static final int EVENT_RESUME_BACKGROUND = 19;
    protected static final int EVENT_DIAL_PENDINGMO = 20;
    private static final int EVENT_EXIT_ECBM_BEFORE_PENDINGMO = 21;
    private static final int EVENT_VT_DATA_USAGE_UPDATE = 22;
    protected static final int EVENT_DATA_ENABLED_CHANGED = 23;
    protected static final int EVENT_GET_IMS_SERVICE = 24;
    protected static final int EVENT_CHECK_FOR_WIFI_HANDOVER = 25;
    private static final int EVENT_ON_FEATURE_CAPABILITY_CHANGED = 26;

    protected static final int TIMEOUT_HANGUP_PENDINGMO = 500;

    // Initial condition for ims connection retry.
    protected static final int IMS_RETRY_STARTING_TIMEOUT_MS = 500; // ms
    // Ceiling bitshift amount for service query timeout, calculated as:
    // 2^mImsServiceRetryCount * IMS_RETRY_STARTING_TIMEOUT_MS, where
    // mImsServiceRetryCount ∊ [0, CEILING_SERVICE_RETRY_COUNT].
    protected static final int CEILING_SERVICE_RETRY_COUNT = 6;

    protected static final int HANDOVER_TO_WIFI_TIMEOUT_MS = 60000; // ms

    //***** Instance Variables
    /// M: mConnection might be access by other thread, use a thread safe list to avoid concurrency
    /// exception. @{
    protected CopyOnWriteArrayList<ImsPhoneConnection> mConnections =
            new CopyOnWriteArrayList<ImsPhoneConnection>();
    /// @}
    //protected ArrayList<ImsPhoneConnection> mConnections = new ArrayList<ImsPhoneConnection>();
    private RegistrantList mVoiceCallEndedRegistrants = new RegistrantList();
    private RegistrantList mVoiceCallStartedRegistrants = new RegistrantList();

    public ImsPhoneCall mRingingCall = new ImsPhoneCall(this, ImsPhoneCall.CONTEXT_RINGING);
    public ImsPhoneCall mForegroundCall = new ImsPhoneCall(this,
            ImsPhoneCall.CONTEXT_FOREGROUND);
    public ImsPhoneCall mBackgroundCall = new ImsPhoneCall(this,
            ImsPhoneCall.CONTEXT_BACKGROUND);
    public ImsPhoneCall mHandoverCall = new ImsPhoneCall(this, ImsPhoneCall.CONTEXT_HANDOVER);

    // Hold aggregated video call data usage for each video call since boot.
    // The ImsCall's call id is the key of the map.
    private final HashMap<Integer, Long> mVtDataUsageMap = new HashMap<>();

    private volatile NetworkStats mVtDataUsageSnapshot = null;
    private volatile NetworkStats mVtDataUsageUidSnapshot = null;

    private final AtomicInteger mDefaultDialerUid = new AtomicInteger(NetworkStats.UID_ALL);

    protected ImsPhoneConnection mPendingMO;
    protected int mClirMode = CommandsInterface.CLIR_DEFAULT;
    protected Object mSyncHold = new Object();

    protected ImsCall mUssdSession = null;
    protected Message mPendingUssd = null;

    public ImsPhone mPhone;

    private boolean mDesiredMute = false;    // false = mute off
    protected boolean mOnHoldToneStarted = false;
    protected int mOnHoldToneId = -1;

    protected PhoneConstants.State mState = PhoneConstants.State.IDLE;

    protected int mImsServiceRetryCount;
    protected ImsManager mImsManager;
    protected int mServiceId = -1;

    protected Call.SrvccState mSrvccState = Call.SrvccState.NONE;

    private boolean mIsInEmergencyCall = false;
    private boolean mIsDataEnabled = false;

    protected int pendingCallClirMode;
    protected int mPendingCallVideoState;
    protected Bundle mPendingIntentExtras;
    protected boolean pendingCallInEcm = false;
    protected boolean mSwitchingFgAndBgCalls = false;
    protected ImsCall mCallExpectedToResume = null;
    protected boolean mAllowEmergencyVideoCalls = false;
    private boolean mIgnoreDataEnabledChangedForVideoCalls = false;
    private boolean mIsViLteDataMetered = false;

    /**
     * Listeners to changes in the phone state.  Intended for use by other interested IMS components
     * without the need to register a full blown {@link android.telephony.PhoneStateListener}.
     */
    private List<PhoneStateListener> mPhoneStateListeners = new ArrayList<>();

    /**
     * Carrier configuration option which determines if video calls which have been downgraded to an
     * audio call should be treated as if they are still video calls.
     */
    private boolean mTreatDowngradedVideoCallsAsVideoCalls = false;

    /**
     * Carrier configuration option which determines if an ongoing video call over wifi should be
     * dropped when an audio call is answered.
     */
    private boolean mDropVideoCallWhenAnsweringAudioCall = false;

    /**
     * Carrier configuration option which determines whether adding a call during a video call
     * should be allowed.
     */
    protected boolean mAllowAddCallDuringVideoCall = true;

    /**
     * Carrier configuration option which determines whether to notify the connection if a handover
     * to wifi fails.
     */
    protected boolean mNotifyVtHandoverToWifiFail = false;

    /**
     * Carrier configuration option which determines whether the carrier supports downgrading a
     * TX/RX/TX-RX video call directly to an audio-only call.
     */
    private boolean mSupportDowngradeVtToAudio = false;

    /**
     * Stores the mapping of {@code ImsReasonInfo#CODE_*} to {@code PreciseDisconnectCause#*}
     */
    private static final SparseIntArray PRECISE_CAUSE_MAP = new SparseIntArray();
    static {
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_ILLEGAL_ARGUMENT,
                PreciseDisconnectCause.LOCAL_ILLEGAL_ARGUMENT);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_ILLEGAL_STATE,
                PreciseDisconnectCause.LOCAL_ILLEGAL_STATE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_INTERNAL_ERROR,
                PreciseDisconnectCause.LOCAL_INTERNAL_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_IMS_SERVICE_DOWN,
                PreciseDisconnectCause.LOCAL_IMS_SERVICE_DOWN);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NO_PENDING_CALL,
                PreciseDisconnectCause.LOCAL_NO_PENDING_CALL);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_ENDED_BY_CONFERENCE_MERGE,
                PreciseDisconnectCause.NORMAL);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_POWER_OFF,
                PreciseDisconnectCause.LOCAL_POWER_OFF);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_LOW_BATTERY,
                PreciseDisconnectCause.LOCAL_LOW_BATTERY);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NETWORK_NO_SERVICE,
                PreciseDisconnectCause.LOCAL_NETWORK_NO_SERVICE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NETWORK_NO_LTE_COVERAGE,
                PreciseDisconnectCause.LOCAL_NETWORK_NO_LTE_COVERAGE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NETWORK_ROAMING,
                PreciseDisconnectCause.LOCAL_NETWORK_ROAMING);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NETWORK_IP_CHANGED,
                PreciseDisconnectCause.LOCAL_NETWORK_IP_CHANGED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_SERVICE_UNAVAILABLE,
                PreciseDisconnectCause.LOCAL_SERVICE_UNAVAILABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_NOT_REGISTERED,
                PreciseDisconnectCause.LOCAL_NOT_REGISTERED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_EXCEEDED,
                PreciseDisconnectCause.LOCAL_MAX_CALL_EXCEEDED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_DECLINE,
                PreciseDisconnectCause.LOCAL_CALL_DECLINE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_VCC_ON_PROGRESSING,
                PreciseDisconnectCause.LOCAL_CALL_VCC_ON_PROGRESSING);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_RESOURCE_RESERVATION_FAILED,
                PreciseDisconnectCause.LOCAL_CALL_RESOURCE_RESERVATION_FAILED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_CS_RETRY_REQUIRED,
                PreciseDisconnectCause.LOCAL_CALL_CS_RETRY_REQUIRED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_VOLTE_RETRY_REQUIRED,
                PreciseDisconnectCause.LOCAL_CALL_VOLTE_RETRY_REQUIRED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_CALL_TERMINATED,
                PreciseDisconnectCause.LOCAL_CALL_TERMINATED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOCAL_HO_NOT_FEASIBLE,
                PreciseDisconnectCause.LOCAL_HO_NOT_FEASIBLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_TIMEOUT_1XX_WAITING,
                PreciseDisconnectCause.TIMEOUT_1XX_WAITING);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_TIMEOUT_NO_ANSWER,
                PreciseDisconnectCause.TIMEOUT_NO_ANSWER);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_TIMEOUT_NO_ANSWER_CALL_UPDATE,
                PreciseDisconnectCause.TIMEOUT_NO_ANSWER_CALL_UPDATE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_FDN_BLOCKED,
                PreciseDisconnectCause.FDN_BLOCKED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_REDIRECTED,
                PreciseDisconnectCause.SIP_REDIRECTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_BAD_REQUEST,
                PreciseDisconnectCause.SIP_BAD_REQUEST);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_FORBIDDEN,
                PreciseDisconnectCause.SIP_FORBIDDEN);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_NOT_FOUND,
                PreciseDisconnectCause.SIP_NOT_FOUND);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_NOT_SUPPORTED,
                PreciseDisconnectCause.SIP_NOT_SUPPORTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_REQUEST_TIMEOUT,
                PreciseDisconnectCause.SIP_REQUEST_TIMEOUT);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_TEMPRARILY_UNAVAILABLE,
                PreciseDisconnectCause.SIP_TEMPRARILY_UNAVAILABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_BAD_ADDRESS,
                PreciseDisconnectCause.SIP_BAD_ADDRESS);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_BUSY,
                PreciseDisconnectCause.SIP_BUSY);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_REQUEST_CANCELLED,
                PreciseDisconnectCause.SIP_REQUEST_CANCELLED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_NOT_ACCEPTABLE,
                PreciseDisconnectCause.SIP_NOT_ACCEPTABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_NOT_REACHABLE,
                PreciseDisconnectCause.SIP_NOT_REACHABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_CLIENT_ERROR,
                PreciseDisconnectCause.SIP_CLIENT_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_SERVER_INTERNAL_ERROR,
                PreciseDisconnectCause.SIP_SERVER_INTERNAL_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_SERVICE_UNAVAILABLE,
                PreciseDisconnectCause.SIP_SERVICE_UNAVAILABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_SERVER_TIMEOUT,
                PreciseDisconnectCause.SIP_SERVER_TIMEOUT);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_SERVER_ERROR,
                PreciseDisconnectCause.SIP_SERVER_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_USER_REJECTED,
                PreciseDisconnectCause.SIP_USER_REJECTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SIP_GLOBAL_ERROR,
                PreciseDisconnectCause.SIP_GLOBAL_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_EMERGENCY_TEMP_FAILURE,
                PreciseDisconnectCause.EMERGENCY_TEMP_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_EMERGENCY_PERM_FAILURE,
                PreciseDisconnectCause.EMERGENCY_PERM_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MEDIA_INIT_FAILED,
                PreciseDisconnectCause.MEDIA_INIT_FAILED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MEDIA_NO_DATA,
                PreciseDisconnectCause.MEDIA_NO_DATA);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MEDIA_NOT_ACCEPTABLE,
                PreciseDisconnectCause.MEDIA_NOT_ACCEPTABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MEDIA_UNSPECIFIED,
                PreciseDisconnectCause.MEDIA_UNSPECIFIED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_USER_TERMINATED,
                PreciseDisconnectCause.USER_TERMINATED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_USER_NOANSWER,
                PreciseDisconnectCause.USER_NOANSWER);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_USER_IGNORE,
                PreciseDisconnectCause.USER_IGNORE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_USER_DECLINE,
                PreciseDisconnectCause.USER_DECLINE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_LOW_BATTERY,
                PreciseDisconnectCause.LOW_BATTERY);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_BLACKLISTED_CALL_ID,
                PreciseDisconnectCause.BLACKLISTED_CALL_ID);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_USER_TERMINATED_BY_REMOTE,
                PreciseDisconnectCause.USER_TERMINATED_BY_REMOTE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_UT_NOT_SUPPORTED,
                PreciseDisconnectCause.UT_NOT_SUPPORTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_UT_SERVICE_UNAVAILABLE,
                PreciseDisconnectCause.UT_SERVICE_UNAVAILABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_UT_OPERATION_NOT_ALLOWED,
                PreciseDisconnectCause.UT_OPERATION_NOT_ALLOWED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_UT_NETWORK_ERROR,
                PreciseDisconnectCause.UT_NETWORK_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_UT_CB_PASSWORD_MISMATCH,
                PreciseDisconnectCause.UT_CB_PASSWORD_MISMATCH);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_ECBM_NOT_SUPPORTED,
                PreciseDisconnectCause.ECBM_NOT_SUPPORTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MULTIENDPOINT_NOT_SUPPORTED,
                PreciseDisconnectCause.MULTIENDPOINT_NOT_SUPPORTED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_CALL_DROP_IWLAN_TO_LTE_UNAVAILABLE,
                PreciseDisconnectCause.CALL_DROP_IWLAN_TO_LTE_UNAVAILABLE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_ANSWERED_ELSEWHERE,
                PreciseDisconnectCause.ANSWERED_ELSEWHERE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_CALL_PULL_OUT_OF_SYNC,
                PreciseDisconnectCause.CALL_PULL_OUT_OF_SYNC);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_CALL_END_CAUSE_CALL_PULL,
                PreciseDisconnectCause.CALL_PULLED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SUPP_SVC_FAILED,
                PreciseDisconnectCause.SUPP_SVC_FAILED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SUPP_SVC_CANCELLED,
                PreciseDisconnectCause.SUPP_SVC_CANCELLED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_SUPP_SVC_REINVITE_COLLISION,
                PreciseDisconnectCause.SUPP_SVC_REINVITE_COLLISION);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_IWLAN_DPD_FAILURE,
                PreciseDisconnectCause.IWLAN_DPD_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_EPDG_TUNNEL_ESTABLISH_FAILURE,
                PreciseDisconnectCause.EPDG_TUNNEL_ESTABLISH_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_EPDG_TUNNEL_REKEY_FAILURE,
                PreciseDisconnectCause.EPDG_TUNNEL_REKEY_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_EPDG_TUNNEL_LOST_CONNECTION,
                PreciseDisconnectCause.EPDG_TUNNEL_LOST_CONNECTION);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_MAXIMUM_NUMBER_OF_CALLS_REACHED,
                PreciseDisconnectCause.MAXIMUM_NUMBER_OF_CALLS_REACHED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_REMOTE_CALL_DECLINE,
                PreciseDisconnectCause.REMOTE_CALL_DECLINE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_DATA_LIMIT_REACHED,
                PreciseDisconnectCause.DATA_LIMIT_REACHED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_DATA_DISABLED,
                PreciseDisconnectCause.DATA_DISABLED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_WIFI_LOST,
                PreciseDisconnectCause.WIFI_LOST);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_OFF,
                PreciseDisconnectCause.RADIO_OFF);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_NO_VALID_SIM,
                PreciseDisconnectCause.NO_VALID_SIM);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_INTERNAL_ERROR,
                PreciseDisconnectCause.RADIO_INTERNAL_ERROR);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_NETWORK_RESP_TIMEOUT,
                PreciseDisconnectCause.NETWORK_RESP_TIMEOUT);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_NETWORK_REJECT,
                PreciseDisconnectCause.NETWORK_REJECT);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_ACCESS_FAILURE,
                PreciseDisconnectCause.RADIO_ACCESS_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_LINK_FAILURE,
                PreciseDisconnectCause.RADIO_LINK_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_LINK_LOST,
                PreciseDisconnectCause.RADIO_LINK_LOST);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_UPLINK_FAILURE,
                PreciseDisconnectCause.RADIO_UPLINK_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_SETUP_FAILURE,
                PreciseDisconnectCause.RADIO_SETUP_FAILURE);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_RELEASE_NORMAL,
                PreciseDisconnectCause.RADIO_RELEASE_NORMAL);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_RADIO_RELEASE_ABNORMAL,
                PreciseDisconnectCause.RADIO_RELEASE_ABNORMAL);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_ACCESS_CLASS_BLOCKED,
                PreciseDisconnectCause.ACCESS_CLASS_BLOCKED);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_NETWORK_DETACH,
                PreciseDisconnectCause.NETWORK_DETACH);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_1,
                PreciseDisconnectCause.OEM_CAUSE_1);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_2,
                PreciseDisconnectCause.OEM_CAUSE_2);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_3,
                PreciseDisconnectCause.OEM_CAUSE_3);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_4,
                PreciseDisconnectCause.OEM_CAUSE_4);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_5,
                PreciseDisconnectCause.OEM_CAUSE_5);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_6,
                PreciseDisconnectCause.OEM_CAUSE_6);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_7,
                PreciseDisconnectCause.OEM_CAUSE_7);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_8,
                PreciseDisconnectCause.OEM_CAUSE_8);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_9,
                PreciseDisconnectCause.OEM_CAUSE_9);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_10,
                PreciseDisconnectCause.OEM_CAUSE_10);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_11,
                PreciseDisconnectCause.OEM_CAUSE_11);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_12,
                PreciseDisconnectCause.OEM_CAUSE_12);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_13,
                PreciseDisconnectCause.OEM_CAUSE_13);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_14,
                PreciseDisconnectCause.OEM_CAUSE_14);
        PRECISE_CAUSE_MAP.append(ImsReasonInfo.CODE_OEM_CAUSE_15,
                PreciseDisconnectCause.OEM_CAUSE_15);
    }

    /**
     * Carrier configuration option which determines whether the carrier wants to inform the user
     * when a video call is handed over from WIFI to LTE.
     * See {@link CarrierConfigManager#KEY_NOTIFY_HANDOVER_VIDEO_FROM_WIFI_TO_LTE_BOOL} for more
     * information.
     */
    protected boolean mNotifyHandoverVideoFromWifiToLTE = false;

    /**
     * Carrier configuration option which determines whether the carrier supports the
     * {@link VideoProfile#STATE_PAUSED} signalling.
     * See {@link CarrierConfigManager#KEY_SUPPORT_PAUSE_IMS_VIDEO_CALLS_BOOL} for more information.
     */
    private boolean mSupportPauseVideo = false;

    /**
     * Carrier configuration option which defines a mapping from pairs of
     * {@link ImsReasonInfo#getCode()} and {@link ImsReasonInfo#getExtraMessage()} values to a new
     * {@code ImsReasonInfo#CODE_*} value.
     *
     * See {@link CarrierConfigManager#KEY_IMS_REASONINFO_MAPPING_STRING_ARRAY}.
     */
    private Map<Pair<Integer, String>, Integer> mImsReasonCodeMap = new ArrayMap<>();


    /**
     * TODO: Remove this code; it is a workaround.
     * When {@code true}, forces {@link ImsManager#updateImsServiceConfig(Context, int, boolean)} to
     * be called when an ongoing video call is disconnected.  In some cases, where video pause is
     * supported by the carrier, when {@link #onDataEnabledChanged(boolean, int)} reports that data
     * has been disabled we will pause the video rather than disconnecting the call.  When this
     * happens we need to prevent the IMS service config from being updated, as this will cause VT
     * to be disabled mid-call, resulting in an inability to un-pause the video.
     */
    protected boolean mShouldUpdateImsConfigOnDisconnect = false;

    /**
     * Default implementation for retrieving shared preferences; uses the actual PreferencesManager.
     */
    private SharedPreferenceProxy mSharedPreferenceProxy = (Context context) -> {
        return PreferenceManager.getDefaultSharedPreferences(context);
    };

    /**
     * Default implementation for determining if a number is an emergency number.  Uses the real
     * PhoneNumberUtils.
     */
    private PhoneNumberUtilsProxy mPhoneNumberUtilsProxy = (String string) -> {
        return PhoneNumberUtils.isEmergencyNumber(string);
    };

    // Callback fires when ImsManager MMTel Feature changes state
    private ImsServiceProxy.INotifyStatusChanged mNotifyStatusChangedCallback = () -> {
        try {
            int status = mImsManager.getImsServiceStatus();
            log("Status Changed: " + status);
            switch(status) {
                case ImsFeature.STATE_READY: {
                    startListeningForCalls();
                    break;
                }
                case ImsFeature.STATE_INITIALIZING:
                    // fall through
                case ImsFeature.STATE_NOT_AVAILABLE: {
                    stopListeningForCalls();
                    break;
                }
                default: {
                    Log.w(LOG_TAG, "Unexpected State!");
                }
            }
        } catch (ImsException e) {
            // Could not get the ImsService, retry!
            retryGetImsService();
        }
    };

    @VisibleForTesting
    public interface IRetryTimeout {
        int get();
    }

    /**
     * Default implementation of interface that calculates the ImsService retry timeout.
     * Override-able for testing.
     */
    @VisibleForTesting
    public IRetryTimeout mRetryTimeout = () -> {
        int timeout = (1 << mImsServiceRetryCount) * IMS_RETRY_STARTING_TIMEOUT_MS;
        if (mImsServiceRetryCount <= CEILING_SERVICE_RETRY_COUNT) {
            mImsServiceRetryCount++;
        }
        return timeout;
    };

    //***** Events


    //***** Constructors
    /// M: BSP+ @{
    public ImsPhoneCallTracker() {
    }
    /// @}
    public ImsPhoneCallTracker(ImsPhone phone) {
        this.mPhone = phone;

        mMetrics = TelephonyMetrics.getInstance();

        IntentFilter intentfilter = new IntentFilter();
        intentfilter.addAction(ImsManager.ACTION_IMS_INCOMING_CALL);
        intentfilter.addAction(CarrierConfigManager.ACTION_CARRIER_CONFIG_CHANGED);
        intentfilter.addAction(TelecomManager.ACTION_CHANGE_DEFAULT_DIALER);
        mPhone.getContext().registerReceiver(mReceiver, intentfilter);
        cacheCarrierConfiguration(mPhone.getSubId());

        mPhone.getDefaultPhone().registerForDataEnabledChanged(
                this, EVENT_DATA_ENABLED_CHANGED, null);

        mImsServiceRetryCount = 0;

        final TelecomManager telecomManager =
                (TelecomManager) mPhone.getContext().getSystemService(Context.TELECOM_SERVICE);
        mDefaultDialerUid.set(
                getPackageUid(mPhone.getContext(), telecomManager.getDefaultDialerPackage()));

        long currentTime = SystemClock.elapsedRealtime();
        mVtDataUsageSnapshot = new NetworkStats(currentTime, 1);
        mVtDataUsageUidSnapshot = new NetworkStats(currentTime, 1);

        // Send a message to connect to the Ims Service and open a connection through
        // getImsService().
        sendEmptyMessage(EVENT_GET_IMS_SERVICE);
    }

    /**
     * Test-only method used to mock out access to the shared preferences through the
     * {@link PreferenceManager}.
     * @param sharedPreferenceProxy
     */
    @VisibleForTesting
    public void setSharedPreferenceProxy(SharedPreferenceProxy sharedPreferenceProxy) {
        mSharedPreferenceProxy = sharedPreferenceProxy;
    }

    /**
     * Test-only method used to mock out access to the phone number utils class.
     * @param phoneNumberUtilsProxy
     */
    @VisibleForTesting
    public void setPhoneNumberUtilsProxy(PhoneNumberUtilsProxy phoneNumberUtilsProxy) {
        mPhoneNumberUtilsProxy = phoneNumberUtilsProxy;
    }

    private int getPackageUid(Context context, String pkg) {
        if (pkg == null) {
            return NetworkStats.UID_ALL;
        }

        // Initialize to UID_ALL so at least it can be counted to overall data usage if
        // the dialer's package uid is not available.
        int uid = NetworkStats.UID_ALL;
        try {
            uid = context.getPackageManager().getPackageUid(pkg, 0);
        } catch (PackageManager.NameNotFoundException e) {
            loge("Cannot find package uid. pkg = " + pkg);
        }
        return uid;
    }

    private PendingIntent createIncomingCallPendingIntent() {
        Intent intent = new Intent(ImsManager.ACTION_IMS_INCOMING_CALL);
        intent.addFlags(Intent.FLAG_RECEIVER_FOREGROUND);
        return PendingIntent.getBroadcast(mPhone.getContext(), 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT);
    }

    protected void getImsService() throws ImsException {
        if (DBG) log("getImsService");
        mImsManager = ImsManager.getInstance(mPhone.getContext(), mPhone.getPhoneId());
        // Adding to set, will be safe adding multiple times. If the ImsService is not active yet,
        // this method will throw an ImsException.
        mImsManager.addNotifyStatusChangedCallbackIfAvailable(mNotifyStatusChangedCallback);
        // Wait for ImsService.STATE_READY to start listening for calls.
        // Call the callback right away for compatibility with older devices that do not use states.
        mNotifyStatusChangedCallback.notifyStatusChanged();
    }

    private void startListeningForCalls() throws ImsException {
        mImsServiceRetryCount = 0;
        mServiceId = mImsManager.open(ImsServiceClass.MMTEL,
                createIncomingCallPendingIntent(),
                mImsConnectionStateListener);

        mImsManager.setImsConfigListener(mImsConfigListener);

        // Get the ECBM interface and set IMSPhone's listener object for notifications
        getEcbmInterface().setEcbmStateListener(mPhone.getImsEcbmStateListener());
        if (mPhone.isInEcm()) {
            // Call exit ECBM which will invoke onECBMExited
            mPhone.exitEmergencyCallbackMode();
        }
        int mPreferredTtyMode = Settings.Secure.getInt(
                mPhone.getContext().getContentResolver(),
                Settings.Secure.PREFERRED_TTY_MODE,
                Phone.TTY_MODE_OFF);
        mImsManager.setUiTTYMode(mPhone.getContext(), mPreferredTtyMode, null);

        ImsMultiEndpoint multiEndpoint = getMultiEndpointInterface();
        if (multiEndpoint != null) {
            multiEndpoint.setExternalCallStateListener(
                    mPhone.getExternalCallTracker().getExternalCallStateListener());
        }
    }

    private void stopListeningForCalls() {
        try {
            // Only close on valid session.
            if (mImsManager != null && mServiceId > 0) {
                mImsManager.close(mServiceId);
                mServiceId = -1;
            }
        } catch (ImsException e) {
            // If the binder is unavailable, then the ImsService doesn't need to close.
        }
    }

    public void dispose() {
        if (DBG) log("dispose");
        mRingingCall.dispose();
        mBackgroundCall.dispose();
        mForegroundCall.dispose();
        mHandoverCall.dispose();

        clearDisconnected();
        mPhone.getContext().unregisterReceiver(mReceiver);
        mPhone.getDefaultPhone().unregisterForDataEnabledChanged(this);
        removeMessages(EVENT_GET_IMS_SERVICE);
    }

    @Override
    protected void finalize() {
        log("ImsPhoneCallTracker finalized");
    }

    //***** Instance Methods

    //***** Public Methods
    @Override
    public void registerForVoiceCallStarted(Handler h, int what, Object obj) {
        Registrant r = new Registrant(h, what, obj);
        mVoiceCallStartedRegistrants.add(r);
    }

    @Override
    public void unregisterForVoiceCallStarted(Handler h) {
        mVoiceCallStartedRegistrants.remove(h);
    }

    @Override
    public void registerForVoiceCallEnded(Handler h, int what, Object obj) {
        Registrant r = new Registrant(h, what, obj);
        mVoiceCallEndedRegistrants.add(r);
    }

    @Override
    public void unregisterForVoiceCallEnded(Handler h) {
        mVoiceCallEndedRegistrants.remove(h);
    }

    public Connection dial(String dialString, int videoState, Bundle intentExtras) throws
            CallStateException {
        int oirMode;
        if (mSharedPreferenceProxy != null && mPhone.getDefaultPhone() != null) {
            SharedPreferences sp = mSharedPreferenceProxy.getDefaultSharedPreferences(
                    mPhone.getContext());
            oirMode = sp.getInt(Phone.CLIR_KEY + mPhone.getDefaultPhone().getPhoneId(),
                    CommandsInterface.CLIR_DEFAULT);
        } else {
            loge("dial; could not get default CLIR mode.");
            oirMode = CommandsInterface.CLIR_DEFAULT;
        }
        return dial(dialString, oirMode, videoState, intentExtras);
    }

    /**
     * oirMode is one of the CLIR_ constants
     */
    public synchronized Connection
    dial(String dialString, int clirMode, int videoState, Bundle intentExtras)
            throws CallStateException {
        boolean isPhoneInEcmMode = isPhoneInEcbMode();
        /// M: @{
        //boolean isEmergencyNumber = mPhoneNumberUtilsProxy.isEmergencyNumber(dialString);
        boolean isEmergencyNumber = isEmergencyNumber(dialString);
        /// @}

        if (DBG) log("dial clirMode=" + clirMode);
        if (isEmergencyNumber) {
            clirMode = CommandsInterface.CLIR_SUPPRESSION;
            if (DBG) log("dial emergency call, set clirModIe=" + clirMode);
        }

        // note that this triggers call state changed notif
        clearDisconnected();

        if (mImsManager == null) {
            throw new CallStateException("service not available");
        }

        /// M: check for CSFB
        checkforCsfb();

        if (!canDial()) {
            throw new CallStateException("cannot dial in current state");
        }

        if (isPhoneInEcmMode && isEmergencyNumber) {
            handleEcmTimer(ImsPhone.CANCEL_ECM_TIMER);
        }

        // If the call is to an emergency number and the carrier does not support video emergency
        // calls, dial as an audio-only call.
        if (isEmergencyNumber && VideoProfile.isVideo(videoState) &&
                !mAllowEmergencyVideoCalls) {
            loge("dial: carrier does not support video emergency calls; downgrade to audio-only");
            videoState = VideoProfile.STATE_AUDIO_ONLY;
        }

        boolean holdBeforeDial = false;

        // The new call must be assigned to the foreground call.
        // That call must be idle, so place anything that's
        // there on hold
        if (mForegroundCall.getState() == ImsPhoneCall.State.ACTIVE) {
            if (mBackgroundCall.getState() != ImsPhoneCall.State.IDLE) {
                //we should have failed in !canDial() above before we get here
                throw new CallStateException("cannot dial in current state");
            }
            // foreground call is empty for the newly dialed connection
            holdBeforeDial = true;
            // Cache the video state for pending MO call.
            mPendingCallVideoState = videoState;
            mPendingIntentExtras = intentExtras;
            switchWaitingOrHoldingAndActive();
        }

        ImsPhoneCall.State fgState = ImsPhoneCall.State.IDLE;
        ImsPhoneCall.State bgState = ImsPhoneCall.State.IDLE;

        mClirMode = clirMode;

        synchronized (mSyncHold) {
            if (holdBeforeDial) {
                fgState = mForegroundCall.getState();
                bgState = mBackgroundCall.getState();

                //holding foreground call failed
                if (fgState == ImsPhoneCall.State.ACTIVE) {
                    throw new CallStateException("cannot dial in current state");
                }

                //holding foreground call succeeded
                if (bgState == ImsPhoneCall.State.HOLDING) {
                    holdBeforeDial = false;
                }
            }

            /// M: @{
            //mPendingMO = new ImsPhoneConnection(mPhone,
            //       checkForTestEmergencyNumber(dialString), this, mForegroundCall,
            //        isEmergencyNumber);
            mPendingMO = makeImsPhoneConnectionForMO(dialString, isEmergencyNumber);
            /// @}
            mPendingMO.setVideoState(videoState);
        }
        addConnection(mPendingMO);

        /// M: @{
        logDebugMessagesWithOpFormat("CC", "Dial", mPendingMO, "");
        logDebugMessagesWithDumpFormat("CC", mPendingMO, "");
        /// @}

        if (!holdBeforeDial) {
            if ((!isPhoneInEcmMode) || (isPhoneInEcmMode && isEmergencyNumber)) {
                dialInternal(mPendingMO, clirMode, videoState, intentExtras);
            } else {
                try {
                    getEcbmInterface().exitEmergencyCallbackMode();
                } catch (ImsException e) {
                    e.printStackTrace();
                    throw new CallStateException("service not available");
                }
                mPhone.setOnEcbModeExitResponse(this, EVENT_EXIT_ECM_RESPONSE_CDMA, null);
                pendingCallClirMode = clirMode;
                mPendingCallVideoState = videoState;
                pendingCallInEcm = true;
            }
        }

        updatePhoneState();
        mPhone.notifyPreciseCallStateChanged();
        /// M: @{
        switchWfcModeIfRequired(mImsManager, isVowifiEnabled(), isEmergencyNumber);
        /// @}

        return mPendingMO;
    }

    boolean isImsServiceReady() {
        if (mImsManager == null) {
            return false;
        }

        return mImsManager.isServiceAvailable();
    }

    /**
     * Caches frequently used carrier configuration items locally.
     *
     * @param subId The sub id.
     */
    protected void cacheCarrierConfiguration(int subId) {
        CarrierConfigManager carrierConfigManager = (CarrierConfigManager)
                mPhone.getContext().getSystemService(Context.CARRIER_CONFIG_SERVICE);
        if (carrierConfigManager == null) {
            loge("cacheCarrierConfiguration: No carrier config service found.");
            return;
        }

        PersistableBundle carrierConfig = carrierConfigManager.getConfigForSubId(subId);
        if (carrierConfig == null) {
            loge("cacheCarrierConfiguration: Empty carrier config.");
            return;
        }

        mAllowEmergencyVideoCalls =
                carrierConfig.getBoolean(CarrierConfigManager.KEY_ALLOW_EMERGENCY_VIDEO_CALLS_BOOL);
        mTreatDowngradedVideoCallsAsVideoCalls =
                carrierConfig.getBoolean(
                        CarrierConfigManager.KEY_TREAT_DOWNGRADED_VIDEO_CALLS_AS_VIDEO_CALLS_BOOL);
        mDropVideoCallWhenAnsweringAudioCall =
                carrierConfig.getBoolean(
                        CarrierConfigManager.KEY_DROP_VIDEO_CALL_WHEN_ANSWERING_AUDIO_CALL_BOOL);
        mAllowAddCallDuringVideoCall =
                carrierConfig.getBoolean(
                        CarrierConfigManager.KEY_ALLOW_ADD_CALL_DURING_VIDEO_CALL_BOOL);
        mNotifyVtHandoverToWifiFail = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_NOTIFY_VT_HANDOVER_TO_WIFI_FAILURE_BOOL);
        mSupportDowngradeVtToAudio = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_SUPPORT_DOWNGRADE_VT_TO_AUDIO_BOOL);
        mNotifyHandoverVideoFromWifiToLTE = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_NOTIFY_HANDOVER_VIDEO_FROM_WIFI_TO_LTE_BOOL);
        mIgnoreDataEnabledChangedForVideoCalls = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_IGNORE_DATA_ENABLED_CHANGED_FOR_VIDEO_CALLS);
        mIsViLteDataMetered = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_VILTE_DATA_IS_METERED_BOOL);
        mSupportPauseVideo = carrierConfig.getBoolean(
                CarrierConfigManager.KEY_SUPPORT_PAUSE_IMS_VIDEO_CALLS_BOOL);

        String[] mappings = carrierConfig
                .getStringArray(CarrierConfigManager.KEY_IMS_REASONINFO_MAPPING_STRING_ARRAY);
        if (mappings != null && mappings.length > 0) {
            for (String mapping : mappings) {
                String[] values = mapping.split(Pattern.quote("|"));
                if (values.length != 3) {
                    continue;
                }

                try {
                    Integer fromCode;
                    if (values[0].equals("*")) {
                        fromCode = null;
                    } else {
                        fromCode = Integer.parseInt(values[0]);
                    }
                    String message = values[1];
                    int toCode = Integer.parseInt(values[2]);

                    addReasonCodeRemapping(fromCode, message, toCode);
                    log("Loaded ImsReasonInfo mapping : fromCode = " +
                            fromCode == null ? "any" : fromCode + " ; message = " +
                            message + " ; toCode = " + toCode);
                } catch (NumberFormatException nfe) {
                    loge("Invalid ImsReasonInfo mapping found: " + mapping);
                }
            }
        } else {
            log("No carrier ImsReasonInfo mappings defined.");
        }
    }

    protected void handleEcmTimer(int action) {
        mPhone.handleTimerInEmergencyCallbackMode(action);
        switch (action) {
            case ImsPhone.CANCEL_ECM_TIMER:
                break;
            case ImsPhone.RESTART_ECM_TIMER:
                break;
            default:
                log("handleEcmTimer, unsupported action " + action);
        }
    }

    protected void dialInternal(ImsPhoneConnection conn, int clirMode, int videoState,
            Bundle intentExtras) {

        if (conn == null) {
            return;
        }

        if (conn.getAddress()== null || conn.getAddress().length() == 0
                || conn.getAddress().indexOf(PhoneNumberUtils.WILD) >= 0) {
            // Phone number is invalid
            conn.setDisconnectCause(DisconnectCause.INVALID_NUMBER);
            sendEmptyMessageDelayed(EVENT_HANGUP_PENDINGMO, TIMEOUT_HANGUP_PENDINGMO);
            return;
        }

        // Always unmute when initiating a new call
        setMute(false);
        int serviceType = mPhoneNumberUtilsProxy.isEmergencyNumber(conn.getAddress()) ?
                ImsCallProfile.SERVICE_TYPE_EMERGENCY : ImsCallProfile.SERVICE_TYPE_NORMAL;
        int callType = ImsCallProfile.getCallTypeFromVideoState(videoState);
        //TODO(vt): Is this sufficient?  At what point do we know the video state of the call?
        conn.setVideoState(videoState);

        try {
            String[] callees = new String[] { conn.getAddress() };
            ImsCallProfile profile = mImsManager.createCallProfile(mServiceId,
                    serviceType, callType);
            profile.setCallExtraInt(ImsCallProfile.EXTRA_OIR, clirMode);

            // Translate call subject intent-extra from Telecom-specific extra key to the
            // ImsCallProfile key.
            if (intentExtras != null) {
                if (intentExtras.containsKey(android.telecom.TelecomManager.EXTRA_CALL_SUBJECT)) {
                    intentExtras.putString(ImsCallProfile.EXTRA_DISPLAY_TEXT,
                            cleanseInstantLetteringMessage(intentExtras.getString(
                                    android.telecom.TelecomManager.EXTRA_CALL_SUBJECT))
                    );
                }

                if (intentExtras.containsKey(ImsCallProfile.EXTRA_IS_CALL_PULL)) {
                    profile.mCallExtras.putBoolean(ImsCallProfile.EXTRA_IS_CALL_PULL,
                            intentExtras.getBoolean(ImsCallProfile.EXTRA_IS_CALL_PULL));
                    int dialogId = intentExtras.getInt(
                            ImsExternalCallTracker.EXTRA_IMS_EXTERNAL_CALL_ID);
                    conn.setIsPulledCall(true);
                    conn.setPulledDialogId(dialogId);
                }

                // Pack the OEM-specific call extras.
                profile.mCallExtras.putBundle(ImsCallProfile.EXTRA_OEM_EXTRAS, intentExtras);

                // NOTE: Extras to be sent over the network are packed into the
                // intentExtras individually, with uniquely defined keys.
                // These key-value pairs are processed by IMS Service before
                // being sent to the lower layers/to the network.
            }

            ImsCall imsCall = mImsManager.makeCall(mServiceId, profile,
                    callees, mImsCallListener);
            conn.setImsCall(imsCall);

            mMetrics.writeOnImsCallStart(mPhone.getPhoneId(),
                    imsCall.getSession());

            setVideoCallProvider(conn, imsCall);
            conn.setAllowAddCallDuringVideoCall(mAllowAddCallDuringVideoCall);
        } catch (ImsException e) {
            loge("dialInternal : " + e);
            conn.setDisconnectCause(DisconnectCause.ERROR_UNSPECIFIED);
            sendEmptyMessageDelayed(EVENT_HANGUP_PENDINGMO, TIMEOUT_HANGUP_PENDINGMO);
            retryGetImsService();
        } catch (RemoteException e) {
        }
    }

    /**
     * Accepts a call with the specified video state.  The video state is the video state that the
     * user has agreed upon in the InCall UI.
     *
     * @param videoState The video State
     * @throws CallStateException
     */
    public void acceptCall (int videoState) throws CallStateException {
        if (DBG) log("acceptCall");

        if (mForegroundCall.getState().isAlive()
                && mBackgroundCall.getState().isAlive()) {
            throw new CallStateException("cannot accept call");
        }

        if ((mRingingCall.getState() == ImsPhoneCall.State.WAITING)
                && mForegroundCall.getState().isAlive()) {
            setMute(false);

            boolean answeringWillDisconnect = false;
            ImsCall activeCall = mForegroundCall.getImsCall();
            ImsCall ringingCall = mRingingCall.getImsCall();
            if (mForegroundCall.hasConnections() && mRingingCall.hasConnections()) {
                answeringWillDisconnect =
                        shouldDisconnectActiveCallOnAnswer(activeCall, ringingCall);
            }

            // Cache video state for pending MT call.
            mPendingCallVideoState = videoState;

            if (answeringWillDisconnect) {
                // We need to disconnect the foreground call before answering the background call.
                mForegroundCall.hangup();
                try {
                    ringingCall.accept(ImsCallProfile.getCallTypeFromVideoState(videoState));
                } catch (ImsException e) {
                    throw new CallStateException("cannot accept call");
                }
            } else {
                switchWaitingOrHoldingAndActive();
            }
        } else if (mRingingCall.getState().isRinging()) {
            if (DBG) log("acceptCall: incoming...");
            // Always unmute when answering a new call
            setMute(false);
            try {
                ImsCall imsCall = mRingingCall.getImsCall();
                if (imsCall != null) {
                    imsCall.accept(ImsCallProfile.getCallTypeFromVideoState(videoState));
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_ACCEPT);
                } else {
                    throw new CallStateException("no valid ims call");
                }
            } catch (ImsException e) {
                throw new CallStateException("cannot accept call");
            }
        } else {
            throw new CallStateException("phone not ringing");
        }
    }

    public void rejectCall () throws CallStateException {
        if (DBG) log("rejectCall");

        if (mRingingCall.getState().isRinging()) {
            hangup(mRingingCall);
        } else {
            throw new CallStateException("phone not ringing");
        }
    }


    private void switchAfterConferenceSuccess() {
        if (DBG) log("switchAfterConferenceSuccess fg =" + mForegroundCall.getState() +
                ", bg = " + mBackgroundCall.getState());

        if (mBackgroundCall.getState() == ImsPhoneCall.State.HOLDING) {
            log("switchAfterConferenceSuccess");
            mForegroundCall.switchWith(mBackgroundCall);
        }
    }

    public void switchWaitingOrHoldingAndActive() throws CallStateException {
        if (DBG) log("switchWaitingOrHoldingAndActive");

        if (mRingingCall.getState() == ImsPhoneCall.State.INCOMING) {
            throw new CallStateException("cannot be in the incoming state");
        }

        if (mForegroundCall.getState() == ImsPhoneCall.State.ACTIVE) {
            ImsCall imsCall = mForegroundCall.getImsCall();
            if (imsCall == null) {
                throw new CallStateException("no ims call");
            }

            // Swap the ImsCalls pointed to by the foreground and background ImsPhoneCalls.
            // If hold or resume later fails, we will swap them back.
            boolean switchingWithWaitingCall = !mBackgroundCall.getState().isAlive() &&
                    mRingingCall != null &&
                    mRingingCall.getState() == ImsPhoneCall.State.WAITING;

            mSwitchingFgAndBgCalls = true;
            if (switchingWithWaitingCall) {
                mCallExpectedToResume = mRingingCall.getImsCall();
            } else {
                mCallExpectedToResume = mBackgroundCall.getImsCall();
            }
            mForegroundCall.switchWith(mBackgroundCall);

            // Hold the foreground call; once the foreground call is held, the background call will
            // be resumed.
            try {
                imsCall.hold();
                mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                        ImsCommand.IMS_CMD_HOLD);

                // If there is no background call to resume, then don't expect there to be a switch.
                if (mCallExpectedToResume == null) {
                    log("mCallExpectedToResume is null");
                    mSwitchingFgAndBgCalls = false;
                }
            } catch (ImsException e) {
                mForegroundCall.switchWith(mBackgroundCall);
                /// M:
                resetFlagWhenSwitchFailed();

                throw new CallStateException(e.getMessage());
            }
        } else if (mBackgroundCall.getState() == ImsPhoneCall.State.HOLDING) {
            resumeWaitingOrHolding();
        }
    }

    public void
    conference() {
        ImsCall fgImsCall = mForegroundCall.getImsCall();
        if (fgImsCall == null) {
            log("conference no foreground ims call");
            return;
        }

        ImsCall bgImsCall = mBackgroundCall.getImsCall();
        if (bgImsCall == null) {
            log("conference no background ims call");
            return;
        }

        if (fgImsCall.isCallSessionMergePending()) {
            log("conference: skip; foreground call already in process of merging.");
            return;
        }

        if (bgImsCall.isCallSessionMergePending()) {
            log("conference: skip; background call already in process of merging.");
            return;
        }

        // Keep track of the connect time of the earliest call so that it can be set on the
        // {@code ImsConference} when it is created.
        long foregroundConnectTime = mForegroundCall.getEarliestConnectTime();
        long backgroundConnectTime = mBackgroundCall.getEarliestConnectTime();
        long conferenceConnectTime;
        if (foregroundConnectTime > 0 && backgroundConnectTime > 0) {
            conferenceConnectTime = Math.min(mForegroundCall.getEarliestConnectTime(),
                    mBackgroundCall.getEarliestConnectTime());
            log("conference - using connect time = " + conferenceConnectTime);
        } else if (foregroundConnectTime > 0) {
            log("conference - bg call connect time is 0; using fg = " + foregroundConnectTime);
            conferenceConnectTime = foregroundConnectTime;
        } else {
            log("conference - fg call connect time is 0; using bg = " + backgroundConnectTime);
            conferenceConnectTime = backgroundConnectTime;
        }

        String foregroundId = "";
        ImsPhoneConnection foregroundConnection = mForegroundCall.getFirstConnection();
        if (foregroundConnection != null) {
            foregroundConnection.setConferenceConnectTime(conferenceConnectTime);
            foregroundConnection.handleMergeStart();
            foregroundId = foregroundConnection.getTelecomCallId();
        }
        String backgroundId = "";
        ImsPhoneConnection backgroundConnection = findConnection(bgImsCall);
        if (backgroundConnection != null) {
            backgroundConnection.handleMergeStart();
            backgroundId = backgroundConnection.getTelecomCallId();
        }
        log("conference: fgCallId=" + foregroundId + ", bgCallId=" + backgroundId);

        try {
            fgImsCall.merge(bgImsCall);
        } catch (ImsException e) {
            log("conference " + e.getMessage());
            /// M: Fix google issue, clear the fg and bg state if conference failed. @{
            if (foregroundConnection != null) {
                foregroundConnection.handleMergeComplete();
            }
            if (backgroundConnection != null) {
                backgroundConnection.handleMergeComplete();
            }
            /// @}
        }
    }

    public void
    explicitCallTransfer() {
        //TODO : implement
    }

    public void
    clearDisconnected() {
        if (DBG) log("clearDisconnected");

        internalClearDisconnected();

        updatePhoneState();
        mPhone.notifyPreciseCallStateChanged();
    }

    public boolean
    canConference() {
        return mForegroundCall.getState() == ImsPhoneCall.State.ACTIVE
            && mBackgroundCall.getState() == ImsPhoneCall.State.HOLDING
            && !mBackgroundCall.isFull()
            && !mForegroundCall.isFull();
    }

    public boolean canDial() {
        boolean ret;
        String disableCall = SystemProperties.get(
                TelephonyProperties.PROPERTY_DISABLE_CALL, "false");

        ret = mPendingMO == null
                && !mRingingCall.isRinging()
                && !disableCall.equals("true")
                && (!mForegroundCall.getState().isAlive()
                        || !mBackgroundCall.getState().isAlive());

        return ret;
    }

    public boolean
    canTransfer() {
        return mForegroundCall.getState() == ImsPhoneCall.State.ACTIVE
            && mBackgroundCall.getState() == ImsPhoneCall.State.HOLDING;
    }

    //***** Private Instance Methods

    private void
    internalClearDisconnected() {
        mRingingCall.clearDisconnected();
        mForegroundCall.clearDisconnected();
        mBackgroundCall.clearDisconnected();
        mHandoverCall.clearDisconnected();
    }

    protected void
    updatePhoneState() {
        PhoneConstants.State oldState = mState;

        boolean isPendingMOIdle = mPendingMO == null || !mPendingMO.getState().isAlive();

        if (mRingingCall.isRinging()) {
            mState = PhoneConstants.State.RINGING;
        } else if (!isPendingMOIdle || !mForegroundCall.isIdle() || !mBackgroundCall.isIdle()) {
            // There is a non-idle call, so we're off the hook.
            mState = PhoneConstants.State.OFFHOOK;
        } else {
            mState = PhoneConstants.State.IDLE;
        }

        if (mState == PhoneConstants.State.IDLE && oldState != mState) {
            /// M: Notify the call state change with specific AsyncResult. @{
            //mVoiceCallEndedRegistrants.notifyRegistrants(
            //        new AsyncResult(null, null, null));
            mVoiceCallEndedRegistrants.notifyRegistrants(
                    getCallStateChangeAsyncResult());
            /// @}
        } else if (oldState == PhoneConstants.State.IDLE && oldState != mState) {
            /// M: Notify the call state change with specific AsyncResult. @{
            //mVoiceCallStartedRegistrants.notifyRegistrants (
            //        new AsyncResult(null, null, null));
            mVoiceCallStartedRegistrants.notifyRegistrants (
                    getCallStateChangeAsyncResult());
            /// @}
        }

        if (DBG) {
            log("updatePhoneState pendingMo = " + (mPendingMO == null ? "null"
                    : mPendingMO.getState()) + ", fg= " + mForegroundCall.getState() + "("
                    + mForegroundCall.getConnections().size() + "), bg= " + mBackgroundCall
                    .getState() + "(" + mBackgroundCall.getConnections().size() + ")");
            log("updatePhoneState oldState=" + oldState + ", newState=" + mState);
        }

        if (mState != oldState) {
            mPhone.notifyPhoneStateChanged();
            mMetrics.writePhoneState(mPhone.getPhoneId(), mState);
            notifyPhoneStateChanged(oldState, mState);
        }
    }

    private void
    handleRadioNotAvailable() {
        // handlePollCalls will clear out its
        // call list when it gets the CommandException
        // error result from this
        pollCallsWhenSafe();
    }

    private void
    dumpState() {
        List l;

        log("Phone State:" + mState);

        log("Ringing call: " + mRingingCall.toString());

        l = mRingingCall.getConnections();
        for (int i = 0, s = l.size(); i < s; i++) {
            log(l.get(i).toString());
        }

        log("Foreground call: " + mForegroundCall.toString());

        l = mForegroundCall.getConnections();
        for (int i = 0, s = l.size(); i < s; i++) {
            log(l.get(i).toString());
        }

        log("Background call: " + mBackgroundCall.toString());

        l = mBackgroundCall.getConnections();
        for (int i = 0, s = l.size(); i < s; i++) {
            log(l.get(i).toString());
        }

    }

    //***** Called from ImsPhone
    /**
     * Set the TTY mode. This is the actual tty mode (varies depending on peripheral status)
     */
    public void setTtyMode(int ttyMode) {
        if (mImsManager == null) {
            Log.w(LOG_TAG, "ImsManager is null when setting TTY mode");
            return;
        }

        try {
            mImsManager.setTtyMode(ttyMode);
        } catch (ImsException e) {
            loge("setTtyMode : " + e);
            retryGetImsService();
        }
    }

    /**
     * Sets the UI TTY mode. This is the preferred TTY mode that the user sets in the call
     * settings screen.
     */
    public void setUiTTYMode(int uiTtyMode, Message onComplete) {
        if (mImsManager == null) {
            mPhone.sendErrorResponse(onComplete, getImsManagerIsNullException());
            return;
        }

        try {
            mImsManager.setUiTTYMode(mPhone.getContext(), uiTtyMode, onComplete);
        } catch (ImsException e) {
            loge("setUITTYMode : " + e);
            mPhone.sendErrorResponse(onComplete, e);
            retryGetImsService();
        }
    }

    public void setMute(boolean mute) {
        mDesiredMute = mute;
        mForegroundCall.setMute(mute);
    }

    public boolean getMute() {
        return mDesiredMute;
    }

    public void sendDtmf(char c, Message result) {
        if (DBG) log("sendDtmf");

        ImsCall imscall = mForegroundCall.getImsCall();
        if (imscall != null) {
            imscall.sendDtmf(c, result);
        }
    }

    public void
    startDtmf(char c) {
        if (DBG) log("startDtmf");

        ImsCall imscall = mForegroundCall.getImsCall();
        if (imscall != null) {
            imscall.startDtmf(c);
        } else {
            loge("startDtmf : no foreground call");
        }
    }

    public void
    stopDtmf() {
        if (DBG) log("stopDtmf");

        ImsCall imscall = mForegroundCall.getImsCall();
        if (imscall != null) {
            imscall.stopDtmf();
        } else {
            loge("stopDtmf : no foreground call");
        }
    }

    //***** Called from ImsPhoneConnection

    public void hangup (ImsPhoneConnection conn) throws CallStateException {
        if (DBG) log("hangup connection");

        if (conn.getOwner() != this) {
            throw new CallStateException ("ImsPhoneConnection " + conn
                    + "does not belong to ImsPhoneCallTracker " + this);
        }

        hangup(conn.getCall());
    }

    //***** Called from ImsPhoneCall

    public void hangup (ImsPhoneCall call) throws CallStateException {
        if (DBG) log("hangup call");

        if (call.getConnections().size() == 0) {
            throw new CallStateException("no connections");
        }

        ImsCall imsCall = call.getImsCall();
        boolean rejectCall = false;

        if (call == mRingingCall) {
            if (Phone.DEBUG_PHONE) log("(ringing) hangup incoming");
            rejectCall = true;
        } else if (call == mForegroundCall) {
            if (call.isDialingOrAlerting()) {
                if (Phone.DEBUG_PHONE) {
                    log("(foregnd) hangup dialing or alerting...");
                }
            } else {
                if (Phone.DEBUG_PHONE) {
                    log("(foregnd) hangup foreground");
                }
                //held call will be resumed by onCallTerminated
            }
        } else if (call == mBackgroundCall) {
            if (Phone.DEBUG_PHONE) {
                log("(backgnd) hangup waiting or background");
            }
        } else {
            throw new CallStateException ("ImsPhoneCall " + call +
                    "does not belong to ImsPhoneCallTracker " + this);
        }

        call.onHangupLocal();

        try {
            if (imsCall != null) {
                if (rejectCall) {
                    imsCall.reject(ImsReasonInfo.CODE_USER_DECLINE);
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_REJECT);
                } else {
                    imsCall.terminate(ImsReasonInfo.CODE_USER_TERMINATED);
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_TERMINATE);
                }
            } else if (mPendingMO != null && call == mForegroundCall) {
                // is holding a foreground call
                mPendingMO.update(null, ImsPhoneCall.State.DISCONNECTED);
                mPendingMO.onDisconnect();
                removeConnection(mPendingMO);
                mPendingMO = null;
                updatePhoneState();
                removeMessages(EVENT_DIAL_PENDINGMO);
            }
        } catch (ImsException e) {
            throw new CallStateException(e.getMessage());
        }

        mPhone.notifyPreciseCallStateChanged();
    }

    protected void callEndCleanupHandOverCallIfAny() {
        if (mHandoverCall.mConnections.size() > 0) {
            if (DBG) log("callEndCleanupHandOverCallIfAny, mHandoverCall.mConnections="
                    + mHandoverCall.mConnections);
            mHandoverCall.mConnections.clear();
            mConnections.clear();
            mState = PhoneConstants.State.IDLE;
        }
    }

    /* package */
    void resumeWaitingOrHolding() throws CallStateException {
        if (DBG) log("resumeWaitingOrHolding");

        try {
            if (mForegroundCall.getState().isAlive()) {
                //resume foreground call after holding background call
                //they were switched before holding
                ImsCall imsCall = mForegroundCall.getImsCall();
                if (imsCall != null) {
                    imsCall.resume();
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_RESUME);
                }
            } else if (mRingingCall.getState() == ImsPhoneCall.State.WAITING) {
                /// M: Skip this accept since there is a resume operation not finished. @{
                if (hasPendingResumeRequest()) {
                    log("there is a pending resume background request, ignore accept()!");
                    return;
                }
                /// @}
                //accept waiting call after holding background call
                ImsCall imsCall = mRingingCall.getImsCall();
                if (imsCall != null) {
                    imsCall.accept(
                        ImsCallProfile.getCallTypeFromVideoState(mPendingCallVideoState));
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_ACCEPT);
                }
            } else {
                //Just resume background call.
                //To distinguish resuming call with swapping calls
                //we do not switch calls.here
                //ImsPhoneConnection.update will chnage the parent when completed
                ImsCall imsCall = mBackgroundCall.getImsCall();
                if (imsCall != null) {
                    imsCall.resume();
                    /// M: ALPS02023277. @{
                    /// Enable this flag to block the operation of accepting call.
                    setPendingResumeRequest(true);
                    /// @}
                    mMetrics.writeOnImsCommand(mPhone.getPhoneId(), imsCall.getSession(),
                            ImsCommand.IMS_CMD_RESUME);
                }
            }
        } catch (ImsException e) {
            throw new CallStateException(e.getMessage());
        }
    }

    public void sendUSSD (String ussdString, Message response) {
        if (DBG) log("sendUSSD");

        try {
            if (mUssdSession != null) {
                mUssdSession.sendUssd(ussdString);
                AsyncResult.forMessage(response, null, null);
                response.sendToTarget();
                return;
            }

            if (mImsManager == null) {
                mPhone.sendErrorResponse(response, getImsManagerIsNullException());
                return;
            }

            String[] callees = new String[] { ussdString };
            ImsCallProfile profile = mImsManager.createCallProfile(mServiceId,
                    ImsCallProfile.SERVICE_TYPE_NORMAL, ImsCallProfile.CALL_TYPE_VOICE);
            profile.setCallExtraInt(ImsCallProfile.EXTRA_DIALSTRING,
                    ImsCallProfile.DIALSTRING_USSD);

            mUssdSession = mImsManager.makeCall(mServiceId, profile,
                    callees, mImsUssdListener);
        } catch (ImsException e) {
            loge("sendUSSD : " + e);
            mPhone.sendErrorResponse(response, e);
            retryGetImsService();
        }
    }

    public void cancelUSSD() {
        if (mUssdSession == null) return;

        try {
            mUssdSession.terminate(ImsReasonInfo.CODE_USER_TERMINATED);
        } catch (ImsException e) {
        }

    }

    protected synchronized ImsPhoneConnection findConnection(final ImsCall imsCall) {
        for (ImsPhoneConnection conn : mConnections) {
            if (conn.getImsCall() == imsCall) {
                return conn;
            }
        }
        return null;
    }

    protected synchronized void removeConnection(ImsPhoneConnection conn) {
        mConnections.remove(conn);
        // If not emergency call is remaining, notify emergency call registrants
        if (mIsInEmergencyCall) {
            boolean isEmergencyCallInList = false;
            // if no emergency calls pending, set this to false
            for (ImsPhoneConnection imsPhoneConnection : mConnections) {
                if (imsPhoneConnection != null && imsPhoneConnection.isEmergency() == true) {
                    isEmergencyCallInList = true;
                    break;
                }
            }

            if (!isEmergencyCallInList) {
                mIsInEmergencyCall = false;
                mPhone.sendEmergencyCallStateChange(false);
                //M: RTT
                startRttEmcGuardTimer();
            }
        }
    }

    protected synchronized void addConnection(ImsPhoneConnection conn) {
        mConnections.add(conn);
        if (conn.isEmergency()) {
            mIsInEmergencyCall = true;
            mPhone.sendEmergencyCallStateChange(true);
        }
    }

    protected void processCallStateChange(ImsCall imsCall, ImsPhoneCall.State state, int cause) {
        if (DBG) log("processCallStateChange " + imsCall + " state=" + state + " cause=" + cause);
        // This method is called on onCallUpdate() where there is not necessarily a call state
        // change. In these situations, we'll ignore the state related updates and only process
        // the change in media capabilities (as expected).  The default is to not ignore state
        // changes so we do not change existing behavior.
        processCallStateChange(imsCall, state, cause, false /* do not ignore state update */);
    }

    protected void processCallStateChange(ImsCall imsCall, ImsPhoneCall.State state, int cause,
            boolean ignoreState) {
        if (DBG) {
            log("processCallStateChange state=" + state + " cause=" + cause
                    + " ignoreState=" + ignoreState);
        }

        if (imsCall == null) return;

        boolean changed = false;
        ImsPhoneConnection conn = findConnection(imsCall);

        if (conn == null) {
            // TODO : what should be done?
            return;
        }

        // processCallStateChange is triggered for onCallUpdated as well.
        // onCallUpdated should not modify the state of the call
        // It should modify only other capabilities of call through updateMediaCapabilities
        // State updates will be triggered through individual callbacks
        // i.e. onCallHeld, onCallResume, etc and conn.update will be responsible for the update
        conn.updateMediaCapabilities(imsCall);
        if (ignoreState) {
            conn.updateAddressDisplay(imsCall);
            conn.updateExtras(imsCall);

            maybeSetVideoCallProvider(conn, imsCall);
            return;
        }

        changed = conn.update(imsCall, state);
        if (state == ImsPhoneCall.State.DISCONNECTED) {
            changed = conn.onDisconnect(cause) || changed;
            //detach the disconnected connections
            conn.getCall().detach(conn);
            removeConnection(conn);
        }

        if (changed) {
            if (conn.getCall() == mHandoverCall) return;
            updatePhoneState();

            //M: for RTT
            checkRttCallType();

            mPhone.notifyPreciseCallStateChanged();
        }
    }

    private void maybeSetVideoCallProvider(ImsPhoneConnection conn, ImsCall imsCall) {
        android.telecom.Connection.VideoProvider connVideoProvider = conn.getVideoProvider();
        if (connVideoProvider != null || imsCall.getCallSession().getVideoCallProvider() == null) {
            return;
        }

        try {
            setVideoCallProvider(conn, imsCall);
        } catch (RemoteException e) {
            loge("maybeSetVideoCallProvider: exception " + e);
        }
    }

    /**
     * Adds a reason code remapping, for test purposes.
     *
     * @param fromCode The from code, or {@code null} if all.
     * @param message The message to map.
     * @param toCode The code to remap to.
     */
    @VisibleForTesting
    public void addReasonCodeRemapping(Integer fromCode, String message, Integer toCode) {
        mImsReasonCodeMap.put(new Pair<>(fromCode, message), toCode);
    }

    /**
     * Returns the {@link ImsReasonInfo#getCode()}, potentially remapping to a new value based on
     * the {@link ImsReasonInfo#getCode()} and {@link ImsReasonInfo#getExtraMessage()}.
     *
     * See {@link #mImsReasonCodeMap}.
     *
     * @param reasonInfo The {@link ImsReasonInfo}.
     * @return The remapped code.
     */
    @VisibleForTesting
    public int maybeRemapReasonCode(ImsReasonInfo reasonInfo) {
        int code = reasonInfo.getCode();

        Pair<Integer, String> toCheck = new Pair<>(code, reasonInfo.getExtraMessage());
        Pair<Integer, String> wildcardToCheck = new Pair<>(null, reasonInfo.getExtraMessage());
        if (mImsReasonCodeMap.containsKey(toCheck)) {
            int toCode = mImsReasonCodeMap.get(toCheck);

            log("maybeRemapReasonCode : fromCode = " + reasonInfo.getCode() + " ; message = "
                    + reasonInfo.getExtraMessage() + " ; toCode = " + toCode);
            return toCode;
        } else if (mImsReasonCodeMap.containsKey(wildcardToCheck)) {
            // Handle the case where a wildcard is specified for the fromCode; in this case we will
            // match without caring about the fromCode.
            int toCode = mImsReasonCodeMap.get(wildcardToCheck);

            log("maybeRemapReasonCode : fromCode(wildcard) = " + reasonInfo.getCode() +
                    " ; message = " + reasonInfo.getExtraMessage() + " ; toCode = " + toCode);
            return toCode;
        }
        return code;
    }

    /**
     * Maps an {@link ImsReasonInfo} reason code to a {@link DisconnectCause} cause code.
     * The {@link Call.State} provided is the state of the call prior to disconnection.
     * @param reasonInfo the {@link ImsReasonInfo} for the disconnection.
     * @param callState The {@link Call.State} prior to disconnection.
     * @return The {@link DisconnectCause} code.
     */
    @VisibleForTesting
    public int getDisconnectCauseFromReasonInfo(ImsReasonInfo reasonInfo, Call.State callState) {
        int cause = DisconnectCause.ERROR_UNSPECIFIED;

        int code = maybeRemapReasonCode(reasonInfo);
        switch (code) {
            case ImsReasonInfo.CODE_SIP_BAD_ADDRESS:
            case ImsReasonInfo.CODE_SIP_NOT_REACHABLE:
                return DisconnectCause.NUMBER_UNREACHABLE;

            case ImsReasonInfo.CODE_SIP_BUSY:
                return DisconnectCause.BUSY;

            case ImsReasonInfo.CODE_USER_TERMINATED:
                return DisconnectCause.LOCAL;

            case ImsReasonInfo.CODE_LOCAL_ENDED_BY_CONFERENCE_MERGE:
                return DisconnectCause.IMS_MERGED_SUCCESSFULLY;

            case ImsReasonInfo.CODE_LOCAL_CALL_DECLINE:
            case ImsReasonInfo.CODE_REMOTE_CALL_DECLINE:
                // If the call has been declined locally (on this device), or on remotely (on
                // another device using multiendpoint functionality), mark it as rejected.
                return DisconnectCause.INCOMING_REJECTED;

            case ImsReasonInfo.CODE_USER_TERMINATED_BY_REMOTE:
                return DisconnectCause.NORMAL;

            case ImsReasonInfo.CODE_SIP_FORBIDDEN:
                return DisconnectCause.SERVER_ERROR;

            case ImsReasonInfo.CODE_SIP_REDIRECTED:
            case ImsReasonInfo.CODE_SIP_BAD_REQUEST:
            case ImsReasonInfo.CODE_SIP_NOT_ACCEPTABLE:
            case ImsReasonInfo.CODE_SIP_USER_REJECTED:
            case ImsReasonInfo.CODE_SIP_GLOBAL_ERROR:
                return DisconnectCause.SERVER_ERROR;

            case ImsReasonInfo.CODE_SIP_SERVICE_UNAVAILABLE:
            case ImsReasonInfo.CODE_SIP_NOT_FOUND:
            case ImsReasonInfo.CODE_SIP_SERVER_ERROR:
                return DisconnectCause.SERVER_UNREACHABLE;

            case ImsReasonInfo.CODE_LOCAL_NETWORK_ROAMING:
            case ImsReasonInfo.CODE_LOCAL_NETWORK_IP_CHANGED:
            case ImsReasonInfo.CODE_LOCAL_IMS_SERVICE_DOWN:
            case ImsReasonInfo.CODE_LOCAL_SERVICE_UNAVAILABLE:
            case ImsReasonInfo.CODE_LOCAL_NOT_REGISTERED:
            case ImsReasonInfo.CODE_LOCAL_NETWORK_NO_LTE_COVERAGE:
            case ImsReasonInfo.CODE_LOCAL_NETWORK_NO_SERVICE:
            case ImsReasonInfo.CODE_LOCAL_CALL_VCC_ON_PROGRESSING:
                return DisconnectCause.OUT_OF_SERVICE;

            case ImsReasonInfo.CODE_SIP_REQUEST_TIMEOUT:
            case ImsReasonInfo.CODE_TIMEOUT_1XX_WAITING:
            case ImsReasonInfo.CODE_TIMEOUT_NO_ANSWER:
            case ImsReasonInfo.CODE_TIMEOUT_NO_ANSWER_CALL_UPDATE:
                return DisconnectCause.TIMED_OUT;

            case ImsReasonInfo.CODE_LOCAL_POWER_OFF:
                return DisconnectCause.POWER_OFF;

            case ImsReasonInfo.CODE_LOCAL_LOW_BATTERY:
            case ImsReasonInfo.CODE_LOW_BATTERY: {
                if (callState == Call.State.DIALING) {
                    return DisconnectCause.DIAL_LOW_BATTERY;
                } else {
                    return DisconnectCause.LOW_BATTERY;
                }
            }

            case ImsReasonInfo.CODE_FDN_BLOCKED:
                return DisconnectCause.FDN_BLOCKED;

            case ImsReasonInfo.CODE_IMEI_NOT_ACCEPTED:
                return DisconnectCause.IMEI_NOT_ACCEPTED;

            case ImsReasonInfo.CODE_ANSWERED_ELSEWHERE:
                return DisconnectCause.ANSWERED_ELSEWHERE;

            case ImsReasonInfo.CODE_CALL_END_CAUSE_CALL_PULL:
                return DisconnectCause.CALL_PULLED;

            case ImsReasonInfo.CODE_MAXIMUM_NUMBER_OF_CALLS_REACHED:
                return DisconnectCause.MAXIMUM_NUMBER_OF_CALLS_REACHED;

            case ImsReasonInfo.CODE_DATA_DISABLED:
                return DisconnectCause.DATA_DISABLED;

            case ImsReasonInfo.CODE_DATA_LIMIT_REACHED:
                return DisconnectCause.DATA_LIMIT_REACHED;

            case ImsReasonInfo.CODE_WIFI_LOST:
                return DisconnectCause.WIFI_LOST;

            case ImsReasonInfo.CODE_ACCESS_CLASS_BLOCKED:
                return DisconnectCause.IMS_ACCESS_BLOCKED;
            default:
        }

        return cause;
    }

    protected int getPreciseDisconnectCauseFromReasonInfo(ImsReasonInfo reasonInfo) {
        return PRECISE_CAUSE_MAP.get(maybeRemapReasonCode(reasonInfo),
                PreciseDisconnectCause.ERROR_UNSPECIFIED);
    }

    /**
     * @return true if the phone is in Emergency Callback mode, otherwise false
     */
    protected boolean isPhoneInEcbMode() {
        return mPhone.isInEcm();
    }

    /**
     * Before dialing pending MO request, check for the Emergency Callback mode.
     * If device is in Emergency callback mode, then exit the mode before dialing pending MO.
     */
    protected void dialPendingMO() {
        boolean isPhoneInEcmMode = isPhoneInEcbMode();
        boolean isEmergencyNumber = mPendingMO.isEmergency();
        if ((!isPhoneInEcmMode) || (isPhoneInEcmMode && isEmergencyNumber)) {
            sendEmptyMessage(EVENT_DIAL_PENDINGMO);
        } else {
            sendEmptyMessage(EVENT_EXIT_ECBM_BEFORE_PENDINGMO);
        }
    }

    /**
     * Listen to the IMS call state change
     */
    protected ImsCall.Listener mImsCallListener = new ImsCall.Listener() {
        @Override
        public void onCallProgressing(ImsCall imsCall) {
            if (DBG) log("onCallProgressing");

            mPendingMO = null;
            processCallStateChange(imsCall, ImsPhoneCall.State.ALERTING,
                    DisconnectCause.NOT_DISCONNECTED);
            mMetrics.writeOnImsCallProgressing(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallStarted(ImsCall imsCall) {
            if (DBG) log("onCallStarted");

            if (mSwitchingFgAndBgCalls) {
                // If we put a call on hold to answer an incoming call, we should reset the
                // variables that keep track of the switch here.
                if (mCallExpectedToResume != null && mCallExpectedToResume == imsCall) {
                    if (DBG) log("onCallStarted: starting a call as a result of a switch.");
                    mSwitchingFgAndBgCalls = false;
                    mCallExpectedToResume = null;
                }
            }

            mPendingMO = null;
            processCallStateChange(imsCall, ImsPhoneCall.State.ACTIVE,
                    DisconnectCause.NOT_DISCONNECTED);

            if (mNotifyVtHandoverToWifiFail &&
                    !imsCall.isWifiCall() && imsCall.isVideoCall() && isWifiConnected()) {
                // Schedule check to see if handover succeeded.
                sendMessageDelayed(obtainMessage(EVENT_CHECK_FOR_WIFI_HANDOVER, imsCall),
                        HANDOVER_TO_WIFI_TIMEOUT_MS);
            }

            mMetrics.writeOnImsCallStarted(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallUpdated(ImsCall imsCall) {
            if (DBG) log("onCallUpdated");
            if (imsCall == null) {
                return;
            }
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                processCallStateChange(imsCall, conn.getCall().mState,
                        DisconnectCause.NOT_DISCONNECTED, true /*ignore state update*/);
                mMetrics.writeImsCallState(mPhone.getPhoneId(),
                        imsCall.getCallSession(), conn.getCall().mState);
            }
        }

        /**
         * onCallStartFailed will be invoked when:
         * case 1) Dialing fails
         * case 2) Ringing call is disconnected by local or remote user
         */
        @Override
        public void onCallStartFailed(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (DBG) log("onCallStartFailed reasonCode=" + reasonInfo.getCode());

            if (mSwitchingFgAndBgCalls) {
                // If we put a call on hold to answer an incoming call, we should reset the
                // variables that keep track of the switch here.
                if (mCallExpectedToResume != null && mCallExpectedToResume == imsCall) {
                    if (DBG) log("onCallStarted: starting a call as a result of a switch.");
                    mSwitchingFgAndBgCalls = false;
                    mCallExpectedToResume = null;
                }
            }

            if (mPendingMO != null) {
                // To initiate dialing circuit-switched call
                if (reasonInfo.getCode() == ImsReasonInfo.CODE_LOCAL_CALL_CS_RETRY_REQUIRED
                        && mBackgroundCall.getState() == ImsPhoneCall.State.IDLE
                        && mRingingCall.getState() == ImsPhoneCall.State.IDLE) {
                    mForegroundCall.detach(mPendingMO);
                    removeConnection(mPendingMO);
                    mPendingMO.finalize();
                    mPendingMO = null;
                    mPhone.initiateSilentRedial();
                    return;
                } else {
                    mPendingMO = null;
                    ImsPhoneConnection conn = findConnection(imsCall);
                    Call.State callState;
                    if (conn != null) {
                        callState = conn.getState();
                    } else {
                        // Need to fall back in case connection is null; it shouldn't be, but a sane
                        // fallback is to assume we're dialing.  This state is only used to
                        // determine which disconnect string to show in the case of a low battery
                        // disconnect.
                        callState = Call.State.DIALING;
                    }
                    int cause = getDisconnectCauseFromReasonInfo(reasonInfo, callState);
                    /// M: if need redial as ECC @{
                    setRedialAsEcc(cause);
                    /// @}

                    if(conn != null) {
                        conn.setPreciseDisconnectCause(
                                getPreciseDisconnectCauseFromReasonInfo(reasonInfo));
                        /// M: @{
                        setVendorDisconnectCause(conn, reasonInfo);
                        /// @}
                    }

                    processCallStateChange(imsCall, ImsPhoneCall.State.DISCONNECTED, cause);
                }
                mMetrics.writeOnImsCallStartFailed(mPhone.getPhoneId(), imsCall.getCallSession(),
                        reasonInfo);
            }
        }

        @Override
        public void onCallTerminated(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (DBG) log("onCallTerminated reasonCode=" + reasonInfo.getCode());

            ImsPhoneConnection conn = findConnection(imsCall);
            Call.State callState;
            if (conn != null) {
                callState = conn.getState();
            } else {
                // Connection shouldn't be null, but if it is, we can assume the call was active.
                // This call state is only used for determining which disconnect message to show in
                // the case of the device's battery being low resulting in a call drop.
                callState = Call.State.ACTIVE;
            }
            int cause = getDisconnectCauseFromReasonInfo(reasonInfo, callState);

            if (DBG) log("cause = " + cause + " conn = " + conn);

            if (conn != null) {
                android.telecom.Connection.VideoProvider videoProvider = conn.getVideoProvider();
                if (videoProvider instanceof ImsVideoCallProviderWrapper) {
                    ImsVideoCallProviderWrapper wrapper = (ImsVideoCallProviderWrapper)
                            videoProvider;
                    /// M: Fix Google memory leak, the data usage handler should be unregistered.@{
                    wrapper.unregisterForDataUsageUpdate(ImsPhoneCallTracker.this);
                    /// @}
                    wrapper.removeImsVideoProviderCallback(conn);
                }
            }
            if (mOnHoldToneId == System.identityHashCode(conn)) {
                if (conn != null && mOnHoldToneStarted) {
                    mPhone.stopOnHoldTone(conn);
                }
                mOnHoldToneStarted = false;
                mOnHoldToneId = -1;
            }
            if (conn != null) {
                if (conn.isPulledCall() && (
                        reasonInfo.getCode() == ImsReasonInfo.CODE_CALL_PULL_OUT_OF_SYNC ||
                        reasonInfo.getCode() == ImsReasonInfo.CODE_SIP_TEMPRARILY_UNAVAILABLE ||
                        reasonInfo.getCode() == ImsReasonInfo.CODE_SIP_FORBIDDEN) &&
                        mPhone != null && mPhone.getExternalCallTracker() != null) {

                    log("Call pull failed.");
                    // Call was being pulled, but the call pull has failed -- inform the associated
                    // TelephonyConnection that the pull failed, and provide it with the original
                    // external connection which was pulled so that it can be swapped back.
                    conn.onCallPullFailed(mPhone.getExternalCallTracker()
                            .getConnectionById(conn.getPulledDialogId()));
                    // Do not mark as disconnected; the call will just change from being a regular
                    // call to being an external call again.
                    cause = DisconnectCause.NOT_DISCONNECTED;

                } else if (conn.isIncoming() && conn.getConnectTime() == 0
                        && cause != DisconnectCause.ANSWERED_ELSEWHERE) {
                    // Missed
                    if (cause == DisconnectCause.NORMAL) {
                        cause = DisconnectCause.INCOMING_MISSED;
                    } else {
                        cause = DisconnectCause.INCOMING_REJECTED;
                    }
                    if (DBG) log("Incoming connection of 0 connect time detected - translated " +
                            "cause = " + cause);
                }
            }

            if (cause == DisconnectCause.NORMAL && conn != null && conn.getImsCall().isMerged()) {
                // Call was terminated while it is merged instead of a remote disconnect.
                cause = DisconnectCause.IMS_MERGED_SUCCESSFULLY;
            }

            /// M: @{
            cause = updateDisconnectCause(cause, conn);
            setRedialAsEcc(cause);
            setVendorDisconnectCause(conn, reasonInfo);
            /// @}

            mMetrics.writeOnImsCallTerminated(mPhone.getPhoneId(), imsCall.getCallSession(),
                    reasonInfo);

            if(conn != null) {
                conn.setPreciseDisconnectCause(getPreciseDisconnectCauseFromReasonInfo(reasonInfo));
            }

            processCallStateChange(imsCall, ImsPhoneCall.State.DISCONNECTED, cause);
            if (mForegroundCall.getState() != ImsPhoneCall.State.ACTIVE) {
                if (mRingingCall.getState().isRinging()) {
                    // Drop pending MO. We should address incoming call first
                    mPendingMO = null;
                /// M: for MTK add-on. @{
                } else if (canDailOnCallTerminated()) {
                /// @}
                    sendEmptyMessage(EVENT_DIAL_PENDINGMO);
                }
            }

            if (mSwitchingFgAndBgCalls) {
                if (DBG) {
                    log("onCallTerminated: Call terminated in the midst of Switching " +
                            "Fg and Bg calls.");
                }
                // If we are the in midst of swapping FG and BG calls and the call that was
                // terminated was the one that we expected to resume, we need to swap the FG and
                // BG calls back.
                if (imsCall == mCallExpectedToResume) {
                    if (DBG) {
                        log("onCallTerminated: switching " + mForegroundCall + " with "
                                + mBackgroundCall);
                    }
                    mForegroundCall.switchWith(mBackgroundCall);
                }
                // This call terminated in the midst of a switch after the other call was held, so
                // resume it back to ACTIVE state since the switch failed.
                log("onCallTerminated: foreground call in state " + mForegroundCall.getState() +
                        " and ringing call in state " + (mRingingCall == null ? "null" :
                        mRingingCall.getState().toString()));

                if (mForegroundCall.getState() == ImsPhoneCall.State.HOLDING ||
                        mRingingCall.getState() == ImsPhoneCall.State.WAITING) {
                    sendEmptyMessage(EVENT_RESUME_BACKGROUND);
                    mSwitchingFgAndBgCalls = false;
                    mCallExpectedToResume = null;
                }
            }

            if (mShouldUpdateImsConfigOnDisconnect) {
                // Ensure we update the IMS config when the call is disconnected; we delayed this
                // because a video call was paused.
                ImsManager.updateImsServiceConfig(mPhone.getContext(), mPhone.getPhoneId(), true);
                mShouldUpdateImsConfigOnDisconnect = false;
            }
        }

        @Override
        public void onCallHeld(ImsCall imsCall) {
            if (DBG) {
                if (mForegroundCall.getImsCall() == imsCall) {
                    log("onCallHeld (fg) " + imsCall);
                } else if (mBackgroundCall.getImsCall() == imsCall) {
                    log("onCallHeld (bg) " + imsCall);
                }
            }

            synchronized (mSyncHold) {
                ImsPhoneCall.State oldState = mBackgroundCall.getState();
                processCallStateChange(imsCall, ImsPhoneCall.State.HOLDING,
                        DisconnectCause.NOT_DISCONNECTED);

                // Note: If we're performing a switchWaitingOrHoldingAndActive, the call to
                // processCallStateChange above may have caused the mBackgroundCall and
                // mForegroundCall references below to change meaning.  Watch out for this if you
                // are reading through this code.
                if (oldState == ImsPhoneCall.State.ACTIVE) {
                    // Note: This case comes up when we have just held a call in response to a
                    // switchWaitingOrHoldingAndActive.  We now need to resume the background call.
                    // The EVENT_RESUME_BACKGROUND causes resumeWaitingOrHolding to be called.
                    if ((mForegroundCall.getState() == ImsPhoneCall.State.HOLDING)
                            || (mRingingCall.getState() == ImsPhoneCall.State.WAITING)) {
                        /// M: ALPS03815859, release pendingMO if it is exist. @{
                        releasePendingMOIfRequired();
                        /// @}
                        sendEmptyMessage(EVENT_RESUME_BACKGROUND);
                    } else {
                        //when multiple connections belong to background call,
                        //only the first callback reaches here
                        //otherwise the oldState is already HOLDING
                        if (mPendingMO != null) {
                            dialPendingMO();
                        }

                        // In this case there will be no call resumed, so we can assume that we
                        // are done switching fg and bg calls now.
                        // This may happen if there is no BG call and we are holding a call so that
                        // we can dial another one.
                        mSwitchingFgAndBgCalls = false;
                    }
                } else if (oldState == ImsPhoneCall.State.IDLE && mSwitchingFgAndBgCalls) {
                    // The other call terminated in the midst of a switch before this call was held,
                    // so resume the foreground call back to ACTIVE state since the switch failed.
                    if (mForegroundCall.getState() == ImsPhoneCall.State.HOLDING) {
                        sendEmptyMessage(EVENT_RESUME_BACKGROUND);
                        mSwitchingFgAndBgCalls = false;
                        mCallExpectedToResume = null;
                    }
                }
            }
            mMetrics.writeOnImsCallHeld(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallHoldFailed(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (DBG) log("onCallHoldFailed reasonCode=" + reasonInfo.getCode());

            synchronized (mSyncHold) {
                ImsPhoneCall.State bgState = mBackgroundCall.getState();
                if (reasonInfo.getCode() == ImsReasonInfo.CODE_LOCAL_CALL_TERMINATED) {
                    // disconnected while processing hold
                    if (mPendingMO != null) {
                        dialPendingMO();
                    }
                } else if (bgState == ImsPhoneCall.State.ACTIVE) {
                    mForegroundCall.switchWith(mBackgroundCall);

                    if (mPendingMO != null) {
                        mPendingMO.setDisconnectCause(DisconnectCause.ERROR_UNSPECIFIED);
                        sendEmptyMessageDelayed(EVENT_HANGUP_PENDINGMO, TIMEOUT_HANGUP_PENDINGMO);
                    }
                }
                mPhone.notifySuppServiceFailed(Phone.SuppService.HOLD);
            }
            mMetrics.writeOnImsCallHoldFailed(mPhone.getPhoneId(), imsCall.getCallSession(),
                    reasonInfo);
        }

        @Override
        public void onCallResumed(ImsCall imsCall) {
            if (DBG) log("onCallResumed");

            // If we are the in midst of swapping FG and BG calls and the call we end up resuming
            // is not the one we expected, we likely had a resume failure and we need to swap the
            // FG and BG calls back.
            if (mSwitchingFgAndBgCalls) {
                if (imsCall != mCallExpectedToResume) {
                    // If the call which resumed isn't as expected, we need to swap back to the
                    // previous configuration; the swap has failed.
                    if (DBG) {
                        log("onCallResumed : switching " + mForegroundCall + " with "
                                + mBackgroundCall);
                    }
                    mForegroundCall.switchWith(mBackgroundCall);
                } else {
                    // The call which resumed is the one we expected to resume, so we can clear out
                    // the mSwitchingFgAndBgCalls flag.
                    if (DBG) {
                        log("onCallResumed : expected call resumed.");
                    }
                }
                mSwitchingFgAndBgCalls = false;
                mCallExpectedToResume = null;
            }
            processCallStateChange(imsCall, ImsPhoneCall.State.ACTIVE,
                    DisconnectCause.NOT_DISCONNECTED);
            mMetrics.writeOnImsCallResumed(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallResumeFailed(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (mSwitchingFgAndBgCalls) {
                // If we are in the midst of swapping the FG and BG calls and
                // we got a resume fail, we need to swap back the FG and BG calls.
                // Since the FG call was held, will also try to resume the same.
                if (imsCall == mCallExpectedToResume) {
                    if (DBG) {
                        log("onCallResumeFailed : switching " + mForegroundCall + " with "
                                + mBackgroundCall);
                    }
                    mForegroundCall.switchWith(mBackgroundCall);
                    if (mForegroundCall.getState() == ImsPhoneCall.State.HOLDING) {
                            sendEmptyMessage(EVENT_RESUME_BACKGROUND);
                    }
                }

                //Call swap is done, reset the relevant variables
                mCallExpectedToResume = null;
                mSwitchingFgAndBgCalls = false;
            }
            mPhone.notifySuppServiceFailed(Phone.SuppService.RESUME);
            mMetrics.writeOnImsCallResumeFailed(mPhone.getPhoneId(), imsCall.getCallSession(),
                    reasonInfo);
        }

        @Override
        public void onCallResumeReceived(ImsCall imsCall) {
            if (DBG) log("onCallResumeReceived");
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                if (mOnHoldToneStarted) {
                    mPhone.stopOnHoldTone(conn);
                    mOnHoldToneStarted = false;
                }
                conn.onConnectionEvent(android.telecom.Connection.EVENT_CALL_REMOTELY_UNHELD, null);
            }

            boolean useVideoPauseWorkaround = mPhone.getContext().getResources().getBoolean(
                    com.android.internal.R.bool.config_useVideoPauseWorkaround);
            if (useVideoPauseWorkaround && mSupportPauseVideo &&
                    VideoProfile.isVideo(conn.getVideoState())) {
                // If we are using the video pause workaround, the vendor IMS code has issues
                // with video pause signalling.  In this case, when a call is remotely
                // held, the modem does not reliably change the video state of the call to be
                // paused.
                // As a workaround, we will turn on that bit now.
                conn.changeToUnPausedState();
            }

            /// M: @{
            mtkNotifyRemoteHeld(conn, false);
            /// @}

            SuppServiceNotification supp = new SuppServiceNotification();
            // Type of notification: 0 = MO; 1 = MT
            // Refer SuppServiceNotification class documentation.
            supp.notificationType = 1;
            supp.code = SuppServiceNotification.MT_CODE_CALL_RETRIEVED;
            mPhone.notifySuppSvcNotification(supp);
            mMetrics.writeOnImsCallResumeReceived(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallHoldReceived(ImsCall imsCall) {
            if (DBG) log("onCallHoldReceived");

            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                if (!mOnHoldToneStarted && ImsPhoneCall.isLocalTone(imsCall) &&
                        conn.getState() == ImsPhoneCall.State.ACTIVE) {
                    mPhone.startOnHoldTone(conn);
                    mOnHoldToneStarted = true;
                    mOnHoldToneId = System.identityHashCode(conn);
                }
                conn.onConnectionEvent(android.telecom.Connection.EVENT_CALL_REMOTELY_HELD, null);

                boolean useVideoPauseWorkaround = mPhone.getContext().getResources().getBoolean(
                        com.android.internal.R.bool.config_useVideoPauseWorkaround);
                if (useVideoPauseWorkaround && mSupportPauseVideo &&
                        VideoProfile.isVideo(conn.getVideoState())) {
                    // If we are using the video pause workaround, the vendor IMS code has issues
                    // with video pause signalling.  In this case, when a call is remotely
                    // held, the modem does not reliably change the video state of the call to be
                    // paused.
                    // As a workaround, we will turn on that bit now.
                    conn.changeToPausedState();
                }
            }

            /// M: @{
            mtkNotifyRemoteHeld(conn, true);
            /// @}

            SuppServiceNotification supp = new SuppServiceNotification();
            // Type of notification: 0 = MO; 1 = MT
            // Refer SuppServiceNotification class documentation.
            supp.notificationType = 1;
            supp.code = SuppServiceNotification.MT_CODE_CALL_ON_HOLD;
            mPhone.notifySuppSvcNotification(supp);
            mMetrics.writeOnImsCallHoldReceived(mPhone.getPhoneId(), imsCall.getCallSession());
        }

        @Override
        public void onCallSuppServiceReceived(ImsCall call,
                ImsSuppServiceNotification suppServiceInfo) {
            if (DBG) log("onCallSuppServiceReceived: suppServiceInfo=" + suppServiceInfo);

            SuppServiceNotification supp = new SuppServiceNotification();
            supp.notificationType = suppServiceInfo.notificationType;
            supp.code = suppServiceInfo.code;
            supp.index = suppServiceInfo.index;
            supp.number = suppServiceInfo.number;
            supp.history = suppServiceInfo.history;

            mPhone.notifySuppSvcNotification(supp);
        }

        @Override
        public void onCallMerged(final ImsCall call, final ImsCall peerCall, boolean swapCalls) {
            if (DBG) log("onCallMerged");

            ImsPhoneCall foregroundImsPhoneCall = findConnection(call).getCall();
            ImsPhoneConnection peerConnection = findConnection(peerCall);
            ImsPhoneCall peerImsPhoneCall = peerConnection == null ? null
                    : peerConnection.getCall();

            if (swapCalls) {
                switchAfterConferenceSuccess();
            }
            foregroundImsPhoneCall.merge(peerImsPhoneCall, ImsPhoneCall.State.ACTIVE);

            try {
                final ImsPhoneConnection conn = findConnection(call);
                log("onCallMerged: ImsPhoneConnection=" + conn);
                log("onCallMerged: CurrentVideoProvider=" + conn.getVideoProvider());
                setVideoCallProvider(conn, call);
                log("onCallMerged: CurrentVideoProvider=" + conn.getVideoProvider());
            } catch (Exception e) {
                loge("onCallMerged: exception " + e);
            }

            // After merge complete, update foreground as Active
            // and background call as Held, if background call exists
            processCallStateChange(mForegroundCall.getImsCall(), ImsPhoneCall.State.ACTIVE,
                    DisconnectCause.NOT_DISCONNECTED);
            if (peerConnection != null) {
                processCallStateChange(mBackgroundCall.getImsCall(), ImsPhoneCall.State.HOLDING,
                    DisconnectCause.NOT_DISCONNECTED);
            }

            // Check if the merge was requested by an existing conference call. In that
            // case, no further action is required.
            if (!call.isMergeRequestedByConf()) {
                log("onCallMerged :: calling onMultipartyStateChanged()");
                onMultipartyStateChanged(call, true);
            } else {
                log("onCallMerged :: Merge requested by existing conference.");
                // Reset the flag.
                call.resetIsMergeRequestedByConf(false);
            }
            logState();
        }

        @Override
        public void onCallMergeFailed(ImsCall call, ImsReasonInfo reasonInfo) {
            if (DBG) log("onCallMergeFailed reasonInfo=" + reasonInfo);

            // TODO: the call to notifySuppServiceFailed throws up the "merge failed" dialog
            // We should move this into the InCallService so that it is handled appropriately
            // based on the user facing UI.
            mPhone.notifySuppServiceFailed(Phone.SuppService.CONFERENCE);

            // Start plumbing this even through Telecom so other components can take
            // appropriate action.
            ImsPhoneConnection conn = findConnection(call);
            if (conn != null) {
                conn.onConferenceMergeFailed();
                /// M: ALPS03604784, Google issue. Both fg and bg connection set as
                /// MERGE_START while merge, but only the merge host connection will update to
                /// MERGE_COMPLETE while merge failed. @{
                //conn.handleMergeComplete();
                /// @}
            }

            /// M: ALPS03604784, Updtate both fg and bg connection to MERGE_COMPLETE. @{
            ImsPhoneConnection foregroundConnection = mForegroundCall.getFirstConnection();
            if (foregroundConnection != null) {
                foregroundConnection.handleMergeComplete();
            }

            ImsCall bgImsCall = mBackgroundCall.getImsCall();
            if (bgImsCall == null) {
                log("onCallMergeFailed: conference no background ims call");
                return;
            }
            ImsPhoneConnection backgroundConnection = findConnection(bgImsCall);
            if (backgroundConnection != null) {
                backgroundConnection.handleMergeComplete();
            }
            /// @}
        }

        /**
         * Called when the state of IMS conference participant(s) has changed.
         *
         * @param call the call object that carries out the IMS call.
         * @param participants the participant(s) and their new state information.
         */
        @Override
        public void onConferenceParticipantsStateChanged(ImsCall call,
                List<ConferenceParticipant> participants) {
            if (DBG) log("onConferenceParticipantsStateChanged");

            ImsPhoneConnection conn = findConnection(call);
            if (conn != null) {
                conn.updateConferenceParticipants(participants);
            }
        }

        @Override
        public void onCallSessionTtyModeReceived(ImsCall call, int mode) {
            mPhone.onTtyModeReceived(mode);
        }

        @Override
        public void onCallHandover(ImsCall imsCall, int srcAccessTech, int targetAccessTech,
            ImsReasonInfo reasonInfo) {
            if (DBG) {
                log("onCallHandover ::  srcAccessTech=" + srcAccessTech + ", targetAccessTech=" +
                        targetAccessTech + ", reasonInfo=" + reasonInfo);
            }

            // Only consider it a valid handover to WIFI if the source radio tech is known.
            boolean isHandoverToWifi = srcAccessTech != ServiceState.RIL_RADIO_TECHNOLOGY_UNKNOWN
                    && srcAccessTech != ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN
                    && targetAccessTech == ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN;
            if (isHandoverToWifi) {
                // If we handed over to wifi successfully, don't check for failure in the future.
                removeMessages(EVENT_CHECK_FOR_WIFI_HANDOVER);
            }

            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                // Only consider it a handover from WIFI if the source and target radio tech is known.
                boolean isHandoverFromWifi =
                        srcAccessTech == ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN
                                && targetAccessTech != ServiceState.RIL_RADIO_TECHNOLOGY_UNKNOWN
                                && targetAccessTech != ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN;
                if (isHandoverFromWifi && imsCall.isVideoCall()) {
                    if (mNotifyHandoverVideoFromWifiToLTE && mIsDataEnabled) {
                        log("onCallHandover :: notifying of WIFI to LTE handover.");
                        conn.onConnectionEvent(
                                TelephonyManager.EVENT_HANDOVER_VIDEO_FROM_WIFI_TO_LTE, null);
                    }

                    if (!mIsDataEnabled && mIsViLteDataMetered) {
                        // Call was downgraded from WIFI to LTE and data is metered; downgrade the
                        // call now.
                        downgradeVideoCall(ImsReasonInfo.CODE_WIFI_LOST, conn);
                    }
                }
            } else {
                loge("onCallHandover :: connection null.");
            }

            mMetrics.writeOnImsCallHandoverEvent(mPhone.getPhoneId(),
                    TelephonyCallSession.Event.Type.IMS_CALL_HANDOVER, imsCall.getCallSession(),
                    srcAccessTech, targetAccessTech, reasonInfo);
        }

        @Override
        public void onCallHandoverFailed(ImsCall imsCall, int srcAccessTech, int targetAccessTech,
            ImsReasonInfo reasonInfo) {
            if (DBG) {
                log("onCallHandoverFailed :: srcAccessTech=" + srcAccessTech +
                    ", targetAccessTech=" + targetAccessTech + ", reasonInfo=" + reasonInfo);
            }
            mMetrics.writeOnImsCallHandoverEvent(mPhone.getPhoneId(),
                    TelephonyCallSession.Event.Type.IMS_CALL_HANDOVER_FAILED,
                    imsCall.getCallSession(), srcAccessTech, targetAccessTech, reasonInfo);

            boolean isHandoverToWifi = srcAccessTech != ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN &&
                    targetAccessTech == ServiceState.RIL_RADIO_TECHNOLOGY_IWLAN;
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null && isHandoverToWifi) {
                log("onCallHandoverFailed - handover to WIFI Failed");

                // If we know we failed to handover, don't check for failure in the future.
                removeMessages(EVENT_CHECK_FOR_WIFI_HANDOVER);

                if (mNotifyVtHandoverToWifiFail) {
                    // Only notify others if carrier config indicates to do so.
                    conn.onHandoverToWifiFailed();
                }
            }
        }

        @Override
        public void onRttModifyRequestReceived(ImsCall imsCall) {
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                conn.onRttModifyRequestReceived();
            }
        }

        @Override
        public void onRttModifyResponseReceived(ImsCall imsCall, int status) {
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                conn.onRttModifyResponseReceived(status);
                if (status ==
                        android.telecom.Connection.RttModifyStatus.SESSION_MODIFY_REQUEST_SUCCESS) {
                    conn.startRttTextProcessing();
                }
            }
        }

        @Override
        public void onRttMessageReceived(ImsCall imsCall, String message) {
            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                conn.onRttMessageReceived(message);
            }
        }

        /**
         * Handles a change to the multiparty state for an {@code ImsCall}.  Notifies the associated
         * {@link ImsPhoneConnection} of the change.
         *
         * @param imsCall The IMS call.
         * @param isMultiParty {@code true} if the call became multiparty, {@code false}
         *      otherwise.
         */
        @Override
        public void onMultipartyStateChanged(ImsCall imsCall, boolean isMultiParty) {
            if (DBG) log("onMultipartyStateChanged to " + (isMultiParty ? "Y" : "N"));

            ImsPhoneConnection conn = findConnection(imsCall);
            if (conn != null) {
                conn.updateMultipartyState(isMultiParty);
            }
        }
    };

    /**
     * Listen to the IMS call state change
     */
    protected ImsCall.Listener mImsUssdListener = new ImsCall.Listener() {
        @Override
        public void onCallStarted(ImsCall imsCall) {
            if (DBG) log("mImsUssdListener onCallStarted");

            if (imsCall == mUssdSession) {
                if (mPendingUssd != null) {
                    AsyncResult.forMessage(mPendingUssd);
                    mPendingUssd.sendToTarget();
                    mPendingUssd = null;
                }
            }
        }

        @Override
        public void onCallStartFailed(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (DBG) log("mImsUssdListener onCallStartFailed reasonCode=" + reasonInfo.getCode());

            onCallTerminated(imsCall, reasonInfo);
        }

        @Override
        public void onCallTerminated(ImsCall imsCall, ImsReasonInfo reasonInfo) {
            if (DBG) log("mImsUssdListener onCallTerminated reasonCode=" + reasonInfo.getCode());
            removeMessages(EVENT_CHECK_FOR_WIFI_HANDOVER);

            if (imsCall == mUssdSession) {
                mUssdSession = null;
                if (mPendingUssd != null) {
                    CommandException ex =
                            new CommandException(CommandException.Error.GENERIC_FAILURE);
                    AsyncResult.forMessage(mPendingUssd, null, ex);
                    mPendingUssd.sendToTarget();
                    mPendingUssd = null;
                }
            }
            imsCall.close();
        }

        @Override
        public void onCallUssdMessageReceived(ImsCall call,
                int mode, String ussdMessage) {
            if (DBG) log("mImsUssdListener onCallUssdMessageReceived mode=" + mode);

            int ussdMode = -1;

            switch(mode) {
                case ImsCall.USSD_MODE_REQUEST:
                    ussdMode = CommandsInterface.USSD_MODE_REQUEST;
                    break;

                case ImsCall.USSD_MODE_NOTIFY:
                    ussdMode = CommandsInterface.USSD_MODE_NOTIFY;
                    break;
            }

            mPhone.onIncomingUSSD(ussdMode, ussdMessage);
        }
    };

    /**
     * Listen to the IMS service state change
     *
     */
    private ImsConnectionStateListener mImsConnectionStateListener =
        new ImsConnectionStateListener() {
        @Override
        public void onImsConnected(int imsRadioTech) {
            if (DBG) log("onImsConnected imsRadioTech=" + imsRadioTech);
            mPhone.setServiceState(ServiceState.STATE_IN_SERVICE);
            mPhone.setImsRegistered(true);
            mMetrics.writeOnImsConnectionState(mPhone.getPhoneId(),
                    ImsConnectionState.State.CONNECTED, null);
        }

        @Override
        public void onImsDisconnected(ImsReasonInfo imsReasonInfo) {
            if (DBG) log("onImsDisconnected imsReasonInfo=" + imsReasonInfo);
            resetImsCapabilities();
            mPhone.setServiceState(ServiceState.STATE_OUT_OF_SERVICE);
            mPhone.setImsRegistered(false);
            mPhone.processDisconnectReason(imsReasonInfo);
            mMetrics.writeOnImsConnectionState(mPhone.getPhoneId(),
                    ImsConnectionState.State.DISCONNECTED, imsReasonInfo);
        }

        @Override
        public void onImsProgressing(int imsRadioTech) {
            if (DBG) log("onImsProgressing imsRadioTech=" + imsRadioTech);
            mPhone.setServiceState(ServiceState.STATE_OUT_OF_SERVICE);
            mPhone.setImsRegistered(false);
            mMetrics.writeOnImsConnectionState(mPhone.getPhoneId(),
                    ImsConnectionState.State.PROGRESSING, null);
        }

        @Override
        public void onImsResumed() {
            if (DBG) log("onImsResumed");
            mPhone.setServiceState(ServiceState.STATE_IN_SERVICE);
            mMetrics.writeOnImsConnectionState(mPhone.getPhoneId(),
                    ImsConnectionState.State.RESUMED, null);
        }

        @Override
        public void onImsSuspended() {
            if (DBG) log("onImsSuspended");
            mPhone.setServiceState(ServiceState.STATE_OUT_OF_SERVICE);
            mMetrics.writeOnImsConnectionState(mPhone.getPhoneId(),
                    ImsConnectionState.State.SUSPENDED, null);

        }

        @Override
        public void onFeatureCapabilityChanged(int serviceClass,
                int[] enabledFeatures, int[] disabledFeatures) {
            if (DBG) log("onFeatureCapabilityChanged");
            SomeArgs args = SomeArgs.obtain();
            args.argi1 = serviceClass;
            args.arg1 = enabledFeatures;
            args.arg2 = disabledFeatures;
            // Remove any pending updates; they're already stale, so no need to process them.
            removeMessages(EVENT_ON_FEATURE_CAPABILITY_CHANGED);
            obtainMessage(EVENT_ON_FEATURE_CAPABILITY_CHANGED, args).sendToTarget();
            //M: for RTT
            checkRttCallType();
        }

        @Override
        public void onVoiceMessageCountChanged(int count) {
            if (DBG) log("onVoiceMessageCountChanged :: count=" + count);
            mPhone.mDefaultPhone.setVoiceMessageCount(count);
        }

        @Override
        public void registrationAssociatedUriChanged(Uri[] uris) {
            if (DBG) log("registrationAssociatedUriChanged");
            mPhone.setCurrentSubscriberUris(uris);
        }
    };

    private ImsConfigListener.Stub mImsConfigListener = new ImsConfigListener.Stub() {
        @Override
        public void onGetFeatureResponse(int feature, int network, int value, int status) {}

        @Override
        public void onSetFeatureResponse(int feature, int network, int value, int status) {
            mMetrics.writeImsSetFeatureValue(
                    mPhone.getPhoneId(), feature, network, value, status);
        }

        @Override
        public void onGetVideoQuality(int status, int quality) {}

        @Override
        public void onSetVideoQuality(int status) {}

    };

    public ImsUtInterface getUtInterface() throws ImsException {
        if (mImsManager == null) {
            throw getImsManagerIsNullException();
        }

        ImsUtInterface ut = mImsManager.getSupplementaryServiceConfiguration();
        return ut;
    }

    protected void transferHandoverConnections(ImsPhoneCall call) {
        if (call.mConnections != null) {
            for (Connection c : call.mConnections) {
                c.mPreHandoverState = call.mState;
                log ("Connection state before handover is " + c.getStateBeforeHandover());
                /// M: for conference SRVCC
                setMultiPartyState(c);
            }
        }
        if (mHandoverCall.mConnections == null ) {
            mHandoverCall.mConnections = call.mConnections;
        } else { // Multi-call SRVCC
            mHandoverCall.mConnections.addAll(call.mConnections);
        }
        if (mHandoverCall.mConnections != null) {
            if (call.getImsCall() != null) {
                call.getImsCall().close();
            }
            for (Connection c : mHandoverCall.mConnections) {
                ((ImsPhoneConnection)c).changeParent(mHandoverCall);
                ((ImsPhoneConnection)c).releaseWakeLock();
            }
        }
        if (call.getState().isAlive()) {
            log ("Call is alive and state is " + call.mState);
            mHandoverCall.mState = call.mState;
        }
        call.mConnections.clear();
        call.mState = ImsPhoneCall.State.IDLE;
        /// M: reset ring back tone
        resetRingBackTone(call);
    }

    /* package */
    protected void notifySrvccState(Call.SrvccState state) {
        if (DBG) log("notifySrvccState state=" + state);

        mSrvccState = state;

        if (mSrvccState == Call.SrvccState.COMPLETED) {
            transferHandoverConnections(mForegroundCall);
            transferHandoverConnections(mBackgroundCall);
            transferHandoverConnections(mRingingCall);
            /// M: update
            updateForSrvccCompleted();
        }
    }

    //****** Overridden from Handler

    @Override
    public void
    handleMessage (Message msg) {
        AsyncResult ar;
        if (DBG) log("handleMessage what=" + msg.what);

        switch (msg.what) {
            case EVENT_HANGUP_PENDINGMO:
                if (mPendingMO != null) {
                    mPendingMO.onDisconnect();
                    removeConnection(mPendingMO);
                    mPendingMO = null;
                }
                mPendingIntentExtras = null;
                updatePhoneState();
                mPhone.notifyPreciseCallStateChanged();
                break;
            case EVENT_RESUME_BACKGROUND:
                try {
                    resumeWaitingOrHolding();
                } catch (CallStateException e) {
                    if (Phone.DEBUG_PHONE) {
                        loge("handleMessage EVENT_RESUME_BACKGROUND exception=" + e);
                    }
                }
                break;
            case EVENT_DIAL_PENDINGMO:
                dialInternal(mPendingMO, mClirMode, mPendingCallVideoState, mPendingIntentExtras);
                mPendingIntentExtras = null;
                break;

            case EVENT_EXIT_ECBM_BEFORE_PENDINGMO:
                if (mPendingMO != null) {
                    //Send ECBM exit request
                    try {
                        getEcbmInterface().exitEmergencyCallbackMode();
                        mPhone.setOnEcbModeExitResponse(this, EVENT_EXIT_ECM_RESPONSE_CDMA, null);
                        pendingCallClirMode = mClirMode;
                        pendingCallInEcm = true;
                    } catch (ImsException e) {
                        e.printStackTrace();
                        mPendingMO.setDisconnectCause(DisconnectCause.ERROR_UNSPECIFIED);
                        sendEmptyMessageDelayed(EVENT_HANGUP_PENDINGMO, TIMEOUT_HANGUP_PENDINGMO);
                    }
                }
                break;

            case EVENT_EXIT_ECM_RESPONSE_CDMA:
                // no matter the result, we still do the same here
                if (pendingCallInEcm) {
                    dialInternal(mPendingMO, pendingCallClirMode,
                            mPendingCallVideoState, mPendingIntentExtras);
                    mPendingIntentExtras = null;
                    pendingCallInEcm = false;
                }
                mPhone.unsetOnEcbModeExitResponse(this);
                break;
            case EVENT_VT_DATA_USAGE_UPDATE:
                ar = (AsyncResult) msg.obj;
                ImsCall call = (ImsCall) ar.userObj;
                Long usage = (long) ar.result;
                log("VT data usage update. usage = " + usage + ", imsCall = " + call);
                if (usage > 0) {
                    updateVtDataUsage(call, usage);
                }
                break;
            case EVENT_DATA_ENABLED_CHANGED:
                ar = (AsyncResult) msg.obj;
                if (ar.result instanceof Pair) {
                    Pair<Boolean, Integer> p = (Pair<Boolean, Integer>) ar.result;
                    onDataEnabledChanged(p.first, p.second);
                }
                break;
            case EVENT_GET_IMS_SERVICE:
                try {
                    getImsService();
                } catch (ImsException e) {
                    loge("getImsService: " + e);
                    retryGetImsService();
                }
                break;
            case EVENT_CHECK_FOR_WIFI_HANDOVER:
                if (msg.obj instanceof ImsCall) {
                    ImsCall imsCall = (ImsCall) msg.obj;
                    if (!imsCall.isWifiCall()) {
                        // Call did not handover to wifi, notify of handover failure.
                        ImsPhoneConnection conn = findConnection(imsCall);
                        if (conn != null) {
                            conn.onHandoverToWifiFailed();
                        }
                    }
                }
                break;
            case EVENT_ON_FEATURE_CAPABILITY_CHANGED: {
                SomeArgs args = (SomeArgs) msg.obj;
                try {
                    int serviceClass = args.argi1;
                    int[] enabledFeatures = (int[]) args.arg1;
                    int[] disabledFeatures = (int[]) args.arg2;
                    handleFeatureCapabilityChanged(serviceClass, enabledFeatures, disabledFeatures);
                } finally {
                    args.recycle();
                }
                break;
            }
        }
    }

    /**
     * Update video call data usage
     *
     * @param call The IMS call
     * @param dataUsage The aggregated data usage for the call
     */
    private void updateVtDataUsage(ImsCall call, long dataUsage) {
        long oldUsage = 0L;
        if (mVtDataUsageMap.containsKey(call.uniqueId)) {
            oldUsage = mVtDataUsageMap.get(call.uniqueId);
        }

        long delta = dataUsage - oldUsage;
        mVtDataUsageMap.put(call.uniqueId, dataUsage);

        log("updateVtDataUsage: call=" + call + ", delta=" + delta);

        long currentTime = SystemClock.elapsedRealtime();
        int isRoaming = mPhone.getServiceState().getDataRoaming() ? 1 : 0;

        // Create the snapshot of total video call data usage.
        NetworkStats vtDataUsageSnapshot = new NetworkStats(currentTime, 1);
        vtDataUsageSnapshot.combineAllValues(mVtDataUsageSnapshot);
        // Since the modem only reports the total vt data usage rather than rx/tx separately,
        // the only thing we can do here is splitting the usage into half rx and half tx.
        // Uid -1 indicates this is for the overall device data usage.
        /// M: use getVtInterface to get the VT_INTERFACE for dual IMS Video Call. @{
        vtDataUsageSnapshot.combineValues(new NetworkStats.Entry(
                getVtInterface(), -1, NetworkStats.SET_FOREGROUND,
                NetworkStats.TAG_NONE, 1, isRoaming, delta / 2, 0, delta / 2, 0, 0));
        //vtDataUsageSnapshot.combineValues(new NetworkStats.Entry(
        //        NetworkStatsService.VT_INTERFACE, -1, NetworkStats.SET_FOREGROUND,
        //        NetworkStats.TAG_NONE, 1, isRoaming, delta / 2, 0, delta / 2, 0, 0));
        /// @}
        mVtDataUsageSnapshot = vtDataUsageSnapshot;

        // Create the snapshot of video call data usage per dialer. combineValues will create
        // a separate entry if uid is different from the previous snapshot.
        NetworkStats vtDataUsageUidSnapshot = new NetworkStats(currentTime, 1);
        vtDataUsageUidSnapshot.combineAllValues(mVtDataUsageUidSnapshot);

        // The dialer uid might not be initialized correctly during boot up due to telecom service
        // not ready or its default dialer cache not ready. So we double check again here to see if
        // default dialer uid is really not available.
        if (mDefaultDialerUid.get() == NetworkStats.UID_ALL) {
            final TelecomManager telecomManager =
                    (TelecomManager) mPhone.getContext().getSystemService(Context.TELECOM_SERVICE);
            mDefaultDialerUid.set(
                    getPackageUid(mPhone.getContext(), telecomManager.getDefaultDialerPackage()));
        }

        // Since the modem only reports the total vt data usage rather than rx/tx separately,
        // the only thing we can do here is splitting the usage into half rx and half tx.
        /// M: use getVtInterface to get the VT_INTERFACE for dual IMS Video Call. @{
        vtDataUsageUidSnapshot.combineValues(new NetworkStats.Entry(
                getVtInterface(), mDefaultDialerUid.get(),
                NetworkStats.SET_FOREGROUND, NetworkStats.TAG_NONE, 1, isRoaming, delta / 2,
                0, delta / 2, 0, 0));
        //vtDataUsageUidSnapshot.combineValues(new NetworkStats.Entry(
        //        NetworkStatsService.VT_INTERFACE, mDefaultDialerUid.get(),
        //        NetworkStats.SET_FOREGROUND, NetworkStats.TAG_NONE, 1, isRoaming, delta / 2,
        //        0, delta / 2, 0, 0));
        /// @}
        mVtDataUsageUidSnapshot = vtDataUsageUidSnapshot;
    }

    @Override
    protected void log(String msg) {
        Rlog.d(LOG_TAG, "[ImsPhoneCallTracker] " + msg);
    }

    /* package */
    void loge(String msg) {
        Rlog.e(LOG_TAG, "[ImsPhoneCallTracker] " + msg);
    }

    /**
     * Logs the current state of the ImsPhoneCallTracker.  Useful for debugging issues with
     * call tracking.
     */
    /* package */
    public void logState() {
        if (!VERBOSE_STATE_LOGGING) {
            return;
        }

        StringBuilder sb = new StringBuilder();
        sb.append("Current IMS PhoneCall State:\n");
        sb.append(" Foreground: ");
        sb.append(mForegroundCall);
        sb.append("\n");
        sb.append(" Background: ");
        sb.append(mBackgroundCall);
        sb.append("\n");
        sb.append(" Ringing: ");
        sb.append(mRingingCall);
        sb.append("\n");
        sb.append(" Handover: ");
        sb.append(mHandoverCall);
        sb.append("\n");
        Rlog.v(LOG_TAG, sb.toString());
    }

    @Override
    public void dump(FileDescriptor fd, PrintWriter pw, String[] args) {
        pw.println("ImsPhoneCallTracker extends:");
        super.dump(fd, pw, args);
        pw.println(" mVoiceCallEndedRegistrants=" + mVoiceCallEndedRegistrants);
        pw.println(" mVoiceCallStartedRegistrants=" + mVoiceCallStartedRegistrants);
        pw.println(" mRingingCall=" + mRingingCall);
        pw.println(" mForegroundCall=" + mForegroundCall);
        pw.println(" mBackgroundCall=" + mBackgroundCall);
        pw.println(" mHandoverCall=" + mHandoverCall);
        pw.println(" mPendingMO=" + mPendingMO);
        //pw.println(" mHangupPendingMO=" + mHangupPendingMO);
        pw.println(" mPhone=" + mPhone);
        pw.println(" mDesiredMute=" + mDesiredMute);
        pw.println(" mState=" + mState);
        for (int i = 0; i < mImsFeatureEnabled.length; i++) {
            pw.println(" " + mImsFeatureStrings[i] + ": "
                    + ((mImsFeatureEnabled[i]) ? "enabled" : "disabled"));
        }
        pw.println(" mDefaultDialerUid=" + mDefaultDialerUid.get());
        pw.println(" mVtDataUsageSnapshot=" + mVtDataUsageSnapshot);
        pw.println(" mVtDataUsageUidSnapshot=" + mVtDataUsageUidSnapshot);

        pw.flush();
        pw.println("++++++++++++++++++++++++++++++++");

        try {
            if (mImsManager != null) {
                mImsManager.dump(fd, pw, args);
            }
        } catch (Exception e) {
            e.printStackTrace();
        }

        if (mConnections != null && mConnections.size() > 0) {
            pw.println("mConnections:");
            for (int i = 0; i < mConnections.size(); i++) {
                pw.println("  [" + i + "]: " + mConnections.get(i));
            }
        }
    }

    @Override
    protected void handlePollCalls(AsyncResult ar) {
    }

    /// M: add-on @{
    /* package */
    //ImsEcbm getEcbmInterface() throws ImsException {
    public ImsEcbm getEcbmInterface() throws ImsException {
    /// @}
        if (mImsManager == null) {
            throw getImsManagerIsNullException();
        }

        ImsEcbm ecbm = mImsManager.getEcbmInterface(mServiceId);
        return ecbm;
    }

    /* package */
    public ImsMultiEndpoint getMultiEndpointInterface() throws ImsException {
        if (mImsManager == null) {
            throw getImsManagerIsNullException();
        }

        try {
            return mImsManager.getMultiEndpointInterface(mServiceId);
        } catch (ImsException e) {
            if (e.getCode() == ImsReasonInfo.CODE_MULTIENDPOINT_NOT_SUPPORTED) {
                return null;
            } else {
                throw e;
            }

        }
    }

    public boolean isInEmergencyCall() {
        return mIsInEmergencyCall;
    }

    public boolean isVolteEnabled() {
        return mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_VOICE_OVER_LTE];
    }

    public boolean isVowifiEnabled() {
        return mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_VOICE_OVER_WIFI];
    }

    public boolean isVideoCallEnabled() {
        return (mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_VIDEO_OVER_LTE]
                || mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_VIDEO_OVER_WIFI]);
    }

    @Override
    public PhoneConstants.State getState() {
        return mState;
    }

    protected void retryGetImsService() {
        // The binder connection is already up. Do not try to get it again.
        if (mImsManager.isServiceAvailable()) {
            return;
        }
        //Leave mImsManager as null, then CallStateException will be thrown when dialing
        mImsManager = null;
        // Exponential backoff during retry, limited to 32 seconds.
        loge("getImsService: Retrying getting ImsService...");
        removeMessages(EVENT_GET_IMS_SERVICE);
        sendEmptyMessageDelayed(EVENT_GET_IMS_SERVICE, mRetryTimeout.get());
    }

    protected void setVideoCallProvider(ImsPhoneConnection conn, ImsCall imsCall)
            throws RemoteException {
        IImsVideoCallProvider imsVideoCallProvider =
                imsCall.getCallSession().getVideoCallProvider();
        if (imsVideoCallProvider != null) {
            // TODO: Remove this when we can better formalize the format of session modify requests.
            boolean useVideoPauseWorkaround = mPhone.getContext().getResources().getBoolean(
                    com.android.internal.R.bool.config_useVideoPauseWorkaround);

            ImsVideoCallProviderWrapper imsVideoCallProviderWrapper =
                    new ImsVideoCallProviderWrapper(imsVideoCallProvider);
            if (useVideoPauseWorkaround) {
                imsVideoCallProviderWrapper.setUseVideoPauseWorkaround(useVideoPauseWorkaround);
            }
            conn.setVideoProvider(imsVideoCallProviderWrapper);
            imsVideoCallProviderWrapper.registerForDataUsageUpdate
                    (this, EVENT_VT_DATA_USAGE_UPDATE, imsCall);
            imsVideoCallProviderWrapper.addImsVideoProviderCallback(conn);
        }
    }

    public boolean isUtEnabled() {
        return (mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_UT_OVER_LTE]
            || mImsFeatureEnabled[ImsConfig.FeatureConstants.FEATURE_TYPE_UT_OVER_WIFI]);
    }

    /**
     * Given a call subject, removes any characters considered by the current carrier to be
     * invalid, as well as escaping (using \) any characters which the carrier requires to be
     * escaped.
     *
     * @param callSubject The call subject.
     * @return The call subject with invalid characters removed and escaping applied as required.
     */
    protected String cleanseInstantLetteringMessage(String callSubject) {
        if (TextUtils.isEmpty(callSubject)) {
            return callSubject;
        }

        // Get the carrier config for the current sub.
        CarrierConfigManager configMgr = (CarrierConfigManager)
                mPhone.getContext().getSystemService(Context.CARRIER_CONFIG_SERVICE);
        // Bail if we can't find the carrier config service.
        if (configMgr == null) {
            return callSubject;
        }

        PersistableBundle carrierConfig = configMgr.getConfigForSubId(mPhone.getSubId());
        // Bail if no carrier config found.
        if (carrierConfig == null) {
            return callSubject;
        }

        // Try to replace invalid characters
        String invalidCharacters = carrierConfig.getString(
                CarrierConfigManager.KEY_CARRIER_INSTANT_LETTERING_INVALID_CHARS_STRING);
        if (!TextUtils.isEmpty(invalidCharacters)) {
            callSubject = callSubject.replaceAll(invalidCharacters, "");
        }

        // Try to escape characters which need to be escaped.
        String escapedCharacters = carrierConfig.getString(
                CarrierConfigManager.KEY_CARRIER_INSTANT_LETTERING_ESCAPED_CHARS_STRING);
        if (!TextUtils.isEmpty(escapedCharacters)) {
            callSubject = escapeChars(escapedCharacters, callSubject);
        }
        return callSubject;
    }

    /**
     * Given a source string, return a string where a set of characters are escaped using the
     * backslash character.
     *
     * @param toEscape The characters to escape with a backslash.
     * @param source The source string.
     * @return The source string with characters escaped.
     */
    private String escapeChars(String toEscape, String source) {
        StringBuilder escaped = new StringBuilder();
        for (char c : source.toCharArray()) {
            if (toEscape.contains(Character.toString(c))) {
                escaped.append("\\");
            }
            escaped.append(c);
        }

        return escaped.toString();
    }

    /**
     * Initiates a pull of an external call.
     *
     * Initiates a pull by making a dial request with the {@link ImsCallProfile#EXTRA_IS_CALL_PULL}
     * extra specified.  We call {@link ImsPhone#notifyUnknownConnection(Connection)} which notifies
     * Telecom of the new dialed connection.  The
     * {@code PstnIncomingCallNotifier#maybeSwapWithUnknownConnection} logic ensures that the new
     * {@link ImsPhoneConnection} resulting from the dial gets swapped with the
     * {@link ImsExternalConnection}, which effectively makes the external call become a regular
     * call.  Magic!
     *
     * @param number The phone number of the call to be pulled.
     * @param videoState The desired video state of the pulled call.
     * @param dialogId The {@link ImsExternalConnection#getCallId()} dialog id associated with the
     *                 call which is being pulled.
     */
    @Override
    public void pullExternalCall(String number, int videoState, int dialogId) {
        Bundle extras = new Bundle();
        extras.putBoolean(ImsCallProfile.EXTRA_IS_CALL_PULL, true);
        extras.putInt(ImsExternalCallTracker.EXTRA_IMS_EXTERNAL_CALL_ID, dialogId);
        try {
            Connection connection = dial(number, videoState, extras);
            mPhone.notifyUnknownConnection(connection);
        } catch (CallStateException e) {
            loge("pullExternalCall failed - " + e);
        }
    }

    protected ImsException getImsManagerIsNullException() {
        return new ImsException("no ims manager", ImsReasonInfo.CODE_LOCAL_ILLEGAL_STATE);
    }

    /**
     * Determines if answering an incoming call will cause the active call to be disconnected.
     * <p>
     * This will be the case if
     * {@link CarrierConfigManager#KEY_DROP_VIDEO_CALL_WHEN_ANSWERING_AUDIO_CALL_BOOL} is
     * {@code true} for the carrier, the active call is a video call over WIFI, and the incoming
     * call is an audio call.
     *
     * @param activeCall The active call.
     * @param incomingCall The incoming call.
     * @return {@code true} if answering the incoming call will cause the active call to be
     *      disconnected, {@code false} otherwise.
     */
    protected boolean shouldDisconnectActiveCallOnAnswer(ImsCall activeCall,
            ImsCall incomingCall) {

        if (activeCall == null || incomingCall == null) {
            return false;
        }

        if (!mDropVideoCallWhenAnsweringAudioCall) {
            return false;
        }

        boolean isActiveCallVideo = activeCall.isVideoCall() ||
                (mTreatDowngradedVideoCallsAsVideoCalls && activeCall.wasVideoCall());
        boolean isActiveCallOnWifi = activeCall.isWifiCall();
        boolean isVoWifiEnabled = mImsManager.isWfcEnabledByPlatform(mPhone.getContext()) &&
                mImsManager.isWfcEnabledByUser(mPhone.getContext());
        boolean isIncomingCallAudio = !incomingCall.isVideoCall();
        log("shouldDisconnectActiveCallOnAnswer : isActiveCallVideo=" + isActiveCallVideo +
                " isActiveCallOnWifi=" + isActiveCallOnWifi + " isIncomingCallAudio=" +
                isIncomingCallAudio + " isVowifiEnabled=" + isVoWifiEnabled);

        return isActiveCallVideo && isActiveCallOnWifi && isIncomingCallAudio && !isVoWifiEnabled;
    }

    /**
     * Get aggregated video call data usage since boot.
     *
     * @param perUidStats True if requesting data usage per uid, otherwise overall usage.
     * @return Snapshot of video call data usage
     */
    public NetworkStats getVtDataUsage(boolean perUidStats) {

        // If there is an ongoing VT call, request the latest VT usage from the modem. The latest
        // usage will return asynchronously so it won't be counted in this round, but it will be
        // eventually counted when next getVtDataUsage is called.
        if (mState != PhoneConstants.State.IDLE) {
            for (ImsPhoneConnection conn : mConnections) {
                android.telecom.Connection.VideoProvider videoProvider = conn.getVideoProvider();
                if (videoProvider != null) {
                    videoProvider.onRequestConnectionDataUsage();
                }
            }
        }

        return perUidStats ? mVtDataUsageUidSnapshot : mVtDataUsageSnapshot;
    }

    public void registerPhoneStateListener(PhoneStateListener listener) {
        mPhoneStateListeners.add(listener);
    }

    public void unregisterPhoneStateListener(PhoneStateListener listener) {
        mPhoneStateListeners.remove(listener);
    }

    /**
     * Notifies local telephony listeners of changes to the IMS phone state.
     *
     * @param oldState The old state.
     * @param newState The new state.
     */
    private void notifyPhoneStateChanged(PhoneConstants.State oldState,
            PhoneConstants.State newState) {

        for (PhoneStateListener listener : mPhoneStateListeners) {
            listener.onPhoneStateChanged(oldState, newState);
        }
    }

    /** Modify video call to a new video state.
     *
     * @param imsCall IMS call to be modified
     * @param newVideoState New video state. (Refer to VideoProfile)
     */
    protected void modifyVideoCall(ImsCall imsCall, int newVideoState) {
        ImsPhoneConnection conn = findConnection(imsCall);
        if (conn != null) {
            int oldVideoState = conn.getVideoState();
            if (conn.getVideoProvider() != null) {
                conn.getVideoProvider().onSendSessionModifyRequest(
                        new VideoProfile(oldVideoState), new VideoProfile(newVideoState));
            }
        }
    }

    /**
     * Handler of data enabled changed event
     * @param enabled True if data is enabled, otherwise disabled.
     * @param reason Reason for data enabled/disabled (see {@code REASON_*} in
     *      {@link DataEnabledSettings}.
     */
    private void onDataEnabledChanged(boolean enabled, int reason) {

        log("onDataEnabledChanged: enabled=" + enabled + ", reason=" + reason);

        ImsManager.getInstance(mPhone.getContext(), mPhone.getPhoneId()).setDataEnabled(enabled);
        mIsDataEnabled = enabled;

        if (!mIsViLteDataMetered) {
            log("Ignore data " + ((enabled) ? "enabled" : "disabled") + " - carrier policy "
                    + "indicates that data is not metered for ViLTE calls.");
            return;
        }

        // Inform connections that data has been disabled to ensure we turn off video capability
        // if this is an LTE call.
        for (ImsPhoneConnection conn : mConnections) {
            conn.handleDataEnabledChange(enabled);
        }

        int reasonCode;
        if (reason == DataEnabledSettings.REASON_POLICY_DATA_ENABLED) {
            reasonCode = ImsReasonInfo.CODE_DATA_LIMIT_REACHED;
        } else if (reason == DataEnabledSettings.REASON_USER_DATA_ENABLED) {
            reasonCode = ImsReasonInfo.CODE_DATA_DISABLED;
        } else {
            // Unexpected code, default to data disabled.
            reasonCode = ImsReasonInfo.CODE_DATA_DISABLED;
        }

        // Potentially send connection events so the InCall UI knows that video calls are being
        // downgraded due to data being enabled/disabled.
        maybeNotifyDataDisabled(enabled, reasonCode);
        // Handle video state changes required as a result of data being enabled/disabled.
        handleDataEnabledChange(enabled, reasonCode);

        // We do not want to update the ImsConfig for REASON_REGISTERED, since it can happen before
        // the carrier config has loaded and will deregister IMS.
        if (!mShouldUpdateImsConfigOnDisconnect
                && reason != DataEnabledSettings.REASON_REGISTERED) {
            // This will call into updateVideoCallFeatureValue and eventually all clients will be
            // asynchronously notified that the availability of VT over LTE has changed.
            ImsManager.updateImsServiceConfig(mPhone.getContext(), mPhone.getPhoneId(), true);
        }
    }

    private void maybeNotifyDataDisabled(boolean enabled, int reasonCode) {
        if (!enabled) {
            // If data is disabled while there are ongoing VT calls which are not taking place over
            // wifi, then they should be disconnected to prevent the user from incurring further
            // data charges.
            for (ImsPhoneConnection conn : mConnections) {
                ImsCall imsCall = conn.getImsCall();
                if (imsCall != null && imsCall.isVideoCall() && !imsCall.isWifiCall()) {
                    if (conn.hasCapabilities(
                            Connection.Capability.SUPPORTS_DOWNGRADE_TO_VOICE_LOCAL |
                                    Connection.Capability.SUPPORTS_DOWNGRADE_TO_VOICE_REMOTE)) {

                        // If the carrier supports downgrading to voice, then we can simply issue a
                        // downgrade to voice instead of terminating the call.
                        if (reasonCode == ImsReasonInfo.CODE_DATA_DISABLED) {
                            conn.onConnectionEvent(TelephonyManager.EVENT_DOWNGRADE_DATA_DISABLED,
                                    null);
                        } else if (reasonCode == ImsReasonInfo.CODE_DATA_LIMIT_REACHED) {
                            conn.onConnectionEvent(
                                    TelephonyManager.EVENT_DOWNGRADE_DATA_LIMIT_REACHED, null);
                        }
                    }
                }
            }
        }
    }

    /**
     * Handles changes to the enabled state of mobile data.
     * When data is disabled, handles auto-downgrade of video calls over LTE.
     * When data is enabled, handled resuming of video calls paused when data was disabled.
     * @param enabled {@code true} if mobile data is enabled, {@code false} if mobile data is
     *                            disabled.
     * @param reasonCode The {@link ImsReasonInfo} code for the data enabled state change.
     */
    private void handleDataEnabledChange(boolean enabled, int reasonCode) {
        if (!enabled) {
            // If data is disabled while there are ongoing VT calls which are not taking place over
            // wifi, then they should be disconnected to prevent the user from incurring further
            // data charges.
            for (ImsPhoneConnection conn : mConnections) {
                ImsCall imsCall = conn.getImsCall();
                if (imsCall != null && imsCall.isVideoCall() && !imsCall.isWifiCall()) {
                    log("handleDataEnabledChange - downgrading " + conn);
                    downgradeVideoCall(reasonCode, conn);
                }
            }
        } else if (mSupportPauseVideo) {
            // Data was re-enabled, so un-pause previously paused video calls.
            for (ImsPhoneConnection conn : mConnections) {
                // If video is paused, check to see if there are any pending pauses due to enabled
                // state of data changing.
                log("handleDataEnabledChange - resuming " + conn);
                if (VideoProfile.isPaused(conn.getVideoState()) &&
                        conn.wasVideoPausedFromSource(VideoPauseTracker.SOURCE_DATA_ENABLED)) {
                    // The data enabled state was a cause of a pending pause, so potentially
                    // resume the video now.
                    conn.resumeVideo(VideoPauseTracker.SOURCE_DATA_ENABLED);
                }
            }
            mShouldUpdateImsConfigOnDisconnect = false;
        }
    }

    /**
     * Handles downgrading a video call.  The behavior depends on carrier capabilities; we will
     * attempt to take one of the following actions (in order of precedence):
     * 1. If supported by the carrier, the call will be downgraded to an audio-only call.
     * 2. If the carrier supports video pause signalling, the video will be paused.
     * 3. The call will be disconnected.
     * @param reasonCode The {@link ImsReasonInfo} reason code for the downgrade.
     * @param conn The {@link ImsPhoneConnection} to downgrade.
     */
    private void downgradeVideoCall(int reasonCode, ImsPhoneConnection conn) {
        ImsCall imsCall = conn.getImsCall();
        if (imsCall != null) {
            if (conn.hasCapabilities(
                    Connection.Capability.SUPPORTS_DOWNGRADE_TO_VOICE_LOCAL |
                            Connection.Capability.SUPPORTS_DOWNGRADE_TO_VOICE_REMOTE)) {

                // If the carrier supports downgrading to voice, then we can simply issue a
                // downgrade to voice instead of terminating the call.
                modifyVideoCall(imsCall, VideoProfile.STATE_AUDIO_ONLY);
            } else if (mSupportPauseVideo && reasonCode != ImsReasonInfo.CODE_WIFI_LOST
                /// M: ALPS03728528, check if pause video allowed during call initiation
                /// phase or not. @{
                      && isCarrierPauseAllowed(imsCall)) {
                //@}
                // The carrier supports video pause signalling, so pause the video if we didn't just
                // lose wifi; in that case just disconnect.
                mShouldUpdateImsConfigOnDisconnect = true;
                conn.pauseVideo(VideoPauseTracker.SOURCE_DATA_ENABLED);
            } else {
                // At this point the only choice we have is to terminate the call.
                try {
                    imsCall.terminate(ImsReasonInfo.CODE_USER_TERMINATED, reasonCode);
                } catch (ImsException ie) {
                    loge("Couldn't terminate call " + imsCall);
                }
            }
        }
    }

    private void resetImsCapabilities() {
        log("Resetting Capabilities...");
        /// M: ALPS03457877. Google issue, should not force set all feature enabled as false.
        /// Use onfeature capability change or might meet race condition. @{
        //for (int i = 0; i < mImsFeatureEnabled.length; i++) {
        //    mImsFeatureEnabled[i] = false;
        //}
        int [] enabledFeatures = new int[mImsFeatureEnabled.length];
        int [] disabledFeatures = new int[mImsFeatureEnabled.length];
        for (int i = 0; i < mImsFeatureEnabled.length; i++) {
            enabledFeatures[i] = ImsConfig.FeatureConstants.FEATURE_TYPE_UNKNOWN;
            disabledFeatures[i] = ImsConfig.FeatureConstants.FEATURE_TYPE_UNKNOWN;
        }
        SomeArgs args = SomeArgs.obtain();
        args.argi1 = ImsServiceClass.MMTEL;
        args.arg1 = enabledFeatures;
        args.arg2 = disabledFeatures;
        // Remove any pending updates; they're already stale, so no need to process them.
        removeMessages(EVENT_ON_FEATURE_CAPABILITY_CHANGED);
        obtainMessage(EVENT_ON_FEATURE_CAPABILITY_CHANGED, args).sendToTarget();
        //@}
    }

    /**
     * @return {@code true} if the device is connected to a WIFI network, {@code false} otherwise.
     */
    protected boolean isWifiConnected() {
        ConnectivityManager cm = (ConnectivityManager) mPhone.getContext()
                .getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null) {
            NetworkInfo ni = cm.getActiveNetworkInfo();
            if (ni != null && ni.isConnected()) {
                return ni.getType() == ConnectivityManager.TYPE_WIFI;
            }
        }
        return false;
    }

    /**
     * @return {@code true} if downgrading of a video call to audio is supported.
     */
    public boolean isCarrierDowngradeOfVtCallSupported() {
        return mSupportDowngradeVtToAudio;
    }

    private void handleFeatureCapabilityChanged(int serviceClass,
            int[] enabledFeatures, int[] disabledFeatures) {
        if (serviceClass == ImsServiceClass.MMTEL) {
            boolean tmpIsVideoCallEnabled = isVideoCallEnabled();
            // Check enabledFeatures to determine capabilities. We ignore disabledFeatures.
            StringBuilder sb;
            if (DBG) {
                sb = new StringBuilder(120);
                sb.append("handleFeatureCapabilityChanged: ");
            }
            for (int  i = ImsConfig.FeatureConstants.FEATURE_TYPE_VOICE_OVER_LTE;
                    i <= ImsConfig.FeatureConstants.FEATURE_TYPE_UT_OVER_WIFI &&
                            i < enabledFeatures.length; i++) {
                if (enabledFeatures[i] == i) {
                    // If the feature is set to its own integer value it is enabled.
                    if (DBG) {
                        sb.append(mImsFeatureStrings[i]);
                        sb.append(":true ");
                    }

                    mImsFeatureEnabled[i] = true;
                } else if (enabledFeatures[i]
                        == ImsConfig.FeatureConstants.FEATURE_TYPE_UNKNOWN) {
                    // FEATURE_TYPE_UNKNOWN indicates that a feature is disabled.
                    if (DBG) {
                        sb.append(mImsFeatureStrings[i]);
                        sb.append(":false ");
                    }

                    mImsFeatureEnabled[i] = false;
                } else {
                    // Feature has unknown state; it is not its own value or -1.
                    if (DBG) {
                        loge("handleFeatureCapabilityChanged(" + i + ", " + mImsFeatureStrings[i]
                                + "): unexpectedValue=" + enabledFeatures[i]);
                    }
                }
            }
            boolean isVideoEnabled = isVideoCallEnabled();
            boolean isVideoEnabledStatechanged = tmpIsVideoCallEnabled != isVideoEnabled;
            if (DBG) {
                sb.append(" isVideoEnabledStateChanged=");
                sb.append(isVideoEnabledStatechanged);
            }

            if (isVideoEnabledStatechanged) {
                log("handleFeatureCapabilityChanged - notifyForVideoCapabilityChanged=" +
                        isVideoEnabled);
                mPhone.notifyForVideoCapabilityChanged(isVideoEnabled);
            }

            if (DBG) {
                log(sb.toString());
            }

            if (DBG) log("handleFeatureCapabilityChanged: isVolteEnabled=" + isVolteEnabled()
                    + ", isVideoCallEnabled=" + isVideoCallEnabled()
                    + ", isVowifiEnabled=" + isVowifiEnabled()
                    + ", isUtEnabled=" + isUtEnabled());

            mPhone.onFeatureCapabilityChanged();

            mMetrics.writeOnImsCapabilities(
                    mPhone.getPhoneId(), mImsFeatureEnabled);
        }
    }

    /// M: for MTK add-on. @{
    protected ImsPhoneConnection makeImsPhoneConnectionForMO(String dialString, boolean
            isEmergencyNumber) {
        return new ImsPhoneConnection(mPhone, checkForTestEmergencyNumber(dialString), this,
                mForegroundCall, isEmergencyNumber);
    }

    protected ImsPhoneConnection makeImsPhoneConnectionForMT(ImsCall imsCall, boolean isUnknown) {
        return new ImsPhoneConnection(mPhone, imsCall, this, (isUnknown ? mForegroundCall :
                mRingingCall), isUnknown);
    }

    protected ImsCall takeCall(Intent intent) throws ImsException {
        return mImsManager.takeCall(mServiceId, intent, mImsCallListener);
    }

    protected void mtkNotifyRemoteHeld(ImsPhoneConnection conn, boolean held) {
    }

    protected boolean isEmergencyNumber(String dialString) {
        return mPhoneNumberUtilsProxy.isEmergencyNumber(dialString);
    }

    protected void checkforCsfb() throws CallStateException {
    }

    protected void resetFlagWhenSwitchFailed() {
    }

    protected void setPendingResumeRequest(boolean pendingResumeRequest) {
    }

    protected boolean hasPendingResumeRequest() {
        return false;
    }

    protected boolean canDailOnCallTerminated() {
        return mPendingMO != null;
    }

    protected void setRedialAsEcc(int cause) {
    }

    protected void setVendorDisconnectCause(ImsPhoneConnection conn, ImsReasonInfo reasonInfo) {
    }

    protected int updateDisconnectCause(int cause, ImsPhoneConnection conn ) {
        return cause;
    }

    protected void setMultiPartyState(Connection c) {
    }

    protected void resetRingBackTone(ImsPhoneCall call) {
    }

    protected void updateForSrvccCompleted() {
    }

    protected void logDebugMessagesWithOpFormat(
            String category, String action, ImsPhoneConnection conn, String msg) {

    }
    protected void logDebugMessagesWithDumpFormat(String category, ImsPhoneConnection conn,
            String msg) {
    }

    protected void switchWfcModeIfRequired(ImsManager imsManager, boolean isWfcEnabled, boolean
            isEmergencyNumber) {
    }

    protected AsyncResult getCallStateChangeAsyncResult() {
        return new AsyncResult(null, null, null);
    }

    protected boolean isCarrierPauseAllowed(ImsCall imsCall) {
        return true;
    }

    protected void releasePendingMOIfRequired() {
    }
    /// @}

    /**
     * M: check if it is incoming RTT call
     *
     * @param intent ImsManager.ACTION_IMS_INCOMING_CALL intent
     * @param imsCall Current ims call
     * @param conn Current ims phone connection
     */
    protected void checkIncomingRtt(Intent intent, ImsCall imsCall, ImsPhoneConnection conn) {
    }

    /**
     * M: check RTT call type
     */
    protected void checkRttCallType() {
    }
    /**
     * M: Start RTT EMC Guard Timer
     */
    protected void startRttEmcGuardTimer() {
    }

    /**
     * M: Return the VtInferface
     */
    protected String getVtInterface() {
        return NetworkStatsService.VT_INTERFACE;
    }
}
