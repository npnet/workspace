#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>  // struct sockaddr_in
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <cutils/properties.h>

#include "mnld.h"
#include "mnld_utile.h"
#include "mnl_common.h"
#include "mtk_gps.h"
#include "agps_agent.h"
#include "mtk_lbs_utility.h"
#include "mnl2hal_interface.h"
#include "mnl2agps_interface.h"
#include "data_coder.h"
#include "gps_controller.h"
#include "epo.h"
#include "qepo.h"
#include "mtknav.h"
#include "op01_log.h"
#ifdef CONFIG_GPS_MT3333
#include "mt3333_controller.h"
#endif
#include "mtk_flp_main.h"
#include "mnl_flp_test_interface.h"
#include "mtk_geofence_main.h"
#include "gps_dbg_log.h"
#include "mpe.h"
#include "mnl_at_interface.h"
#include "Mnld2NlpUtilsInterface.h"

#ifdef LOGD
#undef LOGD
#endif
#ifdef LOGW
#undef LOGW
#endif
#ifdef LOGE
#undef LOGE
#endif
#if 0
#define LOGD(...) tag_log(1, "[mnld]", __VA_ARGS__);
#define LOGW(...) tag_log(1, "[mnld] WARNING: ", __VA_ARGS__);
#define LOGE(...) tag_log(1, "[mnld] ERR: ", __VA_ARGS__);
#else
#define LOG_TAG "MNLD"
#include <cutils/sockets.h>
#include <log/log.h>     /*logging in logcat*/
#define LOGD(fmt, arg ...) ALOGD("%s: " fmt, __FUNCTION__ , ##arg)
#define LOGW(fmt, arg ...) ALOGW("%s: " fmt, __FUNCTION__ , ##arg)
#define LOGE(fmt, arg ...) ALOGE("%s: " fmt, __FUNCTION__ , ##arg)
#endif

static void mnld_fsm(mnld_gps_event event, int data1, int data2, void* data3);


static mnld_context g_mnld_ctx;
UINT8 sv_type_agps_set = 0;
UINT8 sib8_16_enable = 0;
UINT8 lppe_enable = 0;// if mnl support lppe, use this variable to determin on or off, else, it is invalid
mnl_agps_agps_settings g_settings_from_agps;
MTK_GPS_MNL_INFO mtk_gps_mnl_info;// include lib information:vertion and if support lppe
mnl_agps_gnss_settings g_settings_to_agps = {
    .gps_satellite_support     = 1,
    .glonass_satellite_support = 0,
    .beidou_satellite_support  = 0,
    .galileo_satellite_support = 0,
};
extern int gps_epo_type;
#if ANDROID_MNLD_PROP_SUPPORT
extern char chip_id[PROPERTY_VALUE_MAX];
#else
extern char chip_id[100];
#endif

/*****************************************
MNLD FSM
*****************************************/
static const char* get_mnld_gps_state_str(mnld_gps_state input) {
    switch (input) {
    case MNLD_GPS_STATE_IDLE:
        return "IDLE";
    case MNLD_GPS_STATE_STARTING:
        return "STARTING";
    case MNLD_GPS_STATE_STARTED:
        return "STARTED";
    case MNLD_GPS_STATE_STOPPING:
        return "STOPPING";
    case MNLD_GPS_STATE_OFL_STARTING:
        return "OFL_STARTING";
    case MNLD_GPS_STATE_OFL_STARTED:
        return "OFL_STARTED";
    case MNLD_GPS_STATE_OFL_STOPPING:
        return "OFL_STOPPING";
    default:
        break;
    }
    return "UNKNOWN";
}

static const char* get_mnld_gps_event_str(mnld_gps_event input) {
    switch (input) {
    case GPS_EVENT_START:
        return "START";
    case GPS_EVENT_STOP:
        return "STOP";
    case GPS_EVENT_RESET:
        return "RESET";
    case GPS_EVENT_START_DONE:
        return "START_DONE";
    case GPS_EVENT_STOP_DONE:
        return "STOP_DONE";
    case GPS_EVENT_OFFLOAD_START:
        return "OFFLOAD_START";
    default:
        break;
    }
    return "UNKNOWN";
}

static void do_gps_reset_hdlr() {
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    if (agps->need_reset_ack) {
        agps->need_reset_ack = false;
        mnl2agps_reset_gps_done();
    }
}

static void do_gps_started_hdlr(int si_assist_req) {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (hal->need_open_ack) {
        hal->need_open_ack = false;
        mnl2hal_gps_status(MTK_GPS_STATUS_SESSION_ENGINE_ON);
        mnl2hal_gps_status(MTK_GPS_STATUS_SESSION_BEGIN);
        mnl2agps_gps_open(si_assist_req);
    }

    if (agps->need_open_ack) {
        agps->need_open_ack = false;
        mnl2agps_open_gps_done();
    }

    if (flp->need_open_ack) {
        flp->need_open_ack = false;
        /*add open ack message to flp*/
        LOGD("MNLD_FLP_DEBUG:support_auto_switch:%d,offload_auto:%d",mnld_support_auto_switch_mode(),mnld_offload_is_auto_mode());
        if (mnld_support_auto_switch_mode()) {   //New offload auto switch mode
            LOGD("Auto switch mode");
            if (mnld_offload_is_auto_mode()) {
                LOGD("Offload Auto mode");
                mnld_flp_ofl_gps_open_done();
            } else {
                LOGD("HBD auto mode");
                mnld_flp_hbd_gps_open_done();
            }
        } else { //always host
            LOGD("HBD auto mode");
            mnld_flp_hbd_gps_open_done();
        }
        mnl2agps_gps_open(si_assist_req);
    }

    if (geofence->need_open_ack) {
        geofence->need_open_ack = false;
        /*add open ack message to gfc*/
        LOGD("MNLD_GFC_DEBUG:support_auto_switch:%d,offload_auto:%d",mnld_support_auto_switch_mode(),mnld_offload_is_auto_mode());
        if (mnld_support_auto_switch_mode()) {   //New offload auto switch mode
            LOGD("Auto switch mode");
            if (mnld_offload_is_auto_mode()) {
                LOGD("Offload Auto mode");
                mnld_gfc_ofl_gps_open_done();
            } else {
                LOGD("HBD auto mode");
                mnld_gfc_hbd_gps_open_done();
            }
        } else { //always host
            LOGD("HBD auto mode");
            mnld_gfc_hbd_gps_open_done();
        }
        mnl2agps_gps_open(si_assist_req);
    }

    if (flp_test->need_open_ack) {
        flp_test->need_open_ack = false;
        /*add open ack message to flp_test*/
    }

    if (at_cmd_test->need_open_ack) {
        at_cmd_test->need_open_ack = false;
        /*add open ack message to at_cmd_test*/
    }

    if (factory->need_open_ack) {
        factory->need_open_ack = false;
        /*add open ack message to factory*/
    }
    do_gps_reset_hdlr();

    if (g_mnld_ctx.gps_status.delete_aiding_flag) {
        start_timer(g_mnld_ctx.gps_status.timer_reset, MNLD_GPS_RESET_TIMEOUT);
        gps_control_gps_reset(g_mnld_ctx.gps_status.delete_aiding_flag);
        g_mnld_ctx.gps_status.delete_aiding_flag = 0;
    }
}

static void do_gps_stopped_hdlr() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    if (hal->need_close_ack) {
        hal->need_close_ack = false;
        mnl2hal_gps_status(MTK_GPS_STATUS_SESSION_END);
        mnl2hal_gps_status(MTK_GPS_STATUS_SESSION_ENGINE_OFF);
        mnl2agps_gps_close();
    }

    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    if (agps->need_close_ack) {
        agps->need_close_ack = false;
        mnl2agps_close_gps_done();
    }

    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    if (flp->need_close_ack) {
        flp->need_close_ack = false;
        /* add close ack message to flp*/
        LOGD("MNLD_FLP_DEBUG:support_auto_switch:%d,offload_auto:%d",mnld_support_auto_switch_mode(),mnld_offload_is_auto_mode());
        if (mnld_support_auto_switch_mode()) {   //New offload auto switch mode
            LOGD("Auto switch mode");
            if (mnld_offload_is_auto_mode()) {
                LOGD("Offload Auto mode");
                mnld_flp_ofl_gps_close_done();
            } else {
                LOGD("HBD auto mode");
                mnld_flp_hbd_gps_close_done();
            }
        } else { //always host
            LOGD("HBD auto mode");
            mnld_flp_hbd_gps_close_done();
        }
    }

    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    if (geofence->need_close_ack) {
        geofence->need_close_ack = false;
        /* add close ack message to geofence*/
        LOGD("MNLD_GFC_DEBUG:support_auto_switch:%d,offload_auto:%d",mnld_support_auto_switch_mode(),mnld_offload_is_auto_mode());
        if (mnld_support_auto_switch_mode()) {   //New offload auto switch mode
            LOGD("Auto switch mode");
            if (mnld_offload_is_auto_mode()) {
                LOGD("Offload Auto mode");
                mnld_gfc_ofl_gps_close_done();
            } else {
                LOGD("HBD auto mode");
                mnld_gfc_hbd_gps_close_done();
            }
        } else {
            LOGD("HBD auto mode");
            mnld_gfc_hbd_gps_close_done();
        }
    }

    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    if (flp_test->need_close_ack) {
        flp_test->need_close_ack = false;
        /* add close ack message to flp_test*/
    }

    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    if (at_cmd_test->need_close_ack) {
        at_cmd_test->need_close_ack = false;
        /* add close ack message to at_cmd_test*/
    }

    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (factory->need_close_ack) {
        factory->need_close_ack = false;
        /* add close ack message to factory*/
    }

    Mnld2NlpUtilsInterface_cancelNlpLocation(&g_mnld_ctx.fds.fd_nlp_utils, NLP_REQUEST_SRC_MNL);

    do_gps_reset_hdlr();
}

static bool is_all_gps_client_exit() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (hal->gps_used == false && agps->gps_used == false && flp->gps_used == false
        && flp_test->gps_used == false && at_cmd_test->gps_used == false && factory->gps_used == false
 && geofence->gps_used == false) {
        return true;
    } else {
        return false;
    }
}

static bool is_a_gps_client_exist() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (hal->gps_used == true || agps->gps_used == true || flp->gps_used == true || flp_test->gps_used == true
        || at_cmd_test->gps_used == true || factory->gps_used == true || geofence->gps_used == true) {
        return true;
    } else {
        return false;
    }
}

void mtk_gps_clear_gps_user() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;

    if (hal->gps_used)
        hal->gps_used = false;
    if (agps->gps_used)
        agps->gps_used = false;
    if (flp->gps_used)
        flp->gps_used = false;
    if (flp_test->gps_used)
        flp_test->gps_used = false;
    if (geofence->gps_used)
        geofence->gps_used = false;
    if (at_cmd_test->gps_used)
        at_cmd_test->gps_used = false;
    if (factory->gps_used)
        factory->gps_used = false;
}
int mtk_gps_get_gps_user() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    int gps_user = GPS_USER_UNKNOWN;

    if (hal->gps_used)
        gps_user |= GPS_USER_APP;
    if (agps->gps_used)
        gps_user |= GPS_USER_AGPS;
    if (flp->gps_used)
        gps_user |= GPS_USER_FLP;
    if (flp_test->gps_used)
        gps_user |= GPS_USER_OFL_TEST;
    if (geofence->gps_used)
        gps_user |= GPS_USER_GEOFENCE;
    if (at_cmd_test->gps_used)
        gps_user |= GPS_USER_AT_CMD;
    if (factory->gps_used)
        gps_user |= GPS_USER_META;
    return gps_user;
}

int is_flp_user_exist() {
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    return flp->gps_used;
}

int is_geofence_user_exist() {
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    return geofence->gps_used;
}


static bool is_all_hbd_gps_client_exit() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (hal->gps_used == false && agps->gps_used == false && at_cmd_test->gps_used == false
        && factory->gps_used == false) {
        return true;
    } else {
        return false;
    }
}

static bool is_a_hbd_gps_client_exist() {
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    mnld_gps_client* at_cmd_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    if (hal->gps_used == true || agps->gps_used == true || at_cmd_test->gps_used == true
        || factory->gps_used == true) {
        return true;
    } else {
        return false;
    }
}

extern int start_time_out;
extern int nmea_data_time_out;
static void fsm_gps_state_idle(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGD("fsm_gps_state_idle() data1=%d,data2=%d,data3=%p\n", data1, data2, data3);
    switch (event) {
    case GPS_EVENT_START: {
        if (!(mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode()))) {
            mnl2hal_request_wakelock();
        }
        #if ANDROID_MNLD_PROP_SUPPORT
        if (get_gps_cmcc_log_enabled()) {
            op01_log_gps_start();
        }
        #else
        op01_log_gps_start();
        #endif
        g_mnld_ctx.gps_status.gps_start_time = get_tick();
        g_mnld_ctx.gps_status.wait_first_location = true;
        g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STARTING;
        int gps_user = mtk_gps_get_gps_user();
        // if no other users except GPS_USER_FLP or GPS_USER_OFL_TEST, bypass restart
        if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == gps_user) && mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            LOGE("gps_user: %d, not run MNL start timer\n", gps_user);
            LOGE("still enable start timer");
            start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
        } else {
        	LOGE("GPS_EVENT_START: g_mnld_ctx.gps_status.timer_start");
            start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
        }
        gps_control_gps_start(g_mnld_ctx.gps_status.delete_aiding_flag);
        g_mnld_ctx.gps_status.delete_aiding_flag = 0;
		mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_REQUES1STNMEA);
        break;
    }
    case GPS_EVENT_STOP: {
        do_gps_stopped_hdlr();
        break;
    }
    case GPS_EVENT_RESET: {
        do_gps_reset_hdlr();
        break;
    }
    case GPS_EVENT_OFFLOAD_START: {
        if (!(mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode()))) {
            mnl2hal_request_wakelock();
        }
        #if ANDROID_MNLD_PROP_SUPPORT
        if (get_gps_cmcc_log_enabled()) {
            op01_log_gps_start();
        }
        #else
        op01_log_gps_start();
        #endif
        g_mnld_ctx.gps_status.gps_start_time = get_tick();
        g_mnld_ctx.gps_status.wait_first_location = true;
        g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STARTING;
        int gps_user = mtk_gps_get_gps_user();
        // if no other users except GPS_USER_FLP or GPS_USER_OFL_TEST, bypass restart
        if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == gps_user) && mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            LOGE("gps_user: %d, not run MNL start timer\n", gps_user);
            LOGE("still enable start timer");
            start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
        } else {
        	LOGE("GPS_EVENT_OFFLOAD_START: g_mnld_ctx.gps_status.timer_start");
            start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
        }
        gps_control_gps_start(g_mnld_ctx.gps_status.delete_aiding_flag);
        g_mnld_ctx.gps_status.delete_aiding_flag = 0;
        break;
    }
    case GPS_EVENT_START_DONE:
        //Need fix by MNL,it will send MTK_AGPS_CB_START_REQ to mnld when stopping or started.
        LOGE("fsm_gps_state_stopping() unexpected event=%d", event);
        break;
    case GPS_EVENT_STOP_DONE:
    default: {
        LOGE("fsm_gps_state_idle() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_starting(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGD("fsm_gps_state_starting() data2=%d,data3=%p\n", data2, data3);
    switch (event) {
    case GPS_EVENT_STOP: {
        // Need to handle starting-stop event for FLP offload like started-stop
        if (mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            #if 1
            LOGE("fsm_gps_state_starting, !NOT! handle starting stop event for offload");
            #else
            LOGE("fsm_gps_state_starting, handle starting stop event for offload");
            do_gps_started_hdlr(0);
            if (is_all_gps_client_exit()) {
                g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STOPPING;
                start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
                gps_control_gps_stop();
            }
            #endif
        }
        break;
    }
    case GPS_EVENT_START:
    case GPS_EVENT_OFFLOAD_START:
    case GPS_EVENT_RESET: {
        // do nothing
        break;
    }
    case GPS_EVENT_START_DONE: {
		LOGD("GPS_EVENT_START_DONE: g_mnld_ctx.gps_status.timer_start\n");
        stop_timer(g_mnld_ctx.gps_status.timer_start);
        do_gps_started_hdlr(data1);
        if (is_all_gps_client_exit()) {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STOPPING;
            start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
            gps_control_gps_stop();
        } else {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STARTED;
            //Update mtknav and qepo if download finish before start done.
            if (mtknav_update_flag == true) {
                mtknav_update_mtknav_file(mtknav_res);
                mtknav_update_flag = false;
            }
            if (qepo_update_flag == true) {
                qepo_update_quarter_epo_file(qepo_dl_res);
                qepo_update_flag = false;
            }
            if (qepo_BD_update_flag == true) {
                qepo_update_quarter_epo_bd_file(qepo_bd_dl_res);
                qepo_BD_update_flag = false;
            }
            if (is_a_hbd_gps_client_exist() || !(mnld_support_auto_switch_mode())) {
                mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_MODE;
                mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_MODE;
                if (mnld_flp_session.type != mnld_flp_session.pre_type) {
                    mnld_flp_session.id = mnld_flp_gen_new_session();
                    mnld_flp_session.pre_type = mnld_flp_session.type;
                    g_mnld_ctx.gps_status.clients.flp.gps_used = false;
                    mnld_flp_session_update();
                }
                if (mnld_gfc_session.type != mnld_gfc_session.pre_type) {
                    mnld_gfc_session.id = mnld_gfc_gen_new_session();
                    mnld_gfc_session.pre_type = mnld_gfc_session.type;
                    g_mnld_ctx.gps_status.clients.geofence.gps_used = false;
                    mnld_gfc_session_update();
                }
            } else {
                mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_MODE;
                mnld_flp_session.pre_type = mnld_flp_session.type;
                mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_MODE;
                mnld_gfc_session.pre_type = mnld_gfc_session.type;
                start_timer(g_mnld_ctx.gps_status.timer_switch_ofl_mode, MNLD_GPS_SWITCH_OFL_MODE_TIMEOUT);
            }
        }
        break;
    }
    case GPS_EVENT_STOP_DONE:
    default: {
        LOGE("fsm_gps_state_starting() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_started(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGE("fsm_gps_state_started() data1=%d,data2=%d,data3=%p\n", data1, data2, data3);
    switch (event) {
    case GPS_EVENT_START: {
        do_gps_started_hdlr(0);
        break;
    }
    case GPS_EVENT_STOP: {
        if (is_all_gps_client_exit()) {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STOPPING;
            start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
            gps_control_gps_stop();
        } else {
            do_gps_stopped_hdlr();
            if (is_all_hbd_gps_client_exit() && mnld_support_auto_switch_mode()) {
                start_timer(g_mnld_ctx.gps_status.timer_switch_ofl_mode, MNLD_GPS_SWITCH_OFL_MODE_TIMEOUT);
            }
        }
        break;
    }
    case GPS_EVENT_RESET: {
        if (g_mnld_ctx.gps_status.delete_aiding_flag) {
            start_timer(g_mnld_ctx.gps_status.timer_reset, MNLD_GPS_RESET_TIMEOUT);
            gps_control_gps_reset(g_mnld_ctx.gps_status.delete_aiding_flag);
            g_mnld_ctx.gps_status.delete_aiding_flag = 0;
        }
        break;
    }
    case GPS_EVENT_START_DONE:
        // MNL restart.
        if (GPS_USER_APP & mtk_gps_get_gps_user()) {
            mnl2agps_reaiding_req();
        }
        LOGE("fsm_gps_state_started() unexpected event=%d", event);
        break;
    case GPS_EVENT_OFFLOAD_START:
        // Do nothing
        LOGE("fsm_gps_state_started() unexpected event=%d", event);
        break;
    case GPS_EVENT_STOP_DONE:
    default: {
        LOGE("fsm_gps_state_started() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_stopping(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGE("fsm_gps_state_stopping() data1=%d,data2=%d,data3=%p\n", data1, data2, data3);
    switch (event) {
    case GPS_EVENT_START:
    case GPS_EVENT_STOP: {
        // do nothing
        break;
    }
    case GPS_EVENT_RESET: {
        do_gps_reset_hdlr();
        break;
    }
    case GPS_EVENT_STOP_DONE: {
        stop_timer(g_mnld_ctx.gps_status.timer_stop);
        do_gps_stopped_hdlr();
        if (is_a_gps_client_exist()) {
            LOGD("OFFLOAD debug: restarting");
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STARTING;
            int gps_user = mtk_gps_get_gps_user();
            // if no other users except GPS_USER_FLP or GPS_USER_OFL_TEST, bypass restart
            if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == gps_user) && mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
                LOGE("gps_user: %d\n", gps_user);
            } else {
            	LOGE("GPS_EVENT_STOP_DONE: g_mnld_ctx.gps_status.timer_start");
                start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
            }
            gps_control_gps_start(g_mnld_ctx.gps_status.delete_aiding_flag);
            g_mnld_ctx.gps_status.delete_aiding_flag = 0;
        } else {
            LOGD("OFFLOAD debug: stop done");
            if (!(mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode()))) {
                mnl2hal_release_wakelock();
            }
            #if ANDROID_MNLD_PROP_SUPPORT
            if (get_gps_cmcc_log_enabled()) {
                op01_log_gps_stop();
            }
            #else
            op01_log_gps_stop();
            #endif
            g_mnld_ctx.gps_status.gps_stop_time = get_tick();
            g_mnld_ctx.gps_status.wait_first_location = false;
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_IDLE;
            LOGD("OFFLOAD debug: update session");

            if(mnld_offload_is_always_on_mode()) {
            mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_OFL_MODE;
            if (mnld_flp_session.type != mnld_flp_session.pre_type) {
                mnld_flp_session.id = mnld_flp_gen_new_session();
                mnld_flp_session.pre_type = mnld_flp_session.type;
                g_mnld_ctx.gps_status.clients.flp.gps_used = false;
                mnld_flp_session_update();
            }
            mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_OFL_MODE;
            if (mnld_gfc_session.type != mnld_gfc_session.pre_type) {
                mnld_gfc_session.id = mnld_gfc_gen_new_session();
                mnld_gfc_session.pre_type = mnld_gfc_session.type;
                g_mnld_ctx.gps_status.clients.geofence.gps_used = false;
                mnld_gfc_session_update();
            }
        }
        }
        break;
    }
    case GPS_EVENT_OFFLOAD_START:
    case GPS_EVENT_START_DONE:
        // MNL restart.
        LOGE("fsm_gps_state_stopping() unexpected event=%d", event);
        break;
    default: {
        LOGE("fsm_gps_state_stopping() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_ofl_starting(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGD("fsm_gps_state_starting() data2=%d,data3=%p\n", data2, data3);
    switch (event) {
    case GPS_EVENT_STOP: {
        // Need to handle starting-stop event for FLP offload like started-stop
        if (mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            #if 1
            LOGE("fsm_gps_state_starting, !NOT! handle starting stop event for offload");
            #else
            LOGE("fsm_gps_state_starting, handle starting stop event for offload");
            do_gps_started_hdlr(0);
            if (is_all_gps_client_exit()) {
                g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_STOPPING;
                start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
                gps_control_gps_stop();
            }
            #endif
        }
        break;
    }
    case GPS_EVENT_START:
    case GPS_EVENT_OFFLOAD_START:
    case GPS_EVENT_RESET: {
        // do nothing
        break;
    }
    case GPS_EVENT_START_DONE: {
		LOGE("GPS_EVENT_START_DONE: g_mnld_ctx.gps_status.timer_start");
        stop_timer(g_mnld_ctx.gps_status.timer_start);
        do_gps_started_hdlr(data1);
        if (is_all_gps_client_exit() || is_a_hbd_gps_client_exist()) {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STOPPING;
            start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
            gps_control_gps_stop();
        } else {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STARTED;
            mnld_flp_session.type = MNLD_FLP_CAPABILITY_OFL_MODE;
            mnld_gfc_session.type = MNLD_GFC_CAPABILITY_OFL_MODE;
        }
        break;
    }
    case GPS_EVENT_STOP_DONE:
    default: {
        LOGE("fsm_gps_state_starting() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_ofl_started(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGE("fsm_gps_state_started() data1=%d,data2=%d,data3=%p\n", data1, data2, data3);
    switch (event) {
    case GPS_EVENT_START: {
        do_gps_started_hdlr(0);
        if (is_a_hbd_gps_client_exist()) {
            //flp
            g_mnld_ctx.gps_status.clients.flp.gps_used = false;
            g_mnld_ctx.gps_status.clients.flp.need_open_ack = false;
            g_mnld_ctx.gps_status.clients.flp.need_close_ack = false;
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STOPPING;
            //geofence
            g_mnld_ctx.gps_status.clients.geofence.gps_used = false;
            g_mnld_ctx.gps_status.clients.geofence.need_open_ack = false;
            g_mnld_ctx.gps_status.clients.geofence.need_close_ack = false;
            start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
            gps_control_gps_stop();
        }
        break;
    }
    case GPS_EVENT_STOP: {
        if (is_flp_user_exist() || is_geofence_user_exist()) {
            do_gps_stopped_hdlr();
            LOGW("GPS_EVENT_STOP cmd sent by error user!!");
        } else {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STOPPING;
            start_timer(g_mnld_ctx.gps_status.timer_stop, MNLD_GPS_STOP_TIMEOUT);
            gps_control_gps_stop();
        }
        break;
    }
    case GPS_EVENT_RESET: {
        //if (g_mnld_ctx.gps_status.delete_aiding_flag) {
        //    start_timer(g_mnld_ctx.gps_status.timer_reset, MNLD_GPS_RESET_TIMEOUT);
        //    gps_control_gps_reset(g_mnld_ctx.gps_status.delete_aiding_flag);
        //    g_mnld_ctx.gps_status.delete_aiding_flag = 0;
        //}
        break;
    }
    case GPS_EVENT_START_DONE:
        // MNL restart.
        if (GPS_USER_APP & mtk_gps_get_gps_user()) {
            mnl2agps_reaiding_req();
        }
        LOGE("fsm_gps_state_started() unexpected event=%d", event);
        break;
    case GPS_EVENT_OFFLOAD_START:
        LOGE("fsm_gps_state_started() unexpected event=%d", event);
        break;
    case GPS_EVENT_STOP_DONE:
    default: {
        LOGE("fsm_gps_state_started() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

static void fsm_gps_state_ofl_stopping(mnld_gps_event event, int data1, int data2, void* data3) {
    LOGE("fsm_gps_state_stopping() data1=%d,data2=%d,data3=%p\n", data1, data2, data3);
    switch (event) {
    case GPS_EVENT_START:
    case GPS_EVENT_STOP: {
        // do nothing
        break;
    }
    case GPS_EVENT_RESET: {
        do_gps_reset_hdlr();
        break;
    }
    case GPS_EVENT_STOP_DONE: {
        stop_timer(g_mnld_ctx.gps_status.timer_stop);
        do_gps_stopped_hdlr();
        if ((is_flp_user_exist() || is_geofence_user_exist()) && is_all_hbd_gps_client_exit()) {
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_OFL_STARTING;
            int gps_user = mtk_gps_get_gps_user();
            // if no other users except GPS_USER_FLP or GPS_USER_OFL_TEST, bypass restart
            if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST| GPS_USER_GEOFENCE)) == gps_user) && mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
                LOGE("gps_user: %d\n", gps_user);
            } else {
            	LOGE("GPS_EVENT_OFFLOAD_START: g_mnld_ctx.gps_status.timer_start");
                start_timer(g_mnld_ctx.gps_status.timer_start, start_time_out);
            }
            gps_control_gps_start(g_mnld_ctx.gps_status.delete_aiding_flag);
            g_mnld_ctx.gps_status.delete_aiding_flag = 0;
        } else {
            if (!(mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode()))) {
                mnl2hal_release_wakelock();
            }
            #if ANDROID_MNLD_PROP_SUPPORT
            if (get_gps_cmcc_log_enabled()) {
                op01_log_gps_stop();
            }
            #else
            op01_log_gps_stop();
            #endif
            g_mnld_ctx.gps_status.gps_stop_time = get_tick();
            g_mnld_ctx.gps_status.wait_first_location = false;
            g_mnld_ctx.gps_status.gps_state = MNLD_GPS_STATE_IDLE;

            if (is_a_hbd_gps_client_exist()) {  //some hbd user exist, must switch to AP mode and start gps
                mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_MODE;
                if (mnld_flp_session.type != mnld_flp_session.pre_type) {
                    mnld_flp_session.id = mnld_flp_gen_new_session();
                    mnld_flp_session.pre_type = mnld_flp_session.type;
                    g_mnld_ctx.gps_status.clients.flp.gps_used = false;
                    mnld_flp_session_update();
                }
                mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_MODE;
                if (mnld_gfc_session.type != mnld_gfc_session.pre_type) {
                    mnld_gfc_session.id = mnld_gfc_gen_new_session();
                    mnld_gfc_session.pre_type = mnld_gfc_session.type;
                    g_mnld_ctx.gps_status.clients.geofence.gps_used = false;
                    mnld_gfc_session_update();
                }
                mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
            } else {   // flp user exit normally and no user exist, so switch to idle state directly
                if(mnld_support_auto_switch_mode()) {
                mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_OFL_MODE;
                if (mnld_flp_session.type != mnld_flp_session.pre_type) {
                    mnld_flp_session.id = mnld_flp_gen_new_session();
                    mnld_flp_session.pre_type = mnld_flp_session.type;
                    g_mnld_ctx.gps_status.clients.flp.gps_used = false;
                    mnld_flp_session_update();
                }
                mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_OFL_MODE;
                if (mnld_gfc_session.type != mnld_gfc_session.pre_type) {
                    mnld_gfc_session.id = mnld_gfc_gen_new_session();
                    mnld_gfc_session.pre_type = mnld_gfc_session.type;
                    g_mnld_ctx.gps_status.clients.geofence.gps_used = false;
                    mnld_gfc_session_update();
                }
            }
        }
        }
        break;
    }
    case GPS_EVENT_OFFLOAD_START:
    case GPS_EVENT_START_DONE:
        // MNL restart.
        LOGE("fsm_gps_state_stopping() unexpected event=%d", event);
        break;
    default: {
        LOGE("fsm_gps_state_stopping() unexpected gps_event=%d", event);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

int mnld_gps_controller_mnl_nmea_timeout(void) {
    if (g_mnld_ctx.gps_status.timer_nmea_monitor != 0) {
        // stop_timer(g_mnld_ctx.gps_status.timer_nmea_monitor);
        start_timer(g_mnld_ctx.gps_status.timer_nmea_monitor, nmea_data_time_out);
    }
    return 0;
}

int mnld_gps_start_nmea_monitor() {
    if (g_mnld_ctx.gps_status.timer_nmea_monitor != 0) {
        start_timer(g_mnld_ctx.gps_status.timer_nmea_monitor, nmea_data_time_out);
    }
    return 0;
}

int mnld_gps_stop_nmea_monitor() {
    if (g_mnld_ctx.gps_status.timer_nmea_monitor != 0) {
        stop_timer(g_mnld_ctx.gps_status.timer_nmea_monitor);
    }
    return 0;
}

static int mnld_fsm_offload_old_user = 0;
static int mnld_fsm_offload_lp_mode = 0;
static int mnld_fsm_offload_fwk_wakelock = 0;
static void mnld_fsm(mnld_gps_event event, int data1, int data2, void* data3) {
    mnld_gps_state gps_state = g_mnld_ctx.gps_status.gps_state;

    if (!(mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode()))) {
        LOGD("mnld_fsm() state=[%s] event=[%s]",
            get_mnld_gps_state_str(gps_state), get_mnld_gps_event_str(event));
    } else {
        // Wakelock control for FLP offload user
        int user_bitmap;
        user_bitmap = mtk_gps_get_gps_user();
        LOGD("mnld_fsm() state=[%s] event=[%s], user=0x%08x, old user=0x%08x",
            get_mnld_gps_state_str(gps_state), get_mnld_gps_event_str(event),
            user_bitmap, mnld_fsm_offload_old_user);

        // Libmnl.so need to know once the users changed, then
        // it can notify connsys to enter low power mode or NMEA mode
        mtk_gps_ofl_set_user(user_bitmap);

        // If only FLP offload user or No user, should stop nmea timer
        if ((user_bitmap & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == user_bitmap) {
            if (!mnld_fsm_offload_lp_mode) {
                LOGD("mnld_fsm() enter offload lp mode");
                mnld_gps_stop_nmea_monitor();
                gps_control_kernel_wakelock_give();
                if (mnld_fsm_offload_fwk_wakelock) {
                    mnl2hal_release_wakelock();
                    mnld_fsm_offload_fwk_wakelock = 0;
                }
                mnld_fsm_offload_lp_mode = 1;
            }
        } else {
            if (mnld_fsm_offload_lp_mode) {
                LOGD("mnld_fsm() leave offload lp mode");
                mnl2hal_request_wakelock();
                gps_control_kernel_wakelock_take();
                mnld_gps_start_nmea_monitor();
                mnld_fsm_offload_lp_mode = 0;
                mnld_fsm_offload_fwk_wakelock = 1;
            }
        }
        mnld_fsm_offload_old_user = user_bitmap;
    }

    switch (gps_state) {
    case MNLD_GPS_STATE_IDLE: {
        fsm_gps_state_idle(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_STARTING: {
        fsm_gps_state_starting(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_STARTED: {
        fsm_gps_state_started(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_STOPPING: {
        fsm_gps_state_stopping(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_OFL_STARTING: {
        fsm_gps_state_ofl_starting(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_OFL_STARTED: {
        fsm_gps_state_ofl_started(event, data1, data2, data3);
        break;
    }
    case MNLD_GPS_STATE_OFL_STOPPING: {
        fsm_gps_state_ofl_stopping(event, data1, data2, data3);
        break;
    }
    default: {
        LOGE("mnld_fsm() unexpected gps_state=%d", gps_state);
        CRASH_TO_DEBUG();
        break;
    }
    }
}

/*****************************************
HAL -> MNL
*****************************************/
static void hal_reboot() {
    LOGW("hal_reboot");
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    hal->gps_used = false;
    hal->need_open_ack = false;
    hal->need_close_ack = false;
    hal->need_reset_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GPSREBOOT);
#endif		
}

static void hal_gps_init() {
    LOGW("hal_gps_init");
    g_mnld_ctx.gps_status.is_gps_init = true;
    mnl2agps_gps_init();
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GPSENABLE);
#endif
}

static void hal_gps_start() {
    LOGW("hal_gps_start");
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    hal->gps_used = true;
    hal->need_open_ack = true;
    hal->need_close_ack = false;
    hal_start_gps_trigger_epo_download();
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GPSSTART);
#endif		
}

static void hal_gps_stop() {
    LOGW("hal_gps_stop");
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    hal->gps_used = false;
    hal->need_open_ack = false;
    hal->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GPSSTOP);
#endif		
}

static void hal_gps_cleanup() {
    LOGW("hal_gps_cleanup");
    g_mnld_ctx.gps_status.is_gps_init = false;
    mnl2agps_gps_cleanup();
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GPSDISABLE);
#endif	
}

static void hal_gps_inject_time(int64_t time, int64_t time_reference, int uncertainty) {
    LOGD("hal_gps_inject_time  time=%llu time_reference=%llu uncertainty=%d",
        time, time_reference, uncertainty);
    // TODO libmnl.so
    ntp_context  ntp_inject;

    memset(&ntp_inject, 0, sizeof(ntp_context));
    ntp_inject.time = time;
    ntp_inject.timeReference = time_reference;
    ntp_inject.uncertainty = uncertainty;
    if (mnld_is_gps_started_done()) {
#ifndef CONFIG_GPS_MT3333		
        mtk_gps_inject_ntp_time((MTK_GPS_NTP_T*)&ntp_inject);	
#else
		mt3333_controller_inject_time(time, time_reference, uncertainty);
#endif
    }
}

static void hal_gps_inject_location(double lat, double lng, float acc) {
    int ret = 0;
    MTK_GPS_NLP_T nlp_inject;
    nlp_context context;
    bool alt_valid = false;
    float altitude = 0.0f;
    bool source_valid = true;
    bool source_gnss = false;
    bool source_nlp = true;
    bool source_sensor = false;

    // LOGW("lat=%f lng=%f acc=%f", lat, lng, acc);
    memset(&nlp_inject, 0, sizeof(MTK_GPS_NLP_T));
    memset(&context, 0, sizeof(nlp_context));
    if (clock_gettime(CLOCK_MONOTONIC , &context.ts) == -1) {
        LOGE("clock_gettime failed reason=[%s]\n", strerror(errno));
        return;
    }
    nlp_inject.accuracy = acc;
    nlp_inject.lattidude = lat;
    nlp_inject.longitude = lng;
    nlp_inject.timeReference[0] = (UINT32)context.ts.tv_sec;
    nlp_inject.timeReference[1] = (UINT32)context.ts.tv_nsec;
    nlp_inject.started = mnld_is_gps_started_done();
    nlp_inject.type = 0;
    if (mnld_is_gps_started_done()) {
#ifndef CONFIG_GPS_MT3333		
        mtk_gps_inject_nlp_location(&nlp_inject);
#else
		mt3333_controller_inject_location(lat, lng, acc);
#endif
    }
    ret = mnl2agps_location_sync(lat, lng, acc, alt_valid, altitude, source_valid, source_gnss, source_nlp, source_sensor);
    LOGD("ret = %d\n", ret);
    if (0 == ret) {
        LOGD("mnl2agps_location_sync success\n");
    }
}

static void hal_gps_delete_aiding_data(int flags) {
    LOGW("hal_gps_delete_aiding_data  flags=0x%x", flags);
    mnl2agps_delete_aiding_data(flags);
    mnld_gps_client* hal = &g_mnld_ctx.gps_status.clients.hal;
    hal->need_reset_ack = false;    // HAL no need the ACK
    g_mnld_ctx.gps_status.delete_aiding_flag |= flags;
    mnld_fsm(GPS_EVENT_RESET, 0, 0, NULL);
#ifdef CONFIG_GPS_MT3333
	extern int is_ygps_delete_data ;
	if(g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_IDLE){
		is_ygps_delete_data = 1;	
		
		 if ((flags&GPS_DELETE_RTI) == GPS_DELETE_RTI){
		 	g_mnld_ctx.gps_status.delete_aiding_flag = 0;
			is_ygps_delete_data = 0;
		 }
	}
#endif	
}

static void hal_gps_set_position_mode(gps_pos_mode mode, gps_pos_recurrence recurrence,
        int min_interval, int preferred_acc, int preferred_time) {
    /*LOGD("hal_gps_set_position_mode  mode=%d recurrence=%d min_interval=%d preferred_acc=%d preferred_time=%d",
        mode, recurrence, min_interval, preferred_acc, preferred_time);*/
    // TODO libmnl.so change the status
    UNUSED(mode);
    UNUSED(recurrence);
    UNUSED(min_interval);
    UNUSED(preferred_acc);
    UNUSED(preferred_time);
}

static void hal_data_conn_open(const char* apn) {
    LOGD("hal_data_conn_open  apn=[%s]", apn);
    mnl2agps_data_conn_open(apn);
}

static void hal_data_conn_open_with_apn_ip_type(const char* apn, apn_ip_type ip_type) {
    LOGD("hal_data_conn_open_with_apn_ip_type  apn=[%s] ip_type=%d", apn, ip_type);
    mnl2agps_data_conn_open_ip_type(apn, ip_type);
}

static void hal_data_conn_closed() {
    LOGD("hal_data_conn_closed");
    mnl2agps_data_conn_closed();
}

static void hal_data_conn_failed() {
    LOGD("hal_data_conn_failed");
    mnl2agps_data_conn_failed();
}

static void hal_set_server(agps_type type, const char* hostname, int port) {
    LOGD("hal_set_server  type=%d hostname=[%s] port=%d", type, hostname, port);
    mnl2agps_set_server(type, hostname, port);
}

static void hal_set_ref_location(cell_type type, int mcc, int mnc, int lac, int cid) {
    LOGD("hal_set_ref_location  type=%d mcc=%d mnc=%d lac=%d cid=%d", type, mcc, mnc, lac, cid);
    mnl2agps_set_ref_loc(type, mcc, mnc, lac, cid);
}

static void hal_set_id(agps_id_type type, const char* setid) {
    LOGD("hal_set_id  type=%d setid=[%s]", type, setid);
    mnl2agps_set_set_id(type, setid);
}

static void hal_ni_message(char* msg, int len) {
    LOGD("hal_ni_message, len=%d", len);
    mnl2agps_ni_message(msg, len);
}

static void hal_ni_respond(int notif_id, ni_user_response_type user_response) {
    LOGD("hal_ni_respond  notif_id=%d user_response=%d", notif_id, user_response);
    mnl2agps_ni_respond(notif_id, user_response);
}

static void hal_update_network_state(int connected, network_type type, int roaming,
        const char* extra_info) {
    LOGD("hal_update_network_state  connected=%d type=%d roaming=%d extra_info=[%s]",
        connected, type, roaming, extra_info);
    mnl2agps_update_network_state(connected, type, roaming, extra_info);
    mnl_epo_status* epo_status = &g_mnld_ctx.epo_status;

    epo_status->is_network_connected = connected;
    if (type == NETWORK_TYPE_WIFI) {
        epo_status->is_wifi_connected = connected;
    } else {
        epo_status->is_wifi_connected = false;
    }

    if (mnld_is_gps_started()) {
        epo_read_cust_config();
        if (connected && epo_status->is_epo_downloading == false &&
            epo_downloader_is_file_invalid() && epo_is_epo_download_enabled()) {
            epo_status->is_epo_downloading = true;
            epo_downloader_start();
        }
    } else if (type == NETWORK_TYPE_WIFI) {
        epo_read_cust_config();
        if (connected && epo_status->is_epo_downloading == false && epo_is_wifi_trigger_enabled() &&
            epo_downloader_is_file_invalid() && epo_is_epo_download_enabled()) {
            epo_status->is_epo_downloading = true;
            epo_downloader_start();
        }
    }
}

void hal_start_gps_trigger_epo_download() {
    mnl_epo_status* epo_status = &g_mnld_ctx.epo_status;
    LOGD("is_network_connected=%d,is_epo_downloading=%d",
      epo_status->is_network_connected, epo_status->is_epo_downloading);
    epo_read_cust_config();
    if (epo_status->is_epo_downloading == false
        && epo_downloader_is_file_invalid() && epo_is_epo_download_enabled()) {
        epo_status->is_epo_downloading = true;
        epo_downloader_start();
    }
}

bool is_network_connected() {
    mnl_epo_status* epo_status = &g_mnld_ctx.epo_status;
    return epo_status->is_network_connected;
}

bool is_wifi_network_connected() {
    mnl_epo_status* epo_status = &g_mnld_ctx.epo_status;
    return epo_status->is_wifi_connected;
}

static void hal_update_network_availability(int available, const char* apn) {
    LOGD("hal_update_network_availability  available=%d apn=[%s]", available, apn);
    mnl2agps_update_network_availability(available, apn);
}

static void hal_set_gps_measurement(bool enabled) {
    LOGD("hal_set_gps_measurement  enabled=%d", enabled);
    g_mnld_ctx.gps_status.is_gps_meas_enabled = enabled;
}

static void hal_set_gps_navigation(bool enabled) {
    LOGD("hal_set_gps_navigation  enabled=%d", enabled);
    g_mnld_ctx.gps_status.is_gps_navi_enabled = enabled;
}

static void hal_set_vzw_debug(bool enabled) {
    LOGD("hal_set_vzw_debug  enabled=%d", enabled);
    mnl2agps_vzw_debug_screen_enable(enabled);
}

extern MNL_CONFIG_T mnl_config;
static void hal_set_satellite_mode(int mode) {
    LOGD("hal_set_satellite_mode  mode=%d", mode);
	mnl_config.GNSSOPMode = mode;
    mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GNSS_SYSTEM);
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GNSS_SYSTEM);
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GNSS_SYSTEM);
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GNSS_SYSTEM);
}

static hal2mnl_interface g_hal2mnl_interface = {
    hal_reboot,
    hal_gps_init,
    hal_gps_start,
    hal_gps_stop,
    hal_gps_cleanup,
    hal_gps_inject_time,
    hal_gps_inject_location,
    hal_gps_delete_aiding_data,
    hal_gps_set_position_mode,
    hal_data_conn_open,
    hal_data_conn_open_with_apn_ip_type,
    hal_data_conn_closed,
    hal_data_conn_failed,
    hal_set_server,
    hal_set_ref_location,
    hal_set_id,
    hal_ni_message,
    hal_ni_respond,
    hal_update_network_state,
    hal_update_network_availability,
    hal_set_gps_measurement,
    hal_set_gps_navigation,
    hal_set_vzw_debug,
    hal_set_satellite_mode,
};

/*****************************************
AGPSD -> MNL
*****************************************/

static void agps_reboot() {
    LOGW("agps_reboot");
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    agps->gps_used = false;
    agps->need_open_ack = false;
    agps->need_close_ack = false;
    agps->need_reset_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void agps_open_gps_req(int show_gps_icon) {
    LOGW("agps_open_gps_req  show_gps_icon=%d", show_gps_icon);
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    agps->gps_used = true;
    agps->need_open_ack = true;
    agps->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

static void agps_close_gps_req() {
    LOGW("agps_close_gps_req");
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    agps->gps_used = false;
    agps->need_open_ack = false;
    agps->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void agps_reset_gps_req(int flags) {
    LOGW("agps_reset_gps_req  flags=0x%x", flags);
    if (flags == 0) {
        mnl2agps_reset_gps_done();
        return;
    }
    g_mnld_ctx.gps_status.delete_aiding_flag |= flags;
    mnld_gps_client* agps = &g_mnld_ctx.gps_status.clients.agps;
    agps->need_reset_ack = true;
    mnld_fsm(GPS_EVENT_RESET, 0, 0, NULL);
}

static void agps_session_done() {
    LOGW("agps_session_done");
    // TODO libmnl.so
    gps_controller_agps_session_done();
}

static void agps_ni_notify(int session_id, mnl_agps_notify_type type,
      const char* requestor_id, const char* client_name) {
    LOGD("agps_ni_notify  session_id=%d type=%d requestor_id=[%s] client_name=[%s]",
        session_id, type, requestor_id, client_name);
    int usc2_len;
    char ucs2_buff[1024];
    char requestorId[1024] = {0};
    char clientName[1024] = {0};

    memset(ucs2_buff, 0, sizeof(ucs2_buff));
    usc2_len = asc_str_to_usc2_str(ucs2_buff, requestor_id);
    raw_data_to_hex_string(requestorId, ucs2_buff, usc2_len);

    memset(ucs2_buff, 0, sizeof(ucs2_buff));
    usc2_len = asc_str_to_usc2_str(ucs2_buff, client_name);
    raw_data_to_hex_string(clientName, ucs2_buff, usc2_len);

    mnl2hal_request_ni_notify(session_id, type, requestorId, clientName,
        NI_ENCODING_TYPE_UCS2, NI_ENCODING_TYPE_UCS2);
}

static void agps_data_conn_req(int ipaddr, int is_emergency) {
    LOGD("agps_data_conn_req  ipaddr=0x%x is_emergency=%d", ipaddr, is_emergency);
    UNUSED(is_emergency);
    struct sockaddr_storage addr;
    memset(&addr, 0, sizeof(addr));
    struct sockaddr_in *in = (struct sockaddr_in*)&addr;
    addr.ss_family = AF_INET;
    in->sin_addr.s_addr = ipaddr;
    mnl2hal_request_data_conn(addr);
}

static void agps_data_conn_release() {
    LOGD("agps_data_conn_release");
    mnl2hal_release_data_conn();
}

static void agps_set_id_req(int flags) {
    LOGD("agps_set_id_req  flags=%d", flags);
    mnl2hal_request_set_id(flags);
}

static void agps_ref_loc_req(int flags) {
    LOGD("agps_ref_loc_req  flags=%d", flags);
    mnl2hal_request_ref_loc(flags);
}

static void agps_rcv_pmtk(const char* pmtk) {
    // LOGD("agps_rcv_pmtk  pmtk=%s", pmtk);
    // TODO libmnl.so
    gps_controller_rcv_pmtk(pmtk);
}

static void agps_gpevt(gpevt_type type) {
    LOGD("agps_gpevt  type=%d", type);
    UNUSED(type);
}
static void agps_rcv_lppe_common_iono(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_COMMON_IONO, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);

    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
          gnss_ha_assist_ack_struct ack;
          memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
          ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_COMMON_IONO;
          mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }
}
static void agps_rcv_lppe_common_trop(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_COMMON_TROP, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_COMMON_TROP;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_rcv_lppe_common_alt(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_COMMON_ALT, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_COMMON_ALT;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_rcv_lppe_common_solar(const char* data, int len)
{
     LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_COMMON_SOLAR, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_COMMON_SOLAR;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_rcv_lppe_common_ccp(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_COMMON_CCP, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_COMMON_CCP;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_rcv_lppe_generic_ccp(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_GENERIC_CCP, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_GENERIC_CCP;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_rcv_lppe_generic_dm(const char* data, int len)
{
    LOGD("rcv_lppe_data\n");
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_lppe_data: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param_with_payload_len(MTK_MSG_AGPS_MSG_SUPL_LPPE_ASSIST_GENERIC_DM, data, MTK_MOD_DISPATCHER, MTK_MOD_AGENT,len);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_LPPE\n");
    }else{
         gnss_ha_assist_ack_struct ack;
         memset((char*)(&ack),0x0,sizeof(gnss_ha_assist_ack_struct));
         ack.type=AGPS_MD_HUGE_DATA_TYPE_LPPE_AGNSS_PROVIDE_ASSIST_DATA_GENERIC_DM;
         mnl2agps_lppe_assist_data_provide_ack((char*)(&ack), sizeof(gnss_ha_assist_ack_struct));
    }

}
static void agps_location(mnl_agps_agps_location* location) {
    if (mtk_gps_log_is_hide() == 0) {
        LOGD("agps_location  lat,lng %f,%f acc=%f used=%d",
            location->latitude, location->longitude, location->accuracy, location->accuracy_used);
    }

    gps_location loc;
    memset(&loc, 0, sizeof(loc));
    loc.flags |= MTK_GPS_LOCATION_HAS_LAT_LONG;
    loc.lat = location->latitude;
    loc.lng = location->longitude;
    if (location->altitude_used) {
        loc.flags |= MTK_GPS_LOCATION_HAS_ALT;
        loc.alt = location->altitude;
    }
    if (location->speed_used) {
        loc.flags |= MTK_GPS_LOCATION_HAS_SPEED;
        loc.speed = location->speed;
    }
    if (location->bearing_used) {
        loc.flags |= MTK_GPS_LOCATION_HAS_BEARING;
        loc.bearing = location->bearing;
    }
    if (location->accuracy_used) {
        loc.flags |= MTK_GPS_LOCATION_HAS_ACCURACY;
        loc.accuracy = location->accuracy;
    }
    if (location->timestamp_used) {
        loc.timestamp = location->timestamp;
    }
    mnl2hal_location(loc);
    MTK_GPS_NLP_T c2k_cell_location;
    nlp_context context;
    memset(&c2k_cell_location, 0, sizeof(MTK_GPS_NLP_T));
    memset(&context, 0, sizeof(nlp_context));
    if (clock_gettime(CLOCK_MONOTONIC , &context.ts) == -1) {
        LOGE("clock_gettime failed reason=[%s]\n", strerror(errno));
        return;
    }
    LOGD("ts.tv_sec = %ld, ts.tv_nsec = %ld\n",
        context.ts.tv_sec, context.ts.tv_nsec);

    c2k_cell_location.lattidude = location->latitude;
    c2k_cell_location.longitude = location->longitude;
    c2k_cell_location.accuracy = location->accuracy;
    c2k_cell_location.timeReference[0] = (UINT32)context.ts.tv_sec;
    c2k_cell_location.timeReference[1] = (UINT32)context.ts.tv_nsec;
    c2k_cell_location.type = 0;
    c2k_cell_location.started = 1;
    if (mtk_gps_log_is_hide() == 0) {
        LOGD("inject cell location lati= %f, longi = %f, accuracy = %f\n",
            c2k_cell_location.lattidude, c2k_cell_location.longitude, c2k_cell_location.accuracy);
    }
    if (mnld_is_gps_started_done()) {
        mtk_gps_inject_nlp_location(&c2k_cell_location);
    }
}

static void agps_ni_notify2(int session_id,
    mnl_agps_notify_type type, const char* requestor_id, const char* client_name,
    mnl_agps_ni_encoding_type requestor_id_encoding,
    mnl_agps_ni_encoding_type client_name_encoding) {
    LOGD("agps_ni_notify2  session_id=%d type=%d requestor_id_encoding=%d client_name_encoding=%d",
        session_id, type, requestor_id_encoding, client_name_encoding);
    mnl2hal_request_ni_notify(session_id, type, requestor_id, client_name, requestor_id_encoding, client_name_encoding);
}

static void agps_data_conn_req2(struct sockaddr_storage* addr, int is_emergency) {
    LOGD("agps_data_conn_req2  is_emergency=%d", is_emergency);
    UNUSED(is_emergency);
    mnl2hal_request_data_conn(*addr);
}

static int get_agnss_capability(const UINT8 sv_type_agps_set, const UINT8 sv_type, char *pmtk_str) {
    char tmp[64]={0};
    int agps_cap = 0;
    int aglonass_cap = 0;
    int abeidou_cap = 0;
    int agalileo_cap = 0;
    int gnss_num = 0;

    // Axxx Enable. with HW support condition
    if (((sv_type_agps_set & 0x21) == 0x21) && ((sv_type & 0x01) == 0x01)) {
        agps_cap = 1;
    }
    if (((sv_type_agps_set & 0x12) == 0x12) && ((sv_type & 0x02) == 0x02)) {
        aglonass_cap = 1;
    }
    if (((sv_type_agps_set & 0x44) == 0x44) && ((sv_type & 0x04) == 0x04)) {
        abeidou_cap = 1;
    }
    if (((sv_type_agps_set & 0x88) == 0x88) && ((sv_type & 0x08) == 0x08)) {
        agalileo_cap = 1;
    }
    gnss_num = agps_cap + aglonass_cap + abeidou_cap + agalileo_cap;

    sprintf(pmtk_str, "$PMTK764,0,0,0");
    sprintf(tmp, ",%d", gnss_num);
    strncat(pmtk_str, tmp, PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));

    if (agps_cap) {
        strncat(pmtk_str, ",0,128", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
    }
    if (agalileo_cap) {
        strncat(pmtk_str, ",3,128", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
    }
    if (aglonass_cap) {
        strncat(pmtk_str, ",4,128", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
    }
    if (abeidou_cap) {
        strncat(pmtk_str, ",5,128", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
    }
    if (lppe_enable){
        strncat(pmtk_str, ",1", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
        }else{
          strncat(pmtk_str, ",0", PMTK_MAX_PKT_LENGTH - strlen(pmtk_str));
      }
    add_chksum(pmtk_str);
    LOGD("MNLD_PMTK764: %s, agps_cap:%d, aglonass_cap:%d, abeidou_cap:%d, agalileo_cap:%d, lppe_support:%d\n",
        pmtk_str, agps_cap, aglonass_cap, abeidou_cap, agalileo_cap, lppe_enable);
    return 0;
}

void agps_settings_sync(mnl_agps_agps_settings* s) {
    UINT8 sv_type;
    int ret;

    char pmtk_str[PMTK_MAX_PKT_LENGTH];
    memset(pmtk_str, 0, sizeof(pmtk_str));

    LOGD("agps setting, sib8_16_enable = %d, gps_sat_en = %d, glonass_sat_en = %d, \
        beidou_sat_en = %d, galileo_sat_en = %d, a_glonass_sat_en = %d, \
        a_gps_satellite_enable = %d, a_beidou_satellite_enable = %d, a_galileo_satellite_enable = %d, lppe_enable=%d\n",
        s->sib8_16_enable,
        s->gps_satellite_enable, s->glonass_satellite_enable,
        s->beidou_satellite_enable, s->galileo_satellite_enable,
        s->a_glonass_satellite_enable, s->a_gps_satellite_enable,
        s->a_beidou_satellite_enable, s->a_galileo_satellite_enable,
        s->lppe_enable);
    g_settings_from_agps = *s;

    sv_type_agps_set = 0;
    sv_type_agps_set |= (g_settings_from_agps.a_galileo_satellite_enable & 0x01) << 7;
    sv_type_agps_set |= (g_settings_from_agps.a_beidou_satellite_enable & 0x01) << 6;
    sv_type_agps_set |= (g_settings_from_agps.a_gps_satellite_enable & 0x01) << 5;
    sv_type_agps_set |= (g_settings_from_agps.a_glonass_satellite_enable & 0x01) << 4;
    sv_type_agps_set |= (g_settings_from_agps.galileo_satellite_enable & 0x01) << 3;
    sv_type_agps_set |= (g_settings_from_agps.beidou_satellite_enable & 0x01) << 2;
    sv_type_agps_set |= (g_settings_from_agps.glonass_satellite_enable & 0x01) << 1;
    sv_type_agps_set |= (g_settings_from_agps.gps_satellite_enable & 0x01);
    sib8_16_enable = g_settings_from_agps.sib8_16_enable;
    lppe_enable = g_settings_from_agps.lppe_enable && mtk_gps_mnl_info.support_lppe;
    get_chip_sv_support_capability(&sv_type);
    //LOGD("get_chip_sv_support_capability, sv_type = %d", sv_type);
    g_settings_to_agps.gps_satellite_support = (sv_type) & (0x01);
    g_settings_to_agps.glonass_satellite_support = ((sv_type) & (0x02)) >> 1;
    g_settings_to_agps.beidou_satellite_support = ((sv_type) & (0x04)) >> 2;
    g_settings_to_agps.galileo_satellite_support = ((sv_type) & (0x08)) >> 3;

    ret = mnl2agps_agps_settings_ack(&g_settings_to_agps);
    //LOGD("mnl2agps_agps_settings_ack done, ret = %d", ret);

    if (mnld_is_gps_started_done()) {
        ret = mtk_gps_set_param(MTK_PARAM_CMD_SWITCH_CONSTELLATION, &sv_type_agps_set);
        LOGD("sent CMD_SWITCH_CONSTELLATION to mnl, sv_type_agps_set = 0x%x ,ret = %d", sv_type_agps_set, ret);

        ret = mtk_gps_set_param(MTK_PARAM_CMD_SIB8_16_ENABLE, &sib8_16_enable);
        LOGD("sent CMD_SIB8_16_ENABLE to mnl, sib8_16_enable = %d ,ret = %d", sib8_16_enable, ret);
        if(mtk_gps_mnl_info.support_lppe){
           ret = mtk_gps_set_param(MTK_PARAM_CMD_LPPE_ENABLE, &lppe_enable);
            LOGD("sent MTK_PARAM_CMD_LPPE_ENABLE to mnl, lppe_enable = %d ,ret = %d", lppe_enable, ret);
        }
    } else {
        LOGD("mnl stop, mnld send pmtk764 to agpsd\n");

        // Generate PMTK764 and send to AGPSD
        get_agnss_capability(sv_type_agps_set, sv_type, pmtk_str);
        mnl2agps_pmtk(pmtk_str);
    }
}
static void vzw_debug_screen_output(const char* str) {
    LOGD("vzw_debug_screen_output  str=%s", str);
    mnl2hal_vzw_debug_screen_output(str);
}

static agps2mnl_interface g_agps2mnl_interface = {
    agps_reboot,
    agps_open_gps_req,
    agps_close_gps_req,
    agps_reset_gps_req,
    agps_session_done,
    agps_ni_notify,
    agps_data_conn_req,
    agps_data_conn_release,
    agps_set_id_req,
    agps_ref_loc_req,
    agps_rcv_pmtk,
    agps_gpevt,
    agps_location,
    agps_ni_notify2,
    agps_data_conn_req2,
    agps_settings_sync,
    vzw_debug_screen_output,
    agps_rcv_lppe_common_iono,
    agps_rcv_lppe_common_trop,
    agps_rcv_lppe_common_alt,
    agps_rcv_lppe_common_solar,
    agps_rcv_lppe_common_ccp,
    agps_rcv_lppe_generic_ccp,
    agps_rcv_lppe_generic_dm
};

static void flp2mnl_flp_reboot() {
    LOGW("flp2mnl_flp_reboot");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = false;
    flp->need_open_ack = false;
    flp->need_close_ack = false;
    flp->need_reset_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void flp2mnl_gps_start() {
    LOGW("flp2mnl_gps_start\n");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = true;
    flp->need_open_ack = true;  // need to confirm flp deamon
    flp->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

static void flp2mnl_gps_stop() {
    LOGW("flp2mnl_gps_stop");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = false;
    flp->need_open_ack = false;
    flp->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static int flp2mnl_flp_lpbk(char *buff, int len) {
    int ret = 0;
    if (-1 == mnl2flp_data_send((UINT8 *)buff, len)) {
        LOGE("[FLP2MNLD]Send to HAL failed, %s\n", strerror(errno));
        ret = -1;
    }
    else {
        LOGD("[FLP2MNLD]Send to HAL successfully\n");
    }
    return ret;
}

static int flp2mnl_flp_data(MTK_FLP_OFFLOAD_MSG_T *prMsg) {
    LOGD("[FLP2MNLD]to connsys: len=%d\n", prMsg->length);
    if (prMsg->length > 0 && prMsg->length <= OFFLOAD_PAYLOAD_LEN) {
        mtk_gps_ofl_send_flp_data((UINT8 *)&prMsg->data[0], prMsg->length);
    } else {
        LOGD("[FLP2MNLD]to connsys: length is invalid\n");
    }
    return 0;
}

static int mnld_flp_attach() {
    LOGD("mnld_flp_attach");
    if (-1 == mnld_flp_attach_done()) {
        LOGE("Send attach done to FLP failed\n");
    } else {
        LOGD("Send attach done to FLP succeed\n");
    }
    return 0;
}

static void mnld_flp_hbd_gps_open() {
    LOGD("mnld_flp_hbd_gps_open\n");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = true;
    flp->need_open_ack = true;  // need to confirm flp deamon
    flp->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

static void mnld_flp_hbd_gps_close() {
    LOGD("mnld_flp_hbd_gps_close");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = false;
    flp->need_open_ack = false;
    flp->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void mnld_flp_ofl_link_open() {
    LOGD("mnld_flp_ofl_link_open\n");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = true;
    flp->need_open_ack = true;  // need to confirm flp deamon
    flp->need_close_ack = false;
    mnld_flp_session.type = MNLD_FLP_CAPABILITY_OFL_MODE;
    mnld_fsm(GPS_EVENT_OFFLOAD_START, 0, 0, NULL);
}

static void mnld_flp_ofl_link_close() {
    LOGD("mnld_flp_ofl_link_close");
    mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
    flp->gps_used = false;
    flp->need_open_ack = false;
    flp->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static int mnld_flp_ofl_link_send(MTK_FLP_MNL_MSG_T *prMsg) {
    LOGD("mnld_flp_ofl_link_send to connsys: len=%d,session_id=%u\n", prMsg->length, prMsg->session);
    if (prMsg->session == mnld_flp_session.id) {
        if (prMsg->length > 0 && prMsg->length <= OFFLOAD_PAYLOAD_LEN) {
            mtk_gps_ofl_send_flp_data((UINT8 *)&prMsg->data[0], prMsg->length);
        } else {
            LOGW("mnld_flp_ofl_link_send to connsys: length is invalid\n");
        }
    } else {
        LOGW("session id doesn't match\n");
    }
    return 0;
}

static flp2mnl_interface g_flp2mnl_interface = {
    mnld_flp_attach,
    mnld_flp_hbd_gps_open,
    mnld_flp_hbd_gps_close,
    mnld_flp_ofl_link_open,
    mnld_flp_ofl_link_close,
    mnld_flp_ofl_link_send
};

static void gfc2mnl_gfc_reboot() {
    LOGW("gfc2mnl_gfc_reboot");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = false;
    geofence->need_open_ack = false;
    geofence->need_close_ack = false;
    geofence->need_reset_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void gfc2mnl_gps_start() {
    LOGW("gfc2mnl_gps_start\n");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = true;
    geofence->need_open_ack = true;  // need to confirm flp deamon
    geofence->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

static void gfc2mnl_gps_stop() {
    LOGW("gfc2mnl_gps_stop");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = false;
    geofence->need_open_ack = false;
    geofence->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static int mnld_gfc_attach() {
    LOGD("mnld_gfc_attach");
    if (-1 == mnld_gfc_attach_done()) {
        LOGE("Send attach done to GFC failed\n");
    } else {
        LOGD("Send attach done to GFC succeed\n");
    }
    return 0;
}

static void mnld_gfc_hbd_gps_open() {
    LOGD("mnld_gfc_hbd_gps_open\n");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = true;
    geofence->need_open_ack = true;
    geofence->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

static void mnld_gfc_hbd_gps_close() {
    LOGD("mnld_gfc_hbd_gps_close");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = false;
    geofence->need_open_ack = false;
    geofence->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static void mnld_gfc_ofl_link_open() {
    LOGD("mnld_gfc_ofl_link_open\n");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = true;
    geofence->need_open_ack = true;  // need to confirm flp deamon
    geofence->need_close_ack = false;
    mnld_gfc_session.type = MNLD_GFC_CAPABILITY_OFL_MODE;
    mnld_fsm(GPS_EVENT_OFFLOAD_START, 0, 0, NULL);
}

static void mnld_gfc_ofl_link_close() {
    LOGD("mnld_gfc_ofl_link_close");
    mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
    geofence->gps_used = false;
    geofence->need_open_ack = false;
    geofence->need_close_ack = true;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

static int mnld_gfc_ofl_link_send(MTK_GFC_MNL_MSG_T *prMsg) {
    LOGD("mnld_gfc_ofl_link_send to connsys: len=%d,session_id=%u\n", prMsg->length, prMsg->session);
    if (prMsg->session == mnld_gfc_session.id) {
        if (prMsg->length > 0 && prMsg->length <= OFFLOAD_PAYLOAD_LEN) {
            mtk_gps_ofl_send_flp_data((UINT8 *)&prMsg->data[0], prMsg->length);
        } else {
            LOGW("mnld_gfc_ofl_link_send to connsys: length is invalid\n");
        }
    } else {
        LOGW("session id doesn't match\n");
    }
    return 0;
}

static gfc2mnl_interface g_gfc2mnl_interface = {
    mnld_gfc_attach,
    mnld_gfc_hbd_gps_open,
    mnld_gfc_hbd_gps_close,
    mnld_gfc_ofl_link_open,
    mnld_gfc_ofl_link_close,
    mnld_gfc_ofl_link_send
};


void flp_test2mnl_gps_start() {
    LOGD("flp_test2mnl_gps_start\n");
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    flp_test->gps_used = true;
    flp_test->need_open_ack = false;  // need to confirm flp deamon
    flp_test->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

void flp_test2mnl_gps_stop() {
    LOGD("flp_test2mnl_gps_stop");
    mnld_gps_client* flp_test = &g_mnld_ctx.gps_status.clients.flp_test;
    flp_test->gps_used = false;
    flp_test->need_open_ack = false;
    flp_test->need_close_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

extern int ofl_lpbk_tst_start;
extern int ofl_lpbk_tst_size;
static int flp_test2mnl_flp_lpbk_start() {
    char tmp_buf[1024];
    int i;

    LOGD("[OFL]lpbk start!");
    for (i=0; i < ofl_lpbk_tst_size; i++) {
        tmp_buf[i] = 0x80;
    }
    mtk_gps_ofl_send_flp_data((UINT8 *)&tmp_buf[0], ofl_lpbk_tst_size);
    ofl_lpbk_tst_start = 1;
    return 0;
}

static int flp_test2mnl_flp_lpbk_stop() {
    LOGD("[OFL]lpbk stop!");
    ofl_lpbk_tst_start = 0;

    return 0;
}

#if 0
static flp_test2mnl_interface g_flp_test2mnl_interface = {
    flp_test2mnl_gps_start,
    flp_test2mnl_gps_stop,
    flp_test2mnl_flp_lpbk_start,
    flp_test2mnl_flp_lpbk_stop,
};
#endif

void factory_mnld_gps_start() {
    LOGD("factory_mnld_gps_start\n");
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    factory->gps_used = true;
    factory->need_open_ack = false;  // need to confirm flp deamon
    factory->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
}

void factory_mnld_gps_stop() {
    LOGD("factory_mnld_gps_stop");
    mnld_gps_client* factory = &g_mnld_ctx.gps_status.clients.factory;
    factory->gps_used = false;
    factory->need_open_ack = false;
    factory->need_close_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
}

// for AT cmd
int at_test2mnl_gps_start(void) {
    LOGD("at_test2mnl_gps_start\n");
    mnld_gps_client* at_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    at_test->gps_used = true;
    at_test->need_open_ack = false;  // need to confirm flp deamon
    at_test->need_close_ack = false;
    mnld_fsm(GPS_EVENT_START, 0, 0, NULL);
    return 0;
}

int at_test2mnl_gps_stop(void) {
    LOGD("at_test2mnl_gps_stop");
    mnld_gps_client* at_test = &g_mnld_ctx.gps_status.clients.at_cmd_test;
    at_test->gps_used = false;
    at_test->need_open_ack = false;
    at_test->need_close_ack = false;
    mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
    return 0;
}

/*****************************************
ALL -> MAIN
*****************************************/
int mnld_gps_start_done(bool is_assist_req) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, GPS2MAIN_EVENT_START_DONE);
    put_int(buff, &offset, is_assist_req);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_gps_start_nmea_timeout() {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, GPS2MAIN_EVENT_NMEA_TIMEOUT);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_gps_stop_done() {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, GPS2MAIN_EVENT_STOP_DONE);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_gps_reset_done() {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, GPS2MAIN_EVENT_RESET_DONE);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_gps_update_location(gps_location location) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, GPS2MAIN_EVENT_UPDATE_LOCATION);
    put_binary(buff, &offset, (const char*)&location, sizeof(location));
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_epo_download_done(epo_download_result result) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, EPO2MAIN_EVENT_EPO_DONE);
    put_int(buff, &offset, result);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_qepo_download_done(epo_download_result result) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, QEPO2MAIN_EVENT_QEPO_DONE);
    put_int(buff, &offset, result);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_qepo_bd_download_done(epo_download_result result) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, QEPO2MAIN_EVENT_QEPO_BD_DONE);
    put_int(buff, &offset, result);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

int mnld_mtknav_download_done(epo_download_result result) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, MTKNAV2MAIN_EVENT_MTKNAV_DONE);
    put_int(buff, &offset, result);
    return safe_sendto(MNLD_MAIN_SOCKET, buff, offset);
}

static int main_event_hdlr(int fd) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    main_internal_event cmd;
    int read_len;

    read_len = safe_recvfrom(fd, buff, sizeof(buff));
    if (read_len <= 0) {
        LOGE("main_event_hdlr() safe_recvfrom() failed read_len=%d", read_len);
        return -1;
    }

    cmd = get_int(buff, &offset);
    switch (cmd) {
    case GPS2MAIN_EVENT_START_DONE: {
        int is_assist_req = get_int(buff, &offset);
        LOGW("GPS2MAIN_EVENT_START_DONE  is_assist_req=%d", is_assist_req);
        mnld_fsm(GPS_EVENT_START_DONE, is_assist_req, 0, NULL);
        break;
    }
    case GPS2MAIN_EVENT_STOP_DONE: {
        LOGW("GPS2MAIN_EVENT_STOP_DONE");
        mnld_fsm(GPS_EVENT_STOP_DONE, 0, 0, NULL);
        break;
    }
    case GPS2MAIN_EVENT_RESET_DONE: {
        LOGW("GPS2MAIN_EVENT_RESET_DONE");
        stop_timer(g_mnld_ctx.gps_status.timer_reset);
        do_gps_reset_hdlr();
        break;
    }
    case GPS2MAIN_EVENT_NMEA_TIMEOUT: {
        LOGD("GPS2MAIN_EVENT_NMEA_TIMEOUT");
        if (mnld_is_gps_started_done()) {
            mtk_gps_clear_gps_user();
            LOGD("set nmea timeout event to main thread\n");
            mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
            // send the reboot message to the related modules
            mnl2hal_mnld_reboot();
            mnl2agps_mnl_reboot();
            mnl2flp_mnld_reboot();
        }
        break;
        }
    case GPS2MAIN_EVENT_UPDATE_LOCATION: {
        gps_location location;
        get_binary(buff, &offset, (char*)&location);
        bool alt_valid = (location.flags & MTK_GPS_LOCATION_HAS_ALT)? true : false;
        bool source_valid = true;
        bool source_gnss = true;
        bool source_nlp = false;
        bool source_sensor = false;
        float valid_acc = (location.flags & MTK_GPS_LOCATION_HAS_ACCURACY)? location.accuracy : 2000;
        if (mtk_gps_log_is_hide() == 0) {
            LOGW("GPS2MAIN_EVENT_UPDATE_LOCATION  lat=%f lng=%f acc=%f",
                location.lat, location.lng, valid_acc);
        }
        LOGD("wait_first_location=%d\n", g_mnld_ctx.gps_status.wait_first_location);
        mnl2agps_location_sync(location.lat, location.lng, (int)valid_acc, alt_valid, (float)location.alt, source_valid, source_gnss, source_nlp, source_sensor);
        if (g_mnld_ctx.gps_status.wait_first_location) {
            g_mnld_ctx.gps_status.wait_first_location = false;
            g_mnld_ctx.gps_status.gps_ttff = get_tick() - g_mnld_ctx.gps_status.gps_start_time;
            #if ANDROID_MNLD_PROP_SUPPORT
            if (get_gps_cmcc_log_enabled()) {
                op01_log_gps_location(location.lat, location.lng, g_mnld_ctx.gps_status.gps_ttff);
            }
            #else
            op01_log_gps_location(location.lat, location.lng, g_mnld_ctx.gps_status.gps_ttff);
            #endif
        }
        LOGD("location_sync done\n");
        break;
    }
    case EPO2MAIN_EVENT_EPO_DONE: {
        epo_download_result result = get_int(buff, &offset);
        LOGD("EPO2MAIN_EVENT_EPO_DONE  result=%d", result);
        g_mnld_ctx.epo_status.is_epo_downloading = false;
        // TODO libmnl.so to inject the EPO
        bool started = mnld_is_gps_started_done();
        if (result == EPO_DOWNLOAD_RESULT_SUCCESS) {
            if (started) {
                epo_update_epo_file();
            }
        } else {
            unlink(EPO_UPDATE_HAL);
        }
        break;
    }
    case QEPO2MAIN_EVENT_QEPO_DONE: {
        epo_download_result result = get_int(buff, &offset);
        bool started = mnld_is_gps_started_done();
        LOGD("QEPO2MAIN_EVENT_QEPO_DONE  result=%d started=%d\n", result, started);
        if (started) {
            qepo_update_quarter_epo_file(result);
        } else {
            LOGW("qepo download finsh before GPS start done");
            qepo_update_flag = true;
        }
        break;
    }
    case QEPO2MAIN_EVENT_QEPO_BD_DONE: {
        epo_download_result result = get_int(buff, &offset);
        bool started = mnld_is_gps_started_done();
        LOGD("QEPO2MAIN_EVENT_QEPO_BD_DONE  result=%d started=%d\n", result, started);
        if (started) {
            qepo_update_quarter_epo_bd_file(result);
        } else {
            LOGW("qepo BD download finsh before GPS start done");
            qepo_BD_update_flag = true;
        }
        break;
    }
    case MTKNAV2MAIN_EVENT_MTKNAV_DONE: {
        epo_download_result result = get_int(buff, &offset);
        bool started = mnld_is_gps_started_done();
        LOGD("MTKNAV2MAIN_EVENT_MTKNAV_DONE  result=%d started=%d\n", result, started);
        if (started) {
            mtknav_update_mtknav_file(result);
        } else {
            LOGW("mtknav download finsh before GPS start done");
            mtknav_update_flag = true;
        }
        break;
    }
    }
    return 0;
}

/*****************************************
Threads
*****************************************/
static void mnld_main_thread_timeout() {
    if (mnld_timeout_ne_enabled() == false) {
        LOGE("mnld_main_thread_timeout() dump and exit.");
        mnld_block_exit();
    } else {
        LOGE("mnld_main_thread_timeout() crash here for debugging");
        CRASH_TO_DEBUG();
    }
}

static void mnld_gps_start_timeout() {
    if (mnld_timeout_ne_enabled() == false) {
        LOGE("mnld_gps_start_timeout() dump and exit.");
        //mnld_block_exit();
    } else {
        LOGE("mnld_gps_start_timeout() crash here for debugging");
        CRASH_TO_DEBUG();
    }
}

static void mnld_gps_stop_timeout() {
    if (mnld_timeout_ne_enabled() == false) {
        LOGE("mnld_gps_stop_timeout() dump and exit.");
        mnld_block_exit();
    } else {
        LOGE("mnld_gps_stop_timeout() crash here for debugging");
        CRASH_TO_DEBUG();
    }
}

static void mnld_gps_reset_timeout() {
    if (mnld_timeout_ne_enabled() == false) {
        LOGE("mnld_gps_reset_timeout() dump and exit.");
        mnld_block_exit();
    } else {
        LOGE("mnld_gps_reset_timeout() crash here for debugging");
        CRASH_TO_DEBUG();
    }
}

static void mnld_gps_switch_ofl_mode_timeout() {
    if (is_all_hbd_gps_client_exit()) {
        LOGD("switch to offload mode timer timeout, flp user prepare to stop gps");
        mnld_gps_client* flp = &g_mnld_ctx.gps_status.clients.flp;
        mnld_gps_client* geofence = &g_mnld_ctx.gps_status.clients.geofence;
        //flp
        flp->gps_used = false;
        flp->need_open_ack = false;
        flp->need_close_ack = false;
        //geofence
        geofence->gps_used = false;
        geofence->need_open_ack = false;
        geofence->need_close_ack = false;
        mnld_fsm(GPS_EVENT_STOP, 0, 0, NULL);
    } else {
        LOGD("switch to offload mode timer timeout. But some HBD user exist, so not stop gps");
    }
}

void gps_mnld_restart_mnl_process(void) {
    LOGD("gps_mnld_restart_mnl_process\n");
    mnld_gps_start_nmea_timeout();
}

void mnld_gps_request_nlp(int src) {
    LOGD("mnld_gps_request_nlp src: %d", src);
    Mnld2NlpUtilsInterface_reqNlpLocation(&g_mnld_ctx.fds.fd_nlp_utils, src);
}

static void* mnld_main_thread(void *arg) {
    #define MAX_EPOLL_EVENT 50
    timer_t hdlr_timer = init_timer(mnld_main_thread_timeout);
    struct epoll_event events[MAX_EPOLL_EVENT];
    UNUSED(arg);

    int epfd = epoll_create(MAX_EPOLL_EVENT);
    if (epfd == -1) {
        LOGE("mnld_main_thread() epoll_create failure reason=[%s]%d",
            strerror(errno), errno);
        return 0;
    }
#ifdef MTK_AGPS_SUPPORT
#ifndef CONFIG_GPS_MT3333
    int fd_agps = g_mnld_ctx.fds.fd_agps;
#endif
#endif
    int fd_hal = g_mnld_ctx.fds.fd_hal;
    int fd_flp = g_mnld_ctx.fds.fd_flp;
    int fd_flp_test = g_mnld_ctx.fds.fd_flp_test;
    int fd_geofence = g_mnld_ctx.fds.fd_geofence;
    int fd_at_cmd = g_mnld_ctx.fds.fd_at_cmd;
    int fd_int = g_mnld_ctx.fds.fd_int;
    int fd_mtklogger = g_mnld_ctx.fds.fd_mtklogger;
    int fd_mtklogger_client = g_mnld_ctx.fds.fd_mtklogger_client = -1;
#ifdef MTK_AGPS_SUPPORT
#ifndef CONFIG_GPS_MT3333
    if (epoll_add_fd(epfd, fd_agps) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_agps failed");
        return 0;
    }
#endif	
#endif
    if (epoll_add_fd(epfd, fd_hal) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_hal failed");
        return 0;
    }
    if (epoll_add_fd(epfd, fd_flp) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_flp failed");
        return 0;
    }
    if (epoll_add_fd(epfd, fd_geofence) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_geofence failed");
        return 0;
    }
    //if (epoll_add_fd(epfd, fd_flp_test) == -1) {
    //    LOGE("mnld_main_thread() epoll_add_fd() failed for fd_flp_test failed");
    //    return 0;
    //}
    if (epoll_add_fd(epfd, fd_at_cmd) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_at_cmd failed");
        return 0;
    }
    if (epoll_add_fd(epfd, fd_int) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_int failed");
        return 0;
    }
    if (epoll_add_fd(epfd, fd_mtklogger) == -1) {
        LOGE("mnld_main_thread() epoll_add_fd() failed for fd_mtklogger failed");
        return 0;
    }

    mtk_socket_client_init_local(&g_mnld_ctx.fds.fd_nlp_utils,
            MNLD_TO_NLP_UTILS_SOCKET, SOCK_NS_ABSTRACT);

    while (1) {
        int i;
        int n = 0;
        #ifdef CONFIG_GPS_ENG_LOAD
        LOGD("mnld_main_thread() enter wait\n");
        #endif
        n = epoll_wait(epfd, events, MAX_EPOLL_EVENT , -1);
        if (n > 1) {
            LOGD("n=%d\n", n);
        } else if (n == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                LOGE("mnld_main_thread() epoll_wait failure reason=[%s]%d",
                    strerror(errno), errno);
                return 0;
            }
        }
        start_timer(hdlr_timer, MNLD_MAIN_HANDLER_TIMEOUT);
        for (i = 0; i < n; i++) {
        #ifdef MTK_AGPS_SUPPORT
		#ifndef CONFIG_GPS_MT3333
            if (events[i].data.fd == fd_agps) {
                if (events[i].events & EPOLLIN) {
                    //LOGD("agps2mnl_hdlr msg");
                    agps2mnl_hdlr(fd_agps, &g_agps2mnl_interface);
                }
            } else
		#endif
        #endif
            if (events[i].data.fd == fd_hal) {
                if (events[i].events & EPOLLIN) {
                    LOGD("hal2mnl_hdlr msg");
                    hal2mnl_hdlr(fd_hal, &g_hal2mnl_interface);
                }
            } else if (events[i].data.fd == fd_flp) {
                if (events[i].events & EPOLLIN) {
                    LOGD("flp2mnl_hdlr msg");
                    mtk_hal2flp_main_hdlr(fd_flp, &g_flp2mnl_interface);
                }
            }  else if (events[i].data.fd == fd_geofence) {
                if (events[i].events & EPOLLIN) {
                    LOGD("gfc2mnl_hdlr msg");
                    mtk_hal2gfc_main_hdlr(fd_geofence, &g_gfc2mnl_interface);
                }
            } else if (events[i].data.fd == fd_flp_test) {
                    if (events[i].events & EPOLLIN) {
                    LOGD("fd_flp_test msg");
                    //flp_test2mnl_hdlr(fd_flp_test, &g_flp_test2mnl_interface);
                }
            } else if (events[i].data.fd == fd_at_cmd) {
                if (events[i].events & EPOLLIN) {
                    LOGD("at_cmd2mnl_hdlr msg");
                    at_cmd2mnl_hdlr(fd_at_cmd);
                }
            } else if (events[i].data.fd == fd_int) {
                if (events[i].events & EPOLLIN) {
                    LOGD("main_event_hdlr msg");
                    main_event_hdlr(fd_int);
                }
            } else if (events[i].data.fd == fd_mtklogger) {
                if (events[i].events & EPOLLIN) {
                        struct sockaddr addr;
                        socklen_t alen = sizeof(addr);
                        if (fd_mtklogger_client >= 0) {  // mtklogger exeception exit,  then reconnect
                            LOGD("mtklogger old client_fd = %d, epoll_del it\n", fd_mtklogger_client);
                            epoll_del_fd(epfd, fd_mtklogger_client);
                            close(fd_mtklogger_client);
                            g_mnld_ctx.fds.fd_mtklogger_client = -1;
                        }
                        fd_mtklogger_client = accept(fd_mtklogger, &addr, &alen);
                        if (fd_mtklogger_client < 0) {
                            LOGE("accept failed, %s", strerror(errno));
                            return 0;
                        }
                        g_mnld_ctx.fds.fd_mtklogger_client = fd_mtklogger_client;
                        LOGD("mtklogger new client fd: %d", fd_mtklogger_client);
                        if (epoll_add_fd(epfd, fd_mtklogger_client) == -1) {
                            LOGE("mnld_main_thread() epoll_add_fd() failed for fd_mtklogger_client failed");
                            return 0;
                    }
                }
            } else if (events[i].data.fd == fd_mtklogger_client) {
                if ((events[i].events & (EPOLLERR|EPOLLHUP)) != 0) {
                    LOGE("wait fd_mtklogger_client EPOLLERR|EPOLLHUP");
                    epoll_del_fd(epfd, fd_mtklogger_client);
                    close(fd_mtklogger_client);
                    fd_mtklogger_client = g_mnld_ctx.fds.fd_mtklogger_client = -1;
                    break;
                } else if ((events[i].events & EPOLLIN) != 0) {
                    LOGD("mtklogger2mnl_hdlr msg");
                    start_timer(hdlr_timer, MNLD_GPS_LOG_HANDLER_TIMEOUT);  // mkdir in external sdcard  so slowly
                    mtklogger2mnl_hdlr(fd_mtklogger_client);
                }
            } else {
                LOGE("mnld_main_thread() unknown fd=%d",
                    events[i].data.fd);
            }
        }
        stop_timer(hdlr_timer);
    }
    LOGE("mnld_main_thread() exit");
    return 0;
}

static int mnld_init() {
    pthread_t pthread_main;

    // init fds
#ifdef MTK_AGPS_SUPPORT
#ifndef CONFIG_GPS_MT3333
    g_mnld_ctx.fds.fd_agps = create_agps2mnl_fd();
    if (g_mnld_ctx.fds.fd_agps < 0) {
        LOGE("create_agps2mnl_fd() failed");
        return -1;
    }
#endif
#else
    g_mnld_ctx.fds.fd_agps = 0;
#endif

    g_mnld_ctx.fds.fd_hal = create_hal2mnl_fd();
    if (g_mnld_ctx.fds.fd_hal < 0) {
        LOGE("create_hal2mnl_fd() failed");
        return -1;
    }

    g_mnld_ctx.fds.fd_flp = create_flphal2mnl_fd();
    if (g_mnld_ctx.fds.fd_flp < 0) {
        LOGE("create_flphal2mnl_fd() failed");
        return -1;
    }
    #if 0
    g_mnld_ctx.fds.fd_flp_test = create_flp_test2mnl_fd();
    if (g_mnld_ctx.fds.fd_flp_test < 0) {
        LOGE("create_flp2mnl_fd() failed");
        return -1;
    }
    #endif

    g_mnld_ctx.fds.fd_geofence= create_gfchal2mnl_fd();
    if (g_mnld_ctx.fds.fd_geofence< 0) {
        LOGE("create_gfchal2mnl_fd() failed");
        return -1;
    }
    g_mnld_ctx.fds.fd_at_cmd = create_at2mnl_fd();
    if (g_mnld_ctx.fds.fd_at_cmd < 0) {
        LOGE("create_at2mnl_fd() failed");
        return -1;
    }
    g_mnld_ctx.fds.fd_int = socket_bind_udp(MNLD_MAIN_SOCKET);
    if (g_mnld_ctx.fds.fd_int < 0) {
        LOGE("socket_bind_udp(MNLD_MAIN_SOCKET) failed");
        return -1;
    }
    g_mnld_ctx.fds.fd_mtklogger = create_mtklogger2mnl_fd();
    if (g_mnld_ctx.fds.fd_mtklogger < 0) {
        LOGE("create_mtklogger2mnl_fd() failed");
        return -1;
    }

    // init timers
    g_mnld_ctx.gps_status.timer_start = init_timer(mnld_gps_start_timeout);
    if (g_mnld_ctx.gps_status.timer_start == (timer_t)-1) {
        LOGE("init_timer(mnld_gps_start_timeout) failed");
        return -1;
    }

    g_mnld_ctx.gps_status.timer_stop = init_timer(mnld_gps_stop_timeout);
    if (g_mnld_ctx.gps_status.timer_stop == (timer_t)-1) {
        LOGE("init_timer(mnld_gps_stop_timeout) failed");
        return -1;
    }

    g_mnld_ctx.gps_status.timer_reset = init_timer(mnld_gps_reset_timeout);
    if (g_mnld_ctx.gps_status.timer_reset == (timer_t)-1) {
        LOGE("init_timer(mnld_gps_reset_timeout) failed");
        return -1;
    }

    g_mnld_ctx.gps_status.timer_nmea_monitor = init_timer(gps_mnld_restart_mnl_process);
    if (g_mnld_ctx.gps_status.timer_nmea_monitor == (timer_t)-1) {
        LOGE("init_timer(gps_mnld_restart_mnl_process) failed");
        return -1;
    }

    g_mnld_ctx.gps_status.timer_switch_ofl_mode = init_timer(mnld_gps_switch_ofl_mode_timeout);
    if (g_mnld_ctx.gps_status.timer_switch_ofl_mode == (timer_t)-1) {
        LOGE("init_timer(mnld_gps_switch_ofl_mode_timeout) failed");
        return -1;
    }

    // init threads
    pthread_create(&pthread_main, NULL, mnld_main_thread, NULL);

    // send the reboot message to the related modules
    mnl2hal_mnld_reboot();
#ifndef CONFIG_GPS_MT3333
    mnl2agps_mnl_reboot();
#endif
    mnl2flp_mnld_reboot();
    return 0;
}

bool mnld_is_gps_started() {
    if (g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_STARTING ||
        g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_STARTED ||
        g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_OFL_STARTING ||
        g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_OFL_STARTED) {
        return true;
    } else {
        return false;
    }
}

bool mnld_is_gps_started_done() {
    if (g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_STARTED ||
        g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_OFL_STARTED) {
        return true;
    } else {
        return false;
    }
}

bool mnld_is_gps_meas_enabled() {
    return g_mnld_ctx.gps_status.is_gps_meas_enabled;
}

bool mnld_is_gps_navi_enabled() {
    return g_mnld_ctx.gps_status.is_gps_navi_enabled;
}

bool mnld_is_gps_stopped() {
    if (g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_IDLE ||
        g_mnld_ctx.gps_status.gps_state == MNLD_GPS_STATE_STOPPING) {
        return true;
    } else {
        return false;
    }
}

bool mnld_is_epo_download_finished() {
    if (g_mnld_ctx.epo_status.is_epo_downloading == false) {
        return true;
    } else {
        return false;
    }
}

int main(int argc, char** argv) {
#ifdef CONFIG_GPS_MT3333
    LOGE("mnld version=2017/08/05 19:39");
#else
    LOGD("mnld version=0.03, offload ver=%s\n", OFL_VER);
#endif
    memset(&g_mnld_ctx, 0, sizeof(g_mnld_ctx));
    if (mnl_init()) {
        LOGE("mnl_init: %d (%s)\n", errno, strerror(errno));
    }
    chip_detector();
    /*get gps_epo_type begin*/
    if (strcmp(chip_id, "0x6572") == 0 || strcmp(chip_id, "0x6582") == 0 || strcmp(chip_id, "0x6570") == 0 ||
        strcmp(chip_id, "0x6580") == 0 || strcmp(chip_id, "0x6592") == 0 || strcmp(chip_id, "0x6571") == 0 ||
        strcmp(chip_id, "0x8127") == 0 || strcmp(chip_id, "0x0335") == 0 ||strcmp(chip_id, "0x8163") == 0) {
        gps_epo_type = 1;    // GPS only
    } else if (strcmp(chip_id, "0x6630") == 0 || strcmp(chip_id, "0x6752") == 0 || strcmp(chip_id, "0x6755") == 0
        || strcmp(chip_id, "0x6797") == 0 || strcmp(chip_id, "0x6632") == 0 || strcmp(chip_id, "0x6759") == 0
        || strcmp(chip_id, "0x6763") == 0 || strcmp(chip_id, "0x6758") == 0 || strcmp(chip_id, "0x6739") == 0
        || strcmp(chip_id, "0x6771") == 0 || strcmp(chip_id, "0x6775") == 0) {
        gps_epo_type = 0;   // G+G
    } else {
        gps_epo_type = 0;   // Default is G+G
    }
    /*get gps_epo_type end*/
    mnld_offload_check_capability();
    flp_mnl_emi_download();
#ifdef CONFIG_GPS_MT3333
	mt3333_controller_init();
	msleep(200);
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_NMEA_ONOFF);
	mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_GNSS_SYSTEM);
#endif
    if (!strncmp(argv[2], "meta", 4) || !strncmp(argv[2], "factory", 4)
        || !strncmp(argv[0], "test", 4) || !strncmp(argv[2], "PDNTest", 7)) {
        mnld_factory_test_entry(argc, argv);
    } else {
        gps_control_init();
        epo_downloader_init();
        qepo_downloader_init();
        mtknav_downloader_init();
        op01_log_init();
        mpe_function_init();
        mnld_init();
#ifdef __TEST__
        mnld_test_start();
#else
        block_here();
        //Will go here after calling mnld_block_exit
        mnld_dump_exit();
#endif
    }
#ifdef CONFIG_GPS_MT3333
    LOGE("mnld version=2017/06/25 17:27 end");
#endif	
/*
    LOGD("sizeof(mnld_context)=%d", sizeof(mnld_context));  // 48
    LOGD("sizeof(gps_location)=%d", sizeof(gps_location));  // 56
    LOGD("sizeof(gnss_sv)=%d", sizeof(gnss_sv ));           // 20
    LOGD("sizeof(gnss_sv_info)=%d", sizeof(gnss_sv_info));  // 5124
    LOGD("sizeof(gps_data)=%d", sizeof(gps_data ));         // 7752
    LOGD("sizeof(gps_nav_msg)=%d", sizeof(gps_nav_msg));    // 80
*/
    return 0;
}

