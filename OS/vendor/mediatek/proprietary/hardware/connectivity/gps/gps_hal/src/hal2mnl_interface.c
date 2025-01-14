#include "hal2mnl_interface.h"
#include "mtk_lbs_utility.h"
#include "data_coder.h"
#include <cutils/sockets.h>
#include <log/log.h>     /*logging in logcat*/

#define LOG_TAG "hal2mnl"

#if 0
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
#define LOGD(...) tag_log(1, "[gps_dbg_log]", __VA_ARGS__);
#define LOGW(...) tag_log(1, "[gps_dbg_log] WARNING: ", __VA_ARGS__);
#define LOGE(...) tag_log(1, "[gps_dbg_log] ERR: ", __VA_ARGS__);
#else
//#define LOGD(fmt, arg ...) ALOGD("%s: " fmt, __FUNCTION__ , ##arg)
//#define LOGW(fmt, arg ...) ALOGW("%s: " fmt, __FUNCTION__ , ##arg)
//#define LOGE(fmt, arg ...) ALOGE("%s: " fmt, __FUNCTION__ , ##arg)
#endif
#endif

static float count = 0;
static float report_time_interval = 0;

int hal2mnl_hal_reboot() {
    LOGD("hal2mnl_hal_reboot");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);
    put_int(buff, &offset, HAL2MNL_HAL_REBOOT);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_init() {
    LOGD("hal2mnl_gps_init");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_INIT);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_start() {
    LOGD("hal2mnl_gps_start");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_START);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}
int hal2mnl_gps_stop() {
    LOGD("hal2mnl_gps_stop");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_STOP);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_cleanup() {
    LOGD("hal2mnl_gps_cleanup");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_CLEANUP);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_inject_time(int64_t time, int64_t time_reference, int uncertainty) {
    /*LOGD("hal2mnl_gps_inject_time  time=%zd time_reference=%zd uncertainty=%d",
        time, time_reference, uncertainty);*/
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_INJECT_TIME);
    put_long(buff, &offset, time);
    put_long(buff, &offset, time_reference);
    put_int(buff, &offset, uncertainty);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_inject_location(double lat, double lng, float acc) {
    // LOGD("hal2mnl_gps_inject_location  lat,lng %f,%f acc=%f", lat, lng, acc);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_INJECT_LOCATION);
    put_double(buff, &offset, lat);
    put_double(buff, &offset, lng);
    put_float(buff, &offset, acc);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_delete_aiding_data(int flags) {
    LOGD("hal2mnl_gps_delete_aiding_data  flag=0x%x", flags);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_DELETE_AIDING_DATA);
    put_int(buff, &offset, flags);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_gps_set_position_mode(gps_pos_mode mode, gps_pos_recurrence recurrence,
    int min_interval, int preferred_acc, int preferred_time) {
    LOGD("hal2mnl_gps_set_position_mode  mode=%d recurrence=%d min_interval=%d preferred_acc=%d preferred_time=%d",
        mode, recurrence, min_interval, preferred_acc, preferred_time);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_SET_POSITION_MODE);
    put_int(buff, &offset, mode);
    put_int(buff, &offset, recurrence);
    put_int(buff, &offset, min_interval);
    put_int(buff, &offset, preferred_acc);
    put_int(buff, &offset, preferred_time);

    if (min_interval <= 1000) {
        report_time_interval = 1;
    } else {
        report_time_interval = (float)min_interval/1000;
    }
    LOGD("set report location time interval is %f s\n", report_time_interval);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_data_conn_open(const char* apn) {
    LOGD("hal2mnl_data_conn_open  apn=[%s]", apn);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_DATA_CONN_OPEN);
    put_string(buff, &offset, apn);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_data_conn_open_with_apn_ip_type(const char* apn, apn_ip_type ip_type) {
    LOGD("hal2mnl_data_conn_open_with_apn_ip_type  apn=[%s] ip_type=%d",
        apn, ip_type);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_DATA_CONN_OPEN_WITH_APN_IP_TYPE);
    put_string(buff, &offset, apn);
    put_int(buff, &offset, ip_type);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_data_conn_closed() {
    LOGD("hal2mnl_data_conn_closed");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_DATA_CONN_CLOSED);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_data_conn_failed() {
    LOGD("hal2mnl_data_conn_failed");
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_DATA_CONN_FAILED);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_server(agps_type type, const char* hostname, int port) {
    LOGD("hal2mnl_set_server  type=%d hostname=[%s] port=%d",
        type, hostname, port);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_SET_SERVER);
    put_int(buff, &offset, type);
    put_string(buff, &offset, hostname);
    put_int(buff, &offset, port);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
    return 0;
}

int hal2mnl_set_ref_location(cell_type type, int mcc, int mnc, int lac, int cid) {
    LOGD("hal2mnl_set_ref_location  type=%d mcc=%d mnc=%d lac=%d cid=%d",
        type, mcc, mnc, lac, cid);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_SET_REF_LOCATION);
    put_int(buff, &offset, type);
    put_int(buff, &offset, mcc);
    put_int(buff, &offset, mnc);
    put_int(buff, &offset, lac);
    put_int(buff, &offset, cid);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_id(agps_id_type type, const char* setid) {
    LOGD("hal2mnl_set_id  type=%d setid=[%s]", type, setid);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_SET_ID);
    put_int(buff, &offset, type);
    put_string(buff, &offset, setid);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}


int hal2mnl_ni_message(char* msg, int len) {
    LOGD("hal2mnl_ni_message  len=%d", len);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_NI_MESSAGE);
    put_binary(buff, &offset, msg, len);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_ni_respond(int notif_id, ni_user_response_type user_response) {
    LOGD("hal2mnl_ni_respond  notif_id=%d user_response=%d",
        notif_id, user_response);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_NI_RESPOND);
    put_int(buff, &offset, notif_id);
    put_int(buff, &offset, user_response);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_update_network_state(int connected, network_type type, int roaming,
    const char* extra_info) {
    LOGD("hal2mnl_update_network_state  connected=%d type=%d roaming=%d extra_info=[%s]",
        connected, type, roaming, extra_info);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_UPDATE_NETWORK_STATE);
    put_int(buff, &offset, connected);
    put_int(buff, &offset, type);
    put_int(buff, &offset, roaming);
    put_string(buff, &offset, extra_info);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_update_network_availability(int available, const char* apn) {
    LOGD("hal2mnl_update_network_availability  available=%d apn=[%s]",
        available, apn);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_UPDATE_NETWORK_AVAILABILITY);
    put_int(buff, &offset, available);
    put_string(buff, &offset, apn);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_gps_measurement(bool enabled) {
    LOGD("hal2mnl_set_gps_measurement  enabled=%d", enabled);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_MEASUREMENT);
    put_int(buff, &offset, enabled);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_gps_navigation(bool enabled) {
    LOGD("hal2mnl_set_gps_navigation   enabled=%d", enabled);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_GPS_NAVIGATION);
    put_int(buff, &offset, enabled);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_vzw_debug(bool enabled) {
    LOGD("hal2mnl_set_vzw_debug, enabled = %d\n", enabled);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_VZW_DEBUG);
    put_int(buff, &offset, enabled);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}

int hal2mnl_set_satellite_mode(int mode) {
    LOGD("hal2mnl_set_satellite_mode, mode = %d\n", mode);
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, HAL_MNL_INTERFACE_VERSION);

    put_int(buff, &offset, HAL2MNL_SATELLITE_MODE);
    put_int(buff, &offset, mode);

    return safe_sendto(MTK_HAL2MNL, buff, offset);
}


// -1 means failure
int mnl2hal_hdlr(int fd, mnl2hal_interface* hdlr) {
    char buff[HAL_MNL_BUFF_SIZE] = {0};
    int offset = 0;
    int ver;
    mnl2hal_cmd cmd;
    int read_len;
    int ret = 0;

    read_len = safe_recvfrom(fd, buff, sizeof(buff));
    if (read_len <= 0) {
        LOGE("mnl2hal_hdlr() safe_recvfrom() failed read_len=%d", read_len);
        return -1;
    }
    ver = get_int(buff, &offset);
    UNUSED(ver);
    cmd = get_int(buff, &offset);

    switch (cmd) {
    case MNL2HAL_MNLD_REBOOT: {
        if (hdlr->mnld_reboot) {
            hdlr->mnld_reboot();
        } else {
            LOGE("mnl2hal_hdlr() mnld_reboot is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_LOCATION: {
        if (report_time_interval > ++count) {
            LOGD("count is %f, interval is %f\n", count, report_time_interval);
            break;
        }
        count = 0;

        if (hdlr->location) {
            gps_location location;
            get_binary(buff, &offset, (char*)&location);
            hdlr->location(location);
        } else {
            LOGE("mnl2hal_hdlr() location is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GPS_STATUS: {
        if (hdlr->gps_status) {
            gps_status status;
            get_binary(buff, &offset, (char*)&status);
            hdlr->gps_status(status);
        } else {
            LOGE("mnl2hal_hdlr() gps_status is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GPS_SV: {
        if (hdlr->gps_sv) {
            gnss_sv_info sv;
            get_binary(buff, &offset, (char*)&sv);
            hdlr->gps_sv(sv);
        } else {
            LOGE("mnl2hal_hdlr() gps_sv is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_NMEA: {
        if (hdlr->nmea) {
            int64_t timestamp = get_long(buff, &offset);
            char* nmea = get_string(buff, &offset);
            int length = get_int(buff, &offset);
            hdlr->nmea(timestamp, nmea, length);
        } else {
            LOGE("mnl2hal_hdlr() nmea is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GPS_CAPABILITIES: {
        if (hdlr->gps_capabilities) {
            gps_capabilites capabilities;
            get_binary(buff, &offset, (char*)&capabilities);
            hdlr->gps_capabilities(capabilities);
        } else {
            LOGE("mnl2hal_hdlr() gps_capabilities is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GPS_MEASUREMENTS: {
        if (hdlr->gps_measurements) {
            gps_data data;
            get_binary(buff, &offset, (char*)&data);
            hdlr->gps_measurements(data);
        } else {
            LOGE("mnl2hal_hdlr() gps_measurements is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GPS_NAVIGATION: {
        if (hdlr->gps_navigation) {
            gps_nav_msg msg;
            get_binary(buff, &offset, (char*)&msg);
            hdlr->gps_navigation(msg);
        } else {
            LOGE("mnl2hal_hdlr() gps_navigation is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GNSS_MEASUREMENTS: {
        if (hdlr->gnss_measurements) {
            gnss_data data;
            get_binary(buff, &offset, (char*)&data);
            hdlr->gnss_measurements(data);
        } else {
            LOGE("mnl2hal_hdlr() gnss_measurements is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_GNSS_NAVIGATION: {
        if (hdlr->gnss_navigation) {
            gnss_nav_msg msg;
            get_binary(buff, &offset, (char*)&msg);
            hdlr->gnss_navigation(msg);
        } else {
            LOGE("mnl2hal_hdlr() gnss_navigation is NULL");
            ret = -1;
        }
        break;
    }

    case MNL2HAL_REQUEST_WAKELOCK: {
        if (hdlr->request_wakelock) {
            hdlr->request_wakelock();
        } else {
            LOGE("mnl2hal_hdlr() request_wakelock is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_RELEASE_WAKELOCK: {
        if (hdlr->release_wakelock) {
            hdlr->release_wakelock();
        } else {
            LOGE("mnl2hal_hdlr() release_wakelock is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_REQUEST_UTC_TIME: {
        if (hdlr->request_utc_time) {
            hdlr->request_utc_time();
        } else {
            LOGE("mnl2hal_hdlr() request_utc_time is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_REQUEST_DATA_CONN: {
        if (hdlr->request_data_conn) {
            struct sockaddr_storage addr;
            get_binary(buff, &offset, (char*)&addr);
            hdlr->request_data_conn(&addr);
        } else {
            LOGE("mnl2hal_hdlr() request_data_conn is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_RELEASE_DATA_CONN: {
        if (hdlr->release_data_conn) {
            hdlr->release_data_conn();
        } else {
            LOGE("mnl2hal_hdlr() release_data_conn is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_REQUEST_NI_NOTIFY: {
        if (hdlr->request_ni_notify) {
            int session_id = get_int(buff, &offset);
            agps_notify_type type = get_int(buff, &offset);
            char* requestor_id = get_string(buff, &offset);
            char* client_name = get_string(buff, &offset);
            ni_encoding_type requestor_id_encoding = get_int(buff, &offset);
            ni_encoding_type client_name_encoding  = get_int(buff, &offset);
            hdlr->request_ni_notify(session_id, type, requestor_id, client_name,
                requestor_id_encoding, client_name_encoding);
        } else {
            LOGE("mnl2hal_hdlr() request_ni_notify is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_REQUEST_SET_ID: {
        if (hdlr->request_set_id) {
            request_setid flags = get_int(buff, &offset);
            hdlr->request_set_id(flags);
        } else {
            LOGE("mnl2hal_hdlr() request_set_id is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_REQUEST_REF_LOC: {
        if (hdlr->request_ref_loc) {
            request_refloc flags = get_int(buff, &offset);
            hdlr->request_ref_loc(flags);
        } else {
            LOGE("mnl2hal_hdlr() request_ref_loc is NULL");
            ret = -1;
        }
        break;
    }
    case MNL2HAL_VZW_DEBUG_OUTPUT: {
        if (hdlr->output_vzw_debug) {
            char* str = get_string(buff, &offset);
            hdlr->output_vzw_debug(str);
        } else {
            LOGE("mnl2hal_hdlr() output_vzw_debug is NULL");
            ret = -1;
        }
        break;
    }

    default: {
        LOGE("mnl2hal_hdlr() unknown cmd=%d", cmd);
        ret = -1;
        break;
    }
    }

    return ret;
}

// -1 means failure
int create_mnl2hal_fd() {
    int fd = socket_bind_udp(MTK_MNL2HAL);
    socket_set_blocking(fd, 0);
    return fd;
}

