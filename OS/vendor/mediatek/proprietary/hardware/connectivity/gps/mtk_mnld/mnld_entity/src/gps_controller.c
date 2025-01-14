#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h>  /* POSIX terminal control definitions */
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/un.h>
#include <cutils/properties.h>
#include <linux/fm.h>

#include "mtk_gps.h"
#include "mtk_lbs_utility.h"
#include "data_coder.h"
#include "gps_controller.h"
#include "mnld.h"
#include "mnld_utile.h"
#include "mnl2agps_interface.h"
#include "mnl2hal_interface.h"
#include "mtk_flp_main.h"
#include "mtk_geofence_main.h"
#include "gps_dbg_log.h"
#include "mpe.h"

#include "mtk_gps_agps.h"
#include "mtk_gps_type.h"
#include "mnl_common.h"
#include "agps_agent.h"
#include "mtk_gps_sys_fp.h"
#include "SUPL_encryption.h"
#include "CFG_GPS_File.h"
#include "epo.h"
#include "qepo.h"
#include "mtknav.h"
#ifdef CONFIG_GPS_MT3333
#include "mt3333_controller.h"
#endif

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
#define LOGD(...) tag_log(1, "[gps_controlller]", __VA_ARGS__);
#define LOGW(...) tag_log(1, "[gps_controlller] WARNING: ", __VA_ARGS__);
#define LOGE(...) tag_log(1, "[gps_controlller] ERR: ", __VA_ARGS__);
#else
#define LOG_TAG "gps_controlller"
#include <cutils/sockets.h>
#include <log/log.h>     /*logging in logcat*/
#define LOGD(fmt, arg ...) ALOGD("%s: " fmt, __FUNCTION__ , ##arg)
#define LOGW(fmt, arg ...) ALOGW("%s: " fmt, __FUNCTION__ , ##arg)
#define LOGE(fmt, arg ...) ALOGE("%s: " fmt, __FUNCTION__ , ##arg)
#endif
typedef enum {
    MAIN2GPS_EVENT_START            = 0,
    MAIN2GPS_EVENT_STOP             = 1,
    MAIN2GPS_DELETE_AIDING_DATA     = 2,
    GPS2GPS_NMEA_DATA_TIMEOUT       = 3,
} main2gps_event;

#define QEPO_BD 1

#ifdef MTK_GPS_CO_CLOCK_DATA_IN_MD
#define GPS_CALI_DATA_PATH "/vendor/nvdata/md/NVRAM/CALIBRAT/ML4A_000"
#define GPS_CALI_DATA_DENALI_PATH "/vendor/nvdata/md/NVRAM/CALIBRAT/EL6N_000"
#endif

#ifdef CONFIG_GPS_MT3333
enum {
    GPS_PWRCTL_UNSUPPORTED  = 0xFF,
    GPS_PWRCTL_OFF          = 0x00,
    GPS_PWRCTL_ON           = 0x01,
    GPS_PWRCTL_RST          = 0x02,
    GPS_PWRCTL_OFF_FORCE    = 0x03,
    GPS_PWRCTL_RST_FORCE    = 0x04,
    GPS_PWRCTL_MAX          = 0x05,
};
enum {
    GPS_PWR_UNSUPPORTED     = 0xFF,
    GPS_PWR_RESUME          = 0x00,
    GPS_PWR_SUSPEND         = 0x01,
    GPS_PWR_MAX             = 0x02,
};
enum {
    MNL_GPS_STATE_UNSUPPORTED   = 0xFF,
    MNL_GPS_STATE_PWROFF        = 0x00, /*cleanup/power off, default state*/
    MNL_GPS_STATE_INIT          = 0x01, /*init*/
    MNL_GPS_STATE_START         = 0x02, /*start navigating*/
    MNL_GPS_STATE_STOP          = 0x03, /*stop navigating*/
    MNL_GPS_STATE_DEC_FREQ      = 0x04,
    MNL_GPS_STATE_SLEEP         = 0x05,
    MNL_GPS_STATE_MAX           = 0x06,
};
enum {
    GPS_PWRSAVE_UNSUPPORTED = 0xFF,
    GPS_PWRSAVE_DEC_FREQ    = 0x00,
    GPS_PWRSAVE_SLEEP       = 0x01,
    GPS_PWRSAVE_OFF         = 0x02,
    GPS_PWRSAVE_MAX         = 0x03,
};
#endif
enum {
    GPS_MEASUREMENT_UNKNOWN     = 0xFF,
    GPS_MEASUREMENT_INIT        = 0x00,
    GPS_MEASUREMENT_CLOSE       = 0x01,
};
enum {
    GPS_NAVIGATION_UNKNOWN     = 0xFF,
    GPS_NAVIGATION_INIT        = 0x00,
    GPS_NAVIGATION_CLOSE       = 0x01,
};

#define DSP_DEV                     "/dev/stpgps" /* stp-gps char dev */
#define DBG_DEV                     "/dev/ttygserial"
#define BEE_PATH                    "/data/misc/gps/"
#define DSP_BIN_LOG_FILE            "/data/misc/gps/DSPdebug.log"
#define PARM_FILE                   "/data/misc/gps/gpsparm.dat"
#define NV_FILE                     "/data/misc/gps/mtkgps.dat"
#define OFL_NV_FILE                     "/data/misc/gps/mtkgps_ofl.dat"

#define PATH_INTERNAL       "internal_sd"
#define PATH_EXTERNAL       "external_sd"
#define PATH_DEFAULT        "/mnt/sdcard/"
#define PATH_EX             "/mnt/sdcard2/"
#define SDCARD_SWITCH_PROP  "persist.mtklog.log2sd.path"
/*---------------------------------------------------------------------------*/

#ifdef CONFIG_GPS_MT3333
#define MNL_ATTR_PWRCTL  "/sys/class/gpsdrv/gps/pwrctl"
#define MNL_ATTR_SUSPEND "/sys/class/gpsdrv/gps/suspend"
#define MNL_ATTR_STATE   "/sys/class/gpsdrv/gps/state"
#define MNL_ATTR_PWRSAVE "/sys/class/gpsdrv/gps/pwrsave"
#define MNL_ATTR_STATUS  "/sys/class/gpsdrv/gps/status"
#define MNL_ATTR_RDELAY  "/sys/class/gpsdrv/gps/rdelay"
#endif

#define GET_VER
#ifdef TCXO
#undef TCXO
#endif
#define TCXO 0
#ifdef CO_CLOCK
#undef CO_CLOCK
#endif
#define CO_CLOCK 1
#ifdef CO_DCXO
#undef CO_DCXO
#endif
#define CO_DCXO 2
#define GPS_CLOCK_TYPE_P    "gps.clock.type"
#define COMBO_IOC_GPS_IC_HW_VERSION   7
#define COMBO_IOC_GPS_IC_FW_VERSION   8

#define COMBO_IOC_TRIGGER_WMT_ASSERT        12
#define COMBO_IOC_TRIGGER_WMT_SUBSYS_RESET  13
#define COMBO_IOC_TAKE_GPS_WAKELOCK         14
#define COMBO_IOC_GIVE_GPS_WAKELOCK         15

static int g_fd_gps;
/////////////// temporary defineded for Android ////////////
//////////////////////////////////////////

// for one binary
#define RAW_DATA_SUPPORT 1
#if RAW_DATA_SUPPORT
#define GPS_CONF_FILE_SIZE 100
#define RAW_DATA_CONTROL_FILE_PATH "/data/misc/gps/gps.conf"
static int gps_raw_debug_mode = 0;
static int mtk_msg_raw_meas_flag = 0;
#define IS_SPACE(ch) ((ch == ' ') || (ch == '\t') || (ch == '\n'))
#endif

// static unsigned char gps_measurement_state = GPS_MEASUREMENT_UNKNOWN;
// static unsigned char gps_navigation_state = GPS_NAVIGATION_UNKNOWN;
extern unsigned char gps_debuglog_state;
extern char gps_debuglog_file_name[GPS_DEBUG_LOG_FILE_NAME_MAX_LEN];
extern bool g_gpsdbglogThreadExit;
extern MTK_GPS_MNL_INFO mtk_gps_mnl_info;
extern UINT32 g_dbglog_file_size_in_config;    //Max dbg log file size read from config file
extern UINT32 g_dbglog_folder_size_in_config;  //Max dbg log folder size read from config file


UINT32 assist_data_bit_map = FLAG_HOT_START;
static UINT32 delete_aiding_data;
#if MTK_GPS_NVRAM
int gps_nvram_valid = 0;
ap_nvram_gps_config_struct stGPSReadback;
#endif
int g_is_1Hz = 1;
int dsp_fd = -1;
#if ANDROID_MNLD_PROP_SUPPORT
char chip_id[PROPERTY_VALUE_MAX]={0};
#else
char chip_id[100]={0};
#endif
int epo_setconfig = 0;
extern int gps_epo_type;
int start_time_out = MNLD_GPS_START_TIMEOUT;
int nmea_data_time_out = MNLD_GPS_NMEA_DATA_TIMEOUT;
static int exit_meta_factory = 0;
static int PDN_test_enable = 0;
static int in_meta_factory = 0;
#ifdef CONFIG_GPS_MT3333
extern MNL_CONFIG_T mnl_config;
extern int is_ygps_delete_data;
#endif

static int gps_restart = 0;
#define M_RESTART 0
static SYNC_LOCK_T lock_for_sync[] = {{PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0}};
static int mtk_gps_log_hide = 1;

#define HIDE_LOG_PROP "ro.mtk_log_hide_gps"

#define fieldp_copy(dst, src, f)  (dst)->f  = (src)->f
#define field_copy(dst, src, f)  (dst).f   = (src).f

extern UINT8 sv_type_agps_set;
extern UINT8 sib8_16_enable;
extern UINT8 lppe_enable;

extern int mtk_gps_sys_init();
extern int mtk_gps_sys_uninit();
#ifdef CONFIG_GPS_MT3333
int mnl_set_pwrctl(unsigned char pwrctl);
#endif
static int mnld_gps_start_impl(void);
static int mnld_gps_stop_impl(void);
static int linux_gps_init(void);
#if ANDROID_MNLD_PROP_SUPPORT
static int get_prop(int index);
#endif
static int gps_raw_data_enable(void);
static void get_gps_measurement_clock_data();
static void get_gnss_measurement_clock_data();
static void get_gps_navigation_event();
static void get_gnss_navigation_event();

/////////////////////////////////////////////////////////////////////////////
// MAIN -> GPS Control (handlers)
static int mnld_gps_start(int delete_aiding_data_flags) {
    LOGD("mnld_gps_start flag=0x%x", delete_aiding_data_flags);

    int ret = 0;
    assist_data_bit_map = delete_aiding_data_flags;

#ifdef CONFIG_GPS_MT3333
	if ((ret = mnl_set_pwrctl(GPS_PWRCTL_ON)))  /*if current state is power off*/
        return ret;
#endif

    if ((ret = mnld_gps_start_impl())) {
        LOGW("mnld_gps_start() fail, ret=%d", ret);
        return ret;
    } else {
        LOGD("mnld_gps_start() success");
    }

    return 0;
}

static int mnld_gps_stop() {
    LOGD("mnld_gps_stop begin");
    int err = 0;

    mnld_gps_stop_nmea_monitor();
    if ((err = mnld_gps_stop_impl())) {
        LOGD("mnld_gps_stop_impl: err = %d", err);
        return err;
    } else {
        mnld_gps_stop_done();
        LOGD("mnld_gps_stop_impl success");
    }

    return 0;
}

static int mnld_gps_delete_aiding_data(int delete_aiding_data_flags) {
    LOGD("mnld_gps_delete_aiding_data flag=0x%x", delete_aiding_data_flags);
    MTK_GPS_PARAM_RESTART restart = {MTK_GPS_START_HOT};
    int ret = 0;
    if (delete_aiding_data_flags == FLAG_HOT_START) {
        restart.restart_type = MTK_GPS_START_HOT;
    } else if (delete_aiding_data_flags == FLAG_WARM_START) {
        restart.restart_type = MTK_GPS_START_WARM;
    } else if (delete_aiding_data_flags == FLAG_COLD_START) {
        restart.restart_type = MTK_GPS_START_COLD;
    } else if (delete_aiding_data_flags == FLAG_FULL_START) {
        restart.restart_type = MTK_GPS_START_FULL;
    } else if (delete_aiding_data_flags == FLAG_AGPS_START) {
        restart.restart_type = MTK_GPS_START_AGPS;
    } else if (delete_aiding_data_flags == FLAG_DELETE_EPH_ALM_START) {
        restart.restart_type = MTK_GPS_START_D_EPH_ALM;
    } else if (delete_aiding_data_flags == FLAG_DELETE_TIME_START) {
        restart.restart_type = MTK_GPS_START_D_TIME;
    } else if (delete_aiding_data_flags == FLAG_AGPS_HOT_START) {
        restart.restart_type = MTK_GPS_START_HOT;
    } else if (delete_aiding_data_flags == FLAG_AGPS_WARM_START) {
        restart.restart_type = MTK_GPS_START_WARM;
    } else if (delete_aiding_data_flags == FLAG_AGPS_COLD_START) {
        restart.restart_type = MTK_GPS_START_COLD;
    } else if (delete_aiding_data_flags == FLAG_AGPS_FULL_START) {
        restart.restart_type = MTK_GPS_START_FULL;
    } else if (delete_aiding_data_flags == FLAG_AGPS_AGPS_START) {
        restart.restart_type = MTK_GPS_START_AGPS;
    } else {
#ifndef CONFIG_GPS_MT3333    
        assist_data_bit_map = delete_aiding_data_flags;
        mtk_gps_delete_nv_data(assist_data_bit_map);
#endif		
    }
#ifndef CONFIG_GPS_MT3333
    if ((ret = mtk_gps_set_param(MTK_PARAM_CMD_RESTART, &restart))) {
        LOGE("GPS restart fail %d", ret);
    }
#else
	is_ygps_delete_data=10;
	mt3333_controller_delete_aiding_data(delete_aiding_data_flags);
#endif
    gps_restart = 1;
    get_condition(&lock_for_sync[M_RESTART]);
    gps_restart = 0;
    mnld_gps_reset_done();
    return 0;
}
/////////////////////////////////////////////////////////////////////////////

/*****************************************************************************/
/*****************************************************************************/
#ifdef CONFIG_GPS_MT3333
/*****************************************************************************/
static int mnl_read_attr(const char *name, unsigned char *attr) {
    int fd = open(name, O_RDWR);
    unsigned char buf;
    int err = 0;

    if (fd == -1) {
        LOGE("open %s err = %s\n", name, strerror(errno));
        return err;
    }
    do {
        err = read(fd, &buf, sizeof(buf));
    } while (err < 0 && errno == EINTR);
    if (err != sizeof(buf)) {
        LOGE("read fails = %s\n", strerror(errno));
        err = -1;
    } else {
        err = 0;    /*no error*/
    }
    if (close(fd) == -1) {
        LOGE("close fails = %s\n", strerror(errno));
        err = (err) ? (err) : (-1);
    }
    if (!err)
        *attr = buf - '0';
    else
        *attr = 0xFF;
    return err;
}

static int mnl_write_attr(const char *name, unsigned char attr) {
    int err, fd = open(name, O_RDWR);
    char buf[] = {attr + '0'};

    if (fd == -1) {
        LOGE("open %s err = %s\n", name, strerror(errno));
        return -errno;
    }
    do { err = write(fd, buf, sizeof(buf));
    } while (err < 0 && errno == EINTR);

    if (err != sizeof(buf)) {
        LOGE("write fails = %s\n", strerror(errno));
        err = -errno;
    } else {
        err = 0;    // no error
    }
    if (close(fd) == -1) {
        LOGE("close fails = %s\n", strerror(errno));
        err = (err) ? (err) : (-errno);
    }
    LOGD("write '%d' to %s okay\n", attr, name);
    return err;
}

int mnl_set_pwrctl(unsigned char pwrctl) {
	LOGE("==> pwrctl = %d\n", pwrctl);
    if (pwrctl < GPS_PWRCTL_MAX) {
        return mnl_write_attr(MNL_ATTR_PWRCTL, pwrctl);
    } else {
        LOGE("invalid pwrctl = %d\n", pwrctl);
        errno = -EINVAL;
        return -1;
    }
}
#endif
/*****************************************************************************/
#ifdef CONFIG_GPS_MT3333
/*****************************************************************************/
static int mnl_set_state(unsigned char state) {

    int err;
    if (state < MNL_GPS_STATE_MAX) {
        if ((err = mnl_write_attr(MNL_ATTR_STATE, state)))
            return err;
        return 0;
    } else {
        LOGE("invalid state = %d\n", state);
        errno = -EINVAL;
        return -1;
    }
}
/*****************************************************************************/
static int mnl_get_state(unsigned char *state) {
    return mnl_read_attr(MNL_ATTR_STATE, state);
}
/*****************************************************************************/
int mnl_set_rdelay(int delay) {
    const char *name = MNL_ATTR_RDELAY;
	char buf[16]={0};
	int len=0;
    int err, fd = open(name, O_RDWR);

	len=sprintf(buf, "%d\n",delay);
	len+=1;
	LOGE("len=%d, buf= %s", len, buf);
	
    if (fd == -1) {
        LOGE("open %s err = %s\n", name, strerror(errno));
        return -errno;
    }
    do {
        err = write(fd, buf, len);
    } while (err < 0 && errno == EINTR);

    if (err != len) {
        LOGE("write fails = %s\n", strerror(errno));
        err = -errno;
    } else {
        err = 0;    /*no error*/
    }
    if (close(fd) == -1) {
        LOGE("close fails = %s\n", strerror(errno));
        err = (err) ? (err) : (-errno);
    }
    return err;
}

int mnl_get_rdelay(void) {
    const char *name = MNL_ATTR_RDELAY;
	char buf[16]={0};
	int len=sizeof(buf);
	int delay=0;
    int err, fd = open(name, O_RDWR);

    if (fd == -1) {
        LOGE("open %s err = %s\n", name, strerror(errno));
        return -errno;
    }
    do {
        err = read(fd, buf, len);
    } while (err < 0 && errno == EINTR);
	LOGE("buf=%s", buf);
    sscanf(buf,"%d\n", &delay);
    if (close(fd) == -1) {
        LOGE("close fails = %s\n", strerror(errno));
        err = (err) ? (err) : (-errno);
    }
	LOGE("delay=%d", delay);
    return delay;
}

#endif
#ifndef CONFIG_GPS_MT3333
static MNL_CONFIG_T mnld_cfg = {
#else
MNL_CONFIG_T mnld_cfg = {
	.init_speed = 115200,
    .link_speed = 115200,
#endif    
    .timeout_init  = MNLD_GPS_START_TIMEOUT,
    .timeout_monitor = MNLD_GPS_NMEA_DATA_TIMEOUT,
    .OFFLOAD_enabled = 0,
    .OFFLOAD_switchMode = 0,    //0: always offload mode  1: Offload/Host-base auto switch mode
};
#ifndef CONFIG_GPS_MT3333
static MNL_CONFIG_T mnl_config = {
    .init_speed = 38400,
    .link_speed = 921600,
#else
MNL_CONFIG_T mnl_config = {
    .init_speed = 115200,
    .link_speed = 115200,
#endif
    .debug_nmea = 1,
    .debug_mnl  = MNL_NEMA_DEBUG_SENTENCE,  /*MNL_NMEA_DEBUG_NORMAL,*/
    .pmtk_conn  = PMTK_CONNECTION_SOCKET,
    .socket_port = 7000,
    .dev_dbg = DBG_DEV,
    .dev_dsp = DSP_DEV,
    .dev_gps = "UseCallback",
    .bee_path = BEE_PATH,
    .epo_file = EPO_FILE,
    .epo_update_file = EPO_UPDATE_HAL,
    .qepo_file = QEPO_FILE,
    .qepo_update_file = QEPO_UPDATE_HAL,
    .delay_reset_dsp = 500,
    .nmea2file = 0,
    .dbg2file = 0,
    .nmea2socket = 1,
    .dbg2socket = 0,
    .timeout_init = 0,
    .timeout_monitor = 0,
    .timeout_wakeup = 0,
    .timeout_sleep = 0,
    .timeout_pwroff = 0,
    .timeout_ttff = 0,
    .EPO_enabled = 1,
    .BEE_enabled = 0,
    .SUPL_enabled = 1,
    .SUPLSI_enabled = 1,
    .EPO_priority = 64,
    .BEE_priority = 32,
    .SUPL_priority = 96,
    .fgGpsAosp_Ver = 1,
    .AVAILIABLE_AGE = 2,
    .RTC_DRIFT = 30,
    .TIME_INTERVAL = 10,
    .u1AgpsMachine = 0,  // default use spirent "0"
    .ACCURACY_SNR = 1,
    .GNSSOPMode = 1,     // 0: G+G; 1: G+B
    .dbglog_file_max_size = 25*1024*1024,
    .dbglog_folder_max_size = 300*1024*1024
};
void mtk_null(UINT16 a) {
    LOGD("mtk_null a=%d\n", a);
    return;
}
int SUPL_encrypt_wrapper(unsigned char *plain,
                         unsigned char *cipher, unsigned int length) {
    return SUPL_encrypt(plain, cipher, length);
}

int SUPL_decrypt_wrapper(unsigned char *plain,
                         unsigned char *cipher, unsigned int length) {
    return SUPL_decrypt(plain, cipher, length);
}

MTK_GPS_SYS_FUNCTION_PTR_T porting_layer_callback = {
    .sys_gps_mnl_callback = mtk_gps_sys_gps_mnl_callback,
    .sys_nmea_output_to_app = mtk_gps_sys_nmea_output_to_app,
    .sys_nmea_output_to_mnld = mtk_gps_sys_nmea_output_to_mnld,
    .sys_frame_sync_enable_sleep_mode = mtk_gps_sys_frame_sync_enable_sleep_mode,
    .sys_frame_sync_meas_req_by_network = mtk_gps_sys_frame_sync_meas_req_by_network,
    .sys_frame_sync_meas_req = mtk_gps_sys_frame_sync_meas_req,
    .sys_agps_disaptcher_callback = mtk_gps_sys_agps_disaptcher_callback,
    .sys_pmtk_cmd_cb = mtk_null,
    .encrypt = SUPL_encrypt_wrapper,
    .decrypt = SUPL_decrypt_wrapper,
    .ofl_sys_rst_stpgps_req = mtk_gps_ofl_sys_rst_stpgps_req,
    .ofl_sys_submit_flp_data = mtk_gps_ofl_sys_submit_flp_data,
    .sys_alps_gps_dbg2file_mnld = mnl_sys_alps_gps_dbg2file_mnld,
    .ofl_sys_mnl_offload_callback = mtk_gps_ofl_sys_mnl_offload_callback,
};

/*****************************************************************************/
int mtk_gps_log_is_hide(void){
    return !!(mtk_gps_log_hide );
}

bool mnl_offload_is_enabled() {
    return !!(mnld_cfg.OFFLOAD_enabled);
}

void mnl_offload_set_enabled(void) {
    mnld_cfg.OFFLOAD_enabled = 1;
}

void mnl_offload_clear_enabled(void) {
    mnld_cfg.OFFLOAD_enabled = 0;
}

#ifdef CONFIG_GPS_MT3333
extern int fw_downloading; //wangguojun modify

int gps_driver_state_init() {
    int ret = 0;
    if(fw_downloading==1)//wangguojun modify
	return 0;
    if ((ret = mnl_set_pwrctl(GPS_PWRCTL_OFF)))  /*default power off*/
        return ret;
    ret = mnl_set_state(MNL_GPS_STATE_INIT);
    return ret;
}
#endif

/*****************************************************************************
 * FUNCTION
 *  mnld_timeout_ne_enabled
 * DESCRIPTION
 *  To get the property and check need to trigger MNLD ne or not
 *  If timeout NE trigger enabled, will trigger NE
 *  If timeout NE trigger disabled, will use "dump + exit" to instead NE(default)
 * PARAMETERS
 *  None
 * RETURNS
 *  None
 *****************************************************************************/
bool mnld_timeout_ne_enabled(void) {
#if ANDROID_MNLD_PROP_SUPPORT
    char result[PROPERTY_VALUE_MAX] = {0};

    if((in_meta_factory == 1)
      ||((property_get("debug.gps.mnld.ne", result, NULL) != 0) && (result[0] == '1'))) {
        LOGD("mnld NE enabled!!!");
        return true;
    } else {
        LOGD("mnld NE disabled!!!");
        return false;
    }
#else
    if(in_meta_factory == 1)  {
        LOGD("mnld NE enabled!!!");
        return true;
    } else {
        LOGD("mnld NE disabled!!!");
        return false;
    }
#endif
}
bool mnld_support_auto_switch_mode() {
    bool ret = false;
    if (mnld_cfg.OFFLOAD_enabled && (mnld_cfg.OFFLOAD_switchMode == 1)) {
        ret = true;
    }
    return ret;
}

bool mnld_offload_is_auto_mode() {
    bool ret = false;
    if (mnld_cfg.OFFLOAD_enabled && (mnld_cfg.OFFLOAD_switchMode == 1) &&
        ((mnld_flp_session.type == MNLD_FLP_CAPABILITY_OFL_MODE) ||
        (mnld_gfc_session.type == MNLD_GFC_CAPABILITY_OFL_MODE))) {
        ret = true;
    }
    return ret;
}

bool mnld_offload_is_always_on_mode() {
    bool ret = false;
    if (mnld_cfg.OFFLOAD_enabled && (mnld_cfg.OFFLOAD_switchMode == 0)) {
        ret = true;
    }
    return ret;
}

void mnld_offload_check_capability() {
    if (chip_id[0] == 0) {
        chip_detector();
    }
    if (strcmp(chip_id, "0x6758") == 0 || strcmp(chip_id, "0x6771") == 0
        || strcmp(chip_id, "0x6775") == 0) {
        mnld_cfg.OFFLOAD_enabled = 1;
        mnld_cfg.OFFLOAD_switchMode = 1;
        mnld_flp_session.type = MNLD_FLP_CAPABILITY_AP_OFL_MODE;
        mnld_flp_session.pre_type = MNLD_FLP_CAPABILITY_AP_OFL_MODE;
        mnld_gfc_session.type = MNLD_GFC_CAPABILITY_AP_OFL_MODE;
        mnld_gfc_session.pre_type = MNLD_GFC_CAPABILITY_AP_OFL_MODE;
    }
}

int mnl_init() {
    int err;

    if(mtk_gps_get_mnl_info(&mtk_gps_mnl_info)) {
        LOGE("get mnl ver infor fail\n");
    }
    lppe_enable=mtk_gps_mnl_info.support_lppe;
    /*setup property*/
	mnl_utl_load_property(&mnl_config);
    if (!mnl_utl_load_property(&mnld_cfg)) {
        start_time_out = mnld_cfg.timeout_init;
        nmea_data_time_out = mnld_cfg.timeout_monitor;
    }

    gps_dbg_log_property_load();
#ifndef CONFIG_GPS_MT3333
    MTK_GPS_SYS_FUNCTION_PTR_T*  mBEE_SYS_FP = &porting_layer_callback;
    if (mtk_gps_sys_function_register(mBEE_SYS_FP) != MTK_GPS_SUCCESS) {
        LOGE("register callback for mnl fail\n");
    }
    else {
        LOGD("register callback for mnl okey, mtk_gps_sys_function_register=%p\n", mtk_gps_sys_function_register);
    }
#endif
    LOGD("MNLD can support offload/non-offload, ver = %s.\n", OFL_VER);
    return 0;
}
void get_gps_version() {
#if ANDROID_MNLD_PROP_SUPPORT
    if (strcmp(chip_id, "0x0321") == 0 || strcmp(chip_id, "0x0335") == 0 ||
        strcmp(chip_id, "0x0337") == 0 ||strcmp(chip_id, "0x6735") == 0) {
        property_set("gps.gps.version", "0x6735");  // Denali1/2/3
    } else {
        property_set("gps.gps.version", chip_id);
    }
    return;
#endif
}
void get_chip_sv_support_capability(unsigned char* sv_type) {
    if (strcmp(chip_id, "0x6620") == 0 || strcmp(chip_id, "0x6628") == 0 ||
        strcmp(chip_id, "0x3336") == 0 || strcmp(chip_id, "0x6572") == 0 ||
        strcmp(chip_id, "0x6582") == 0 || strcmp(chip_id, "0x6592") == 0 ||
        strcmp(chip_id, "0x6571") == 0 || strcmp(chip_id, "0x6580") == 0 ||
        strcmp(chip_id, "0x0335") == 0 || strcmp(chip_id, "0x6570") == 0) {
        *sv_type = 1;  // GPS only
    } else if (strcmp(chip_id, "0x3332") == 0 || strcmp(chip_id, "0x6752") == 0 ||
        strcmp(chip_id, "0x0321") == 0 || strcmp(chip_id, "0x0337") == 0 ||
        strcmp(chip_id, "0x6755") == 0 || strcmp(chip_id, "0x6757") == 0 ||
        strcmp(chip_id, "0x6763") == 0 || strcmp(chip_id, "0x6739") == 0) {
        *sv_type = 3;  // GPS+Glonass
    } else if (strcmp(chip_id, "0x6797") == 0 || strcmp(chip_id, "0x6630") == 0 ||
        strcmp(chip_id, "0x6759") == 0 || strcmp(chip_id, "0x6758") == 0 ||
        strcmp(chip_id, "0x6771") == 0 || strcmp(chip_id, "0x6775") == 0) {
        *sv_type = 7;  // GPS+Glonass+Beidou
    } else if (strcmp(chip_id, "0x6632") == 0) {
        *sv_type = 15;  // GPS+Glonass+Beidou+Galileo
    }
}

int hasAlmanac() {
    char ch;
    char* ptr;
    int i = -1;
    int size = -1;
    FILE *fp;
    char fileName[] = "/nvcfg/almanac.dat";
    char str[32] = {0};
    char strTime[32] = {0};
    char strSatNum[32] = {0};
    int almanac_sat_num = 0;

    if ((fp = fopen(fileName, "r")) == NULL) {
        LOGE("open file(%s) fail", fileName);
        return 0;
    }

    fseek(fp, 0L, SEEK_END);
    size = ftell(fp);
    if (size == 0) {
        LOGD("file(%s) is empty", fileName);
        fclose(fp);
        return 0;
    }
    if (fseek(fp, i, SEEK_END) == -1){
        LOGE("fseek fail,offset:%d", i);
        fclose(fp);
        return 0;
    }

    ch = fgetc(fp);
    while ((ch != '\n') && (size != 0)) {
        i--;
        if (fseek(fp, i, SEEK_END) == -1){
            LOGE("fseek fail,offset:%d", i);
            fclose(fp);
            return 0;
        }
        size = ftell(fp);
        ch = fgetc(fp);
    }

    i = 0;
    ch = fgetc(fp);

    while (!feof(fp)) {
        if ((ch >= '0' && ch <= '9') || (ch == ',')) {
            str[i] = ch;
            i++;
        }
        ch = fgetc(fp);
    }
    fclose(fp);
    LOGD("almanac.dat last line, str=%s\n", str);

    ptr = strchr(str, ',');
    if (ptr) {
        MNLD_STRNCPY(strTime, ptr + 1, sizeof(strTime));
        *ptr = '\0';
        MNLD_STRNCPY(strSatNum, str, sizeof(strSatNum));
        LOGD("strSatNum=%s, strTime=%s\n", strSatNum, strTime);
    } else {
        LOGD("don't find dot in almanac.dat last line, return\n");
        return 0;
    }

    if (strlen(strSatNum) > 0) {
        almanac_sat_num = atoi(strSatNum);
    }
#if ANDROID_MNLD_PROP_SUPPORT
    if (almanac_sat_num >= 25) {
        property_set("gps.almanac", strTime);
        return 1;
    } else {
        property_set("gps.almanac", "0");
    }
#endif
    return 0;
}

static int mnld_gps_start_impl(void) {
    int ret = 0;
    int gps_user = GPS_USER_UNKNOWN;

    //LOGD("mnld_gps_start_impl");
    mnl_utl_load_property(&mnl_config);

    g_dbglog_file_size_in_config = mnl_config.dbglog_file_max_size;
    g_dbglog_folder_size_in_config = mnl_config.dbglog_folder_max_size;

    gps_user = mtk_gps_get_gps_user();
    LOGD("gps_user=0x%x\n", gps_user);
#ifndef CONFIG_GPS_MT3333	
    mtk_gps_ofl_set_user(gps_user);
#endif

    /*initialize system resource (message queue, mutex) used by library*/
    if ((ret = mtk_gps_sys_init())) {
        LOGD("mtk_gps_sys_init err = %d\n", errno);
    } else {
        LOGD("mtk_gps_sys_init() success\n");
    }
    // mnld_gps_start_nmea_monitor();
    // start library gps run
    if ((ret = linux_gps_init())) {
        LOGD("linux_gps_init err = %d\n", errno);
        mnld_gps_stop_impl();
        return ret;
    } else {
        LOGD("linux_gps_init() success\n");
    }

    if ((ret = linux_setup_signal_handler())) {
        LOGD("linux_setup_signal_handler err = %d\n", errno);
        mnld_gps_stop_impl();
        return ret;
    } else {
        LOGD("linux_setup_signal_handler() success\n");
    }

    get_gps_version();
    return ret;
}

/*****************************************************************************/
#ifndef CONFIG_GPS_MT3333
static int linux_gps_init(void) {
    int clock_type;
    int gnss_mode_flag = 0;
    INT32 status = MTK_GPS_SUCCESS;
    static MTK_GPS_INIT_CFG init_cfg;
    static MTK_GPS_DRIVER_CFG driver_cfg;
    MTK_GPS_BOOT_STATUS mnl_status = 0;
    double latitude, longitude;
    int accuracy  = 100;
    int ret_val = 0;

    memset(&init_cfg, 0, sizeof(MTK_GPS_INIT_CFG));
    memset(&driver_cfg, 0, sizeof(MTK_GPS_DRIVER_CFG));
    MTK_AGPS_USER_PROFILE userprofile;
    memset(&userprofile, 0, sizeof(MTK_AGPS_USER_PROFILE));
    //  ====== default config ======
    init_cfg.if_type = MTK_IF_UART_NO_HW_FLOW_CTRL;
    init_cfg.pps_mode = MTK_PPS_DISABLE;        //  PPS disabled
    init_cfg.pps_duty = 100;                    //  pps_duty (100ms high)
    init_cfg.if_link_spd = 115200;              //  115200bps

    UINT32 hw_ver = 0;
    UINT32 fw_ver = 0;

#ifdef MTK_GPS_CO_CLOCK_DATA_IN_MD
    typedef struct gps_nvram_t {
        unsigned int C0;
        unsigned int C1;
        unsigned int initU;
        unsigned int lastU;
    }GPS_NVRAM_COCLOCK_T;
    GPS_NVRAM_COCLOCK_T gps_clock_calidata;
    int fd = -1;
    int read_size;
    int dsp_open_retry;
    if ((strcmp(chip_id, "0x6735") == 0) || (strcmp(chip_id, "0x0321") == 0)
        || (strcmp(chip_id, "0x0335") == 0) || (strcmp(chip_id, "0x0337") == 0)) {
        LOGD("Denali calibration,chip_id:%s",chip_id);
        fd = open(GPS_CALI_DATA_DENALI_PATH, O_RDONLY, 660);
    } else {
        LOGD("Other calibration,chip_id:%s",chip_id);
        fd = open(GPS_CALI_DATA_PATH, O_RDONLY, 660);
    }

    if (fd == -1) {
        LOGD("open error is %s\n", strerror(errno));
        gps_clock_calidata.C0 = 0x0;
        gps_clock_calidata.C1 = 0x0;
        gps_clock_calidata.initU = 0x0;
        gps_clock_calidata.lastU = 0x0;
    } else {
        if ((strcmp(chip_id, "0x6763") == 0) || (strcmp(chip_id, "0x6739") == 0)
            || (strcmp(chip_id, "0x6771") == 0)) {
            LOGD("lseek calibration file for 93MD");
            if (lseek(fd, 0x40, SEEK_SET) < 0) {
                LOGW("MD NVRam file lseek failed!!\n");
            }
        }
        read_size = read(fd, &gps_clock_calidata, sizeof(GPS_NVRAM_COCLOCK_T));
        if (read_size != sizeof(GPS_NVRAM_COCLOCK_T)) {
            LOGD("read size is %d, structure size is %d\n", read_size, sizeof(GPS_NVRAM_COCLOCK_T));
        }
        close(fd);
        fd = -1;
    }
    init_cfg.C0 = gps_clock_calidata.C0;
    init_cfg.C1 = gps_clock_calidata.C1;
    init_cfg.initU = gps_clock_calidata.initU;
    init_cfg.lastU = gps_clock_calidata.lastU;
#endif
#if MTK_GPS_NVRAM
    if (gps_nvram_valid == 1) {
        init_cfg.hw_Clock_Freq = stGPSReadback.gps_tcxo_hz;            //  26MHz TCXO,
        init_cfg.hw_Clock_Drift = stGPSReadback.gps_tcxo_ppb;                 //  0.5ppm TCXO
        init_cfg.Int_LNA_Config = stGPSReadback.gps_lna_mode;                   //  0 -> Mixer in , 1 -> Internal LNA
        init_cfg.u1ClockType = stGPSReadback.gps_tcxo_type;  // clk_type;
  #ifdef MTK_GPS_CO_CLOCK_DATA_IN_MD
        if (strcmp(chip_id, "0x6580") == 0 || strcmp(chip_id, "0x6570") == 0) {
          LOGD("calibration read from AP NVRAM\n");
          init_cfg.C0 = stGPSReadback.C0;
          init_cfg.C1 = stGPSReadback.C1;
          init_cfg.initU = stGPSReadback.initU;
          init_cfg.lastU = stGPSReadback.lastU;
        }
  #else
        init_cfg.C0 = stGPSReadback.C0;
        init_cfg.C1 = stGPSReadback.C1;
        init_cfg.initU = stGPSReadback.initU;
        init_cfg.lastU = stGPSReadback.lastU;
  #endif
    } else {
#endif
        init_cfg.hw_Clock_Freq = 26000000;             //  26MHz TCXO
        init_cfg.hw_Clock_Drift = 2000;                 //  0.5ppm TCXO
        init_cfg.Int_LNA_Config = 0;                    //  0 -> Mixer in , 1 -> Internal LNA
        init_cfg.u1ClockType = 0xFF;  // clk_type;
#if MTK_GPS_NVRAM
    }
#endif
    if (init_cfg.hw_Clock_Drift == 0) {
        LOGD("customer didn't set clock drift value, use default value\n");
        init_cfg.hw_Clock_Drift = 2000;
    }

    /*setting 1Hz/5Hz */
    if (g_is_1Hz) {
        init_cfg.fix_interval = 1000;               //  1Hz update rate
    } else {
        init_cfg.fix_interval = 200;               //  5Hz update rate
    }

    init_cfg.datum = MTK_DATUM_WGS84;           //  datum
    init_cfg.dgps_mode = MTK_DGPS_MODE_SBAS;    //  enable SBAS

    dsp_open_retry = 0;
    while (1) {
        dsp_fd = open(mnl_config.dev_dsp, O_RDWR);
        if (dsp_fd == -1) {
            LOGD("open_port: Unable to open - %s \n", mnl_config.dev_dsp);
            if (dsp_open_retry <= 20) {
                usleep(1000*1000);
                dsp_open_retry++;
                LOGD("open_port: sleep and contine to do %d retry", dsp_open_retry);
                continue;
            } else {
                LOGE("open_port: %d retry still fail, return err", dsp_open_retry);
            }
            return MTK_GPS_ERROR;
        } else {
            LOGD("open dsp successfully\n");
        }
        break;
    }
#if ANDROID_MNLD_PROP_SUPPORT
{
    char result[PROPERTY_VALUE_MAX] = {0};
    property_get(HIDE_LOG_PROP, result, NULL);
    if (result[0] == '1') {
        mtk_gps_log_hide = 1;
    }else{
        mtk_gps_log_hide = 0;
    }
}
#endif

    if (chip_id[0] == 0) {
        chip_detector();
    }

#if ANDROID_MNLD_PROP_SUPPORT
    // strcpy(driver_cfg.mnl_chip_id, chip_id);
    if (strcmp(chip_id, "0x6592") == 0 || strcmp(chip_id, "0x6571") == 0
        || strcmp(chip_id, "0x6580") == 0 || strcmp(chip_id, "0x0321") == 0
        || strcmp(chip_id, "0x0335") == 0 || strcmp(chip_id, "0x0337") == 0
        || strcmp(chip_id, "0x6735") == 0 || strcmp(chip_id, "0x8163") == 0
        || strcmp(chip_id, "0x8127") == 0 || strcmp(chip_id, "0x6755") == 0
        || strcmp(chip_id, "0x6797") == 0 || strcmp(chip_id, "0x6757") == 0
        || strcmp(chip_id, "0x6759") == 0 || strcmp(chip_id, "0x6763") == 0
        || strcmp(chip_id, "0x6758") == 0 || strcmp(chip_id, "0x6570") == 0
        || strcmp(chip_id, "0x6739") == 0 || strcmp(chip_id, "0x6771") == 0
        || strcmp(chip_id, "0x6775") == 0) {
        clock_type = ioctl(dsp_fd, 11, NULL);
        clock_type = clock_type & 0x00ff;
        switch (clock_type) {
        case 0x00:
            LOGD("TCXO, buffer 2\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "20") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x10:
            LOGD("TCXO, buffer 1\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "10") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x20:
            LOGD("TCXO, buffer 2\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "20") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x30:
            LOGD("TCXO, buffer 3\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "30") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x40:
            LOGD("TCXO, buffer 4\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "40") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x01:
            LOGD("GPS coclock, buffer 2, coTMS\n");
            init_cfg.u1ClockType = 0xFE;
            if (property_set(GPS_CLOCK_TYPE_P, "21") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x02:
        case 0x03:
            LOGD("TCXO, buffer 2, coVCTCXO\n");
            init_cfg.u1ClockType = 0xFF;
            if (property_set(GPS_CLOCK_TYPE_P, "20") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x11:
            LOGD("GPS coclock, buffer 1\n");
            init_cfg.u1ClockType = 0xFE;
            if (property_set(GPS_CLOCK_TYPE_P, "11") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x21:
            LOGD("GPS coclock, buffer 2\n");
            init_cfg.u1ClockType = 0xFE;
            if (property_set(GPS_CLOCK_TYPE_P, "21") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x31:
            LOGD("GPS coclock, buffer 3\n");
            init_cfg.u1ClockType = 0xFE;
            if (property_set(GPS_CLOCK_TYPE_P, "31") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        case 0x41:
            LOGD("GPS coclock, buffer 4\n");
            init_cfg.u1ClockType = 0xFE;
            if (property_set(GPS_CLOCK_TYPE_P, "41") != 0)
                LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
            break;
        default:
            LOGE("unknown clock type, clocktype = %x\n", clock_type);
        }
    } else {
        if (strcmp(chip_id, "0x6572") == 0 || strcmp(chip_id, "0x6582") == 0
            || strcmp(chip_id, "0x6630") == 0 || strcmp(chip_id, "0x6752") == 0
            || strcmp(chip_id, "0x6632") == 0) {
            if (0xFF == init_cfg.u1ClockType) {
                LOGD("TCXO\n");
                if (property_set(GPS_CLOCK_TYPE_P, "90") != 0) {
                    LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
                }
            } else if (0xFE == init_cfg.u1ClockType) {
                LOGD("GPS coclock\n");
                if (property_set(GPS_CLOCK_TYPE_P, "91") != 0) {
                    LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
                }
            } else {
                LOGD("GPS unknown clock\n");
            }
        }
        /*Add clock type to display on YGPS by mtk06325 2013-12-09 end */
    }
#endif
    if (ioctl(dsp_fd, 10, NULL) == 1) {
        LOGD("clear RTC\n");
        delete_aiding_data = GPS_DELETE_TIME;
    }

    if (strcmp(chip_id, "0x6628") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6628;
        init_cfg.reservedx = MT6628_E1;
    } else if (strcmp(chip_id, "0x6630") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6630;
        if (ioctl(dsp_fd, COMBO_IOC_GPS_IC_HW_VERSION, &hw_ver) < 0) {
            LOGD("get COMBO_IOC_GPS_IC_HW_VERSION failed\n");
            return MTK_GPS_ERROR;
        }

        if (ioctl(dsp_fd, COMBO_IOC_GPS_IC_FW_VERSION, &fw_ver) < 0) {
            LOGD("get COMBO_IOC_GPS_IC_FW_VERSION failed\n");
            return MTK_GPS_ERROR;
        }

        if ((hw_ver == 0x8A00) && (fw_ver == 0x8A00)) {
            LOGD("MT6630_E1\n");
            init_cfg.reservedx = MT6630_E1;
        } else if ((hw_ver == 0x8A10) && (fw_ver == 0x8A10)) {
            LOGD("MT6630_E2\n");
            init_cfg.reservedx = MT6630_E2;
        } else if ((hw_ver >= 0x8A11) && (fw_ver >= 0x8A11)) {
            LOGD("MT6630 chip dection done,hw_ver = %d and fw_ver = %d\n", hw_ver, fw_ver);
            init_cfg.reservedx = MT6630_E2;  /*mnl match E1 or not E1,so we send MT6630_E2 to mnl */
        } else {
            LOGD("hw_ver = %d and fw_ver = %d\n", hw_ver, fw_ver);
            init_cfg.reservedx = MT6630_E2; /*default value*/
        }
    } else if (strcmp(chip_id, "0x6572") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6572;
        init_cfg.reservedx = MT6572_E1;
    } else if (strcmp(chip_id, "0x6570") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6570;
        init_cfg.reservedx = MT6570_E1;
    } else if (strcmp(chip_id, "0x6571") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6571;
        init_cfg.reservedx = MT6571_E1;
    } else if (strcmp(chip_id, "0x8127") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6571;
        init_cfg.reservedx = MT6571_E1;
    } else if (strcmp(chip_id, "0x6582") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6582;
        init_cfg.reservedx = MT6582_E1;
    } else if (strcmp(chip_id, "0x6592") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6592;
        init_cfg.reservedx = MT6592_E1;
    } else if (strcmp(chip_id, "0x3332") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT3332;
        init_cfg.reservedx = MT3332_E2;
    } else if (strcmp(chip_id, "0x6752") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6752;
        init_cfg.reservedx = MT6752_E1;
    } else if (strcmp(chip_id, "0x8163") == 0) {
        mnl_config.GNSSOPMode = 3;  // gps only
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6735M;
        init_cfg.reservedx = MT6735M_E1;
    } else if (strcmp(chip_id, "0x6580") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6580;
        init_cfg.reservedx = MT6580_E1;
    } else if (strcmp(chip_id, "0x0321") == 0) {  // Denali1
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6735;
        init_cfg.reservedx = MT6735_E1;

        gnss_mode_flag = ioctl(dsp_fd, 9, NULL);  //  32'h10206198 value is 01
        LOGD("gnss_mode_flag=%d \n", gnss_mode_flag);

        if (((gnss_mode_flag & 0x01000000) != 0) && ((gnss_mode_flag & 0x02000000) == 0)) {
            mnl_config.GNSSOPMode = 3;  //  gps only
        }
    } else if (strcmp(chip_id, "0x0335") == 0) {   // Denali2
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6735M;
        init_cfg.reservedx = MT6735M_E1;
    } else if (strcmp(chip_id, "0x0337") == 0) {    // Denali3
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6753;
        init_cfg.reservedx = MT6753_E1;
    } else if (strcmp(chip_id, "0x6739") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6739;
        init_cfg.reservedx = MT6739_E1;
    } else if (strcmp(chip_id, "0x6755") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6755;
        init_cfg.reservedx = MT6755_E1;

        gnss_mode_flag = ioctl(dsp_fd, 9, NULL);  // 32'h10206048 value is 01
        LOGD("gnss_mode_flag=%d \n", gnss_mode_flag);

        if (((gnss_mode_flag & 0x01000000) != 0) && ((gnss_mode_flag & 0x02000000) == 0)) {
            mnl_config.GNSSOPMode = 3;  // gps only
        }
    } else if (strcmp(chip_id, "0x6763") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6763;
        init_cfg.reservedx = MT6763_E1;
    } else if (strcmp(chip_id, "0x6797") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6797;
        init_cfg.reservedx = MT6797_E1;
    } else if (strcmp(chip_id, "0x6757") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6757;
        init_cfg.reservedx = MT6757_E1;

        gnss_mode_flag = ioctl(dsp_fd, 9, NULL);  // 32'h10206048 value is 01
        LOGD("gnss_mode_flag=%d \n", gnss_mode_flag);

        if (((gnss_mode_flag & 0x01000000) != 0) && ((gnss_mode_flag & 0x02000000) == 0)) {
            mnl_config.GNSSOPMode = 3;  // gps only
        }
    } else if (strcmp(chip_id, "0x6758") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6758;
        init_cfg.reservedx = MT6758_E1;
    } else if (strcmp(chip_id, "0x6759") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6759;
        init_cfg.reservedx = MT6759_E1;
    } else if (strcmp(chip_id, "0x6771") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6771;
        init_cfg.reservedx = MT6771_E1;
    } else if (strcmp(chip_id, "0x6775") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6775;
        init_cfg.reservedx = MT6775_E1;
    } else if (strcmp(chip_id, "0x6632") == 0) {
        init_cfg.reservedy = (void *)MTK_GPS_CHIP_KEY_MT6632;
        if (ioctl(dsp_fd, COMBO_IOC_GPS_IC_HW_VERSION, &hw_ver) < 0) {
            LOGD("get COMBO_IOC_GPS_IC_HW_VERSION failed\n");
            return MTK_GPS_ERROR;
        }
        if (ioctl(dsp_fd, COMBO_IOC_GPS_IC_FW_VERSION, &fw_ver) < 0) {
            LOGD("get COMBO_IOC_GPS_IC_FW_VERSION failed\n");
            return MTK_GPS_ERROR;
        }

        if ((hw_ver == 0x8A00) && (fw_ver == 0x8A00)) {
            LOGD("MT6632_E1\n");
            init_cfg.reservedx = MT6632_E1;
        } else if ((hw_ver >= 0x8A10) && (fw_ver >= 0x8A10)) {
            LOGD("MT6632 chip dection done, hw_ver = %d and fw_ver = %d\n", hw_ver, fw_ver);
            init_cfg.reservedx = MT6632_E3;
        } else {
            LOGD("hw_ver = %d and fw_ver = %d\n", hw_ver, fw_ver);
            init_cfg.reservedx = MT6632_E3; /*default value*/
            mnl_config.GNSSOPMode = 6;
        }
    } else {
        LOGE("chip is unknown, chip id is %s\n", chip_id);
    }

    LOGD("Get chip version type (%p) \n", init_cfg.reservedy);
    LOGD("Get chip version value (%d) \n", init_cfg.reservedx);
    if (gps_epo_type == 0) {
        if (((mnl_config.GNSSOPMode & 0x000f) != 0) && ((mnl_config.GNSSOPMode & 0x000f) != 2)) {
            gps_epo_type = 1;
        }
    }

    if (mnl_config.ACCURACY_SNR == 1) {
        init_cfg.reservedx |=(UINT32)0x80000000;
    } else if (mnl_config.ACCURACY_SNR == 2) {
        init_cfg.reservedx |=(UINT32)0x40000000;
    } else if (mnl_config.ACCURACY_SNR == 3) {
        init_cfg.reservedx |=(UINT32)0xC0000000;
    }
    init_cfg.mtk_gps_version_mode = MTK_GPS_AOSP_MODE;
    //LOGD("mtk_gps_version_mode = %d\n", init_cfg.mtk_gps_version_mode);

    mnl_config.GNSSOPMode |= 0x0300;  // default AGps On, AGlonass On, ABeidou Off
    if ((sv_type_agps_set | 0xef) == 0xef) {  // locationEM2 AGlonass button off
        mnl_config.GNSSOPMode &= ~(0x0200);
    }
    if ((sv_type_agps_set & 0x40) == 0x40) {   // locationEM2 ABeidou button on
        mnl_config.GNSSOPMode |= 0x0400;
    } else {
        mnl_config.GNSSOPMode &= ~(0x0400);
    }
    init_cfg.GNSSOPMode = mnl_config.GNSSOPMode;
    //LOGD("GNSSOPMode: 0x%x\n", init_cfg.GNSSOPMode);

    MNLD_STRNCPY(driver_cfg.nv_file_name, NV_FILE,sizeof(driver_cfg.nv_file_name));
    MNLD_STRNCPY(driver_cfg.ofl_nv_file_name, OFL_NV_FILE ,sizeof(driver_cfg.ofl_nv_file_name));
    // strcpy(driver_cfg.dbg_file_name, LOG_FILE);
    MNLD_STRNCPY(driver_cfg.nmeain_port_name, mnl_config.dev_dbg ,sizeof(driver_cfg.nmeain_port_name));
    MNLD_STRNCPY(driver_cfg.nmea_port_name, mnl_config.dev_gps ,sizeof(driver_cfg.nmea_port_name));
    MNLD_STRNCPY(driver_cfg.dsp_port_name, mnl_config.dev_dsp ,sizeof(driver_cfg.dsp_port_name));
    MNLD_STRNCPY((char *)driver_cfg.bee_path_name, mnl_config.bee_path ,sizeof(driver_cfg.bee_path_name));
    driver_cfg.reserved   =   mnl_config.BEE_enabled;

    gps_debuglog_state = gps_debuglog_state | mnl_config.dbg2file;
     // driver_cfg.DebugType: 0x01-> libmnl write file;0x11 -> libmnl write file.
    driver_cfg.DebugType    =   gps_debuglog_state;
    MNLD_STRNCPY(driver_cfg.dbg_file_name, gps_debuglog_file_name, GPS_DEBUG_LOG_FILE_NAME_MAX_LEN);

    driver_cfg.u1AgpsMachine = mnl_config.u1AgpsMachine;
    MNLD_STRNCPY((char *)driver_cfg.epo_file_name, mnl_config.epo_file ,sizeof(driver_cfg.epo_file_name));
    MNLD_STRNCPY((char *)driver_cfg.epo_update_file_name, mnl_config.epo_update_file ,sizeof(driver_cfg.epo_update_file_name));
    MNLD_STRNCPY((char *)driver_cfg.qepo_file_name, mnl_config.qepo_file ,sizeof(driver_cfg.qepo_file_name));
    MNLD_STRNCPY((char *)driver_cfg.qepo_update_file_name, mnl_config.qepo_update_file ,sizeof(driver_cfg.qepo_update_file_name));
    MNLD_STRNCPY((char *)driver_cfg.qepo_bd_file_name, QEPO_BD_FILE,sizeof(driver_cfg.qepo_bd_file_name));
    MNLD_STRNCPY((char *)driver_cfg.qepo_bd_update_file_name, QEPO_BD_UPDATE_FILE ,sizeof(driver_cfg.qepo_bd_update_file_name));
    MNLD_STRNCPY((char *)driver_cfg.mtknav_file_name, MTKNAV_DAT_FILE ,sizeof(driver_cfg.mtknav_file_name));
    MNLD_STRNCPY((char *)driver_cfg.mtknav_update_file_name, MTKNAV_DAT_FILE_HAL ,sizeof(driver_cfg.mtknav_update_file_name));

    driver_cfg.log_file_max_size = MAX_DBG_LOG_FILE_SIZE;
    driver_cfg.log_folder_max_size = MAX_DBG_LOG_DIR_SIZE;

    driver_cfg.u1AgpsMachine = mnl_config.u1AgpsMachine;
    if (driver_cfg.u1AgpsMachine == 1) {
        LOGD("use CRTU to test\n");
    } else {
        LOGD("use Spirent to test\n");
    }

    status = mtk_gps_delete_nv_data(assist_data_bit_map);

    LOGD("u4Bitmap= %d, init_cfg.C0 = %d,init_cfg.C1 = %d,init_cfg.initU = %d,init_cfg.lastU = %d,GNSSOPMode: 0x%x\n",
        status, init_cfg.C0, init_cfg.C1, init_cfg.initU, init_cfg.lastU, init_cfg.GNSSOPMode);
    driver_cfg.dsp_fd = dsp_fd;
    if (strcmp(chip_id, "0x6797") == 0 || strcmp(chip_id, "0x6632") == 0 ||
        strcmp(chip_id, "0x6759") == 0 || strcmp(chip_id, "0x6758") == 0 ||
        strcmp(chip_id, "0x6771") == 0 || strcmp(chip_id, "0x6775") == 0) {
        LOGD("OFFLOAD_STATUS:OFFLOAD_enabled:%d,OFFLOAD_switchMode:%d,type=%d",
            mnld_cfg.OFFLOAD_enabled,mnld_cfg.OFFLOAD_switchMode,mnld_flp_session.type);
        if (mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            gps_control_kernel_wakelock_init();
            mtk_gps_ofl_set_option(MNL_OFL_OPTION_ENALBE_OFFLOAD);
            LOGD("MNL_OFL_OPTION_ENALBE_OFFLOAD = 1");
        } else {
            mtk_gps_ofl_set_option(0);
            LOGD("MNL_OFL_OPTION_ENALBE_OFFLOAD = 0");
        }
    } else {
        mtk_gps_ofl_set_option(0);
        LOGD("MNL_OFL_OPTION_ENALBE_OFFLOAD = 0");
    }
    gps_dbg_log_thread_init();

    if (PDN_test_enable == 1 || PDN_test_enable == 0) {
        init_cfg.Int_IMAX_Config = PDN_test_enable;
        LOGD("init_cfg.Int_IMAX_Config = %d\n", init_cfg.Int_IMAX_Config);
    } else {
        LOGD("PDN_test_enable has wrong number\n");
        init_cfg.Int_IMAX_Config = 0;
    }

    mnl_status = mtk_gps_mnl_run((const MTK_GPS_INIT_CFG*)&init_cfg , (const MTK_GPS_DRIVER_CFG*)&driver_cfg);
    LOGD("Status (%d) \n", mnl_status);
    if (mnl_status != MNL_INIT_SUCCESS) {
        status = MTK_GPS_ERROR;
        return status;
    }
    mnl_mpe_thread_init();

    if (access(EPO_UPDATE_HAL, F_OK) == -1) {
        LOGD("EPOHAL file does not exist, no EPO yet\n");
    } else if (mnld_is_epo_download_finished() == false) {
        LOGD("EPO is still downloading");
    } else {
        //LOGD("there is a EPOHAL file, please mnl update EPO.DAT from EPOHAL.DAT\n");
        if (mtk_agps_agent_epo_file_update() == MTK_GPS_ERROR) {
            LOGE("EPO file updates fail\n");
        } else {
            unlink(EPO_UPDATE_HAL);
        }
    }
    if (access(QEPO_UPDATE_HAL, F_OK) == -1) {
        LOGD("QEPOHAL file does not exist, no EPO yet\n");
    } else {
        //LOGD("there is a QEPOHAL file, please mnl update QEPO.DAT from QEPOHAL.DAT\n");
        if (mtk_agps_agent_qepo_file_update() == MTK_GPS_ERROR) {
            LOGE("QEPO file updates fail\n");
        }
    }
    if (access(MTKNAV_DAT_FILE_HAL, F_OK) == -1) {
        LOGD("MTKNAVHAL file does not exist, no MTKNAV yet\n");
    } else {
        //LOGD("there is a MTKNAVHAL file, please mnl update MTKNAV.DAT from MTKNAVHAL.DAT\n");
        if (mtk_agps_agent_mtknav_file_update() == MTK_GPS_ERROR) {
            LOGE("MTKNAV file updates fail\n");
        } else {
            unlink(MTKNAV_DAT_FILE_HAL);
        }
    }
    LOGD("dsp port (%s),nmea port (%s),nmea dbg port (%s),dbg_file_name (%s),DebugType (%d),nv_file_name (%s), mtk_gps_log_hide:%d\n",
        driver_cfg.dsp_port_name, driver_cfg.nmea_port_name, driver_cfg.nmeain_port_name,
        driver_cfg.dbg_file_name, driver_cfg.DebugType, driver_cfg.nv_file_name, mtk_gps_log_hide);
    if (epo_setconfig == 1) {
        userprofile.EPO_enabled = mnl_config.EPO_enabled;
    } else {
#if ANDROID_MNLD_PROP_SUPPORT
        userprofile.EPO_enabled = get_prop(7);
#else
    LOGD("Prop is not support,EPO_enabled (%d) \n", userprofile.EPO_enabled);
#endif
    }
    // userprofile.EPO_enabled = mnl_config.EPO_enabled;
    userprofile.BEE_enabled = mnl_config.BEE_enabled;
    userprofile.SUPL_enabled = mnl_config.SUPL_enabled;
    userprofile.EPO_priority = mnl_config.EPO_priority;
    userprofile.BEE_priority = mnl_config.BEE_priority;
    userprofile.SUPL_priority = mnl_config.SUPL_priority;
    userprofile.fgGpsAosp_Ver = mnl_config.fgGpsAosp_Ver;
    userprofile.LPPE_enabled = lppe_enable;
    // mtk_agps_set_param(MTK_MSG_AGPS_MSG_PROFILE, &userprofile, MTK_MOD_DISPATCHER, MTK_MOD_AGENT);
#if RAW_DATA_SUPPORT
    gps_raw_data_enable();
#endif

    unsigned int i = 0;
    INT32 ret = MTK_GPS_ERROR;
    bool alt_valid = false;
    float altitude = 0.0f;
    bool source_valid = true;
    bool source_gnss = true;
    bool source_nlp = false;
    bool source_sensor = false;
    //  if sending profile msg fail, re-try 2-times, each time sleep 10ms
    for (i = 0; i < 3; i++) {
        ret = mtk_agps_set_param(MTK_MSG_AGPS_MSG_PROFILE, &userprofile, MTK_MOD_DISPATCHER, MTK_MOD_AGENT);
        if (ret != MTK_GPS_SUCCESS) {
            LOGD("%d st send profile to agent fail. try again \n", i);
            usleep(10000);  //  sleep 10ms for init agent message queue
        } else {
            LOGD("%d st send profile to agent OK \n", i);
            break;
        }
    }

    if (mtk_gps_get_position_accuracy(&latitude, &longitude, &accuracy) == MTK_GPS_SUCCESS) {
        if (mtk_gps_log_is_hide() == 0) {
            LOGD("mnl init, mtk_gps_get_position_accuracy success, %lf, %lf, %d", latitude, longitude, accuracy);
        }
        if (accuracy < 100) {
            ret_val = mnl2agps_location_sync(latitude, longitude, accuracy, alt_valid, altitude, source_valid, source_gnss, source_nlp, source_sensor);
            if (0 == ret_val) {
                LOGD("mnl2agps_location_sync success");
            }
        }
    }
    hasAlmanac();

    ret = mtk_gps_set_param(MTK_PARAM_CMD_SIB8_16_ENABLE, &sib8_16_enable);
    LOGD("sent CMD_SIB8_16_ENABLE to mnl, sib8_16_enable = %d ,ret = %d", sib8_16_enable, ret);
    if(mtk_gps_mnl_info.support_lppe){
        ret = mtk_gps_set_param(MTK_PARAM_CMD_LPPE_ENABLE, &lppe_enable);
        LOGD("sent MTK_PARAM_CMD_LPPE_ENABLE to mnl, lppe_enable = %d ,ret = %d", lppe_enable, ret);
    }
    return  status;
}
#else
static int linux_gps_init(void) {
 	// power on gps on another place.
 	
#if ANDROID_MNLD_PROP_SUPPORT
    // strcpy(driver_cfg.mnl_chip_id, chip_id);
    if (strcmp(chip_id, "0x3333") == 0 ) {
        LOGD("TCXO\n");
        if (property_set(GPS_CLOCK_TYPE_P, "90") != 0) {
			LOGE("set GPS_CLOCK_TYPE_P %s\n", strerror(errno));
		}
    }
#endif

    gps_dbg_log_thread_init();
#if RAW_DATA_SUPPORT
    gps_raw_data_enable();
#endif
	
    hasAlmanac();

    return  0;
}
#endif

/*****************************************************************************/
static int mnld_gps_stop_impl(void) {
    int ret = 0;
    LOGD("MNL exiting \n");
#ifndef CONFIG_GPS_MT3333	
    mtk_gps_mnl_stop();
#endif
    LOGD("mtk_gps_mnl_stop()\n");

    if (g_gpsdbglogThreadExit == false) {
        gps_dbg_log_exit_flush(1);
    }
    if ((ret = mtk_gps_sys_uninit())) {
        LOGE("mtk_gps_sys_uninit err = %d=\n", errno);
    }
#ifndef CONFIG_GPS_MT3333	
    if (dsp_fd > 0) {
        if (mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            gps_control_kernel_wakelock_uninit();
        }
        close(dsp_fd);
        dsp_fd = -1;
    }
    // cancel alarm
    LOGD("Cancel alarm");
    alarm(0);
#endif
    return ret;
}

/*****************************************************************************/
static time_t last_send_time = 0;
static time_t current_time = 0;

int send_active_notify() {
    int gps_user = mtk_gps_get_gps_user();
    // if no other users except GPS_USER_FLP or GPS_USER_OFL_TEST, bypass restart
    if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == gps_user) && mnl_offload_is_enabled()
        && (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
        LOGE("gps_user: %d,offload: %d\n", gps_user, mnld_cfg.OFFLOAD_enabled);
        return -1;
    }
    if (!(mnl_config.debug_mnl & MNL_NMEA_DISABLE_NOTIFY)) {
        char buff[1024] = {0};
        int offset = 0;
        LOGD("send clean nmea timer cmd!\n");
        put_int(buff, &offset, GPS2GPS_NMEA_DATA_TIMEOUT);
        return safe_sendto(MNLD_GPS_CONTROL_SOCKET, buff, offset);
    }
    return -1;
}

INT32 mtk_gps_sys_gps_mnl_callback(MTK_GPS_NOTIFICATION_TYPE msg) {
    LOGD("msg:%d\n", msg);
    switch (msg) {
    case MTK_GPS_MSG_FIX_READY:
        {
            // For NI open GPS
            double dfRtcD = 0.0, dfAge = 0.0;
            send_active_notify();
#ifndef CONFIG_GPS_MT3333
            if (mtk_gps_get_rtc_info(&dfRtcD, &dfAge) == MTK_GPS_SUCCESS) {
                LOGD("MTK_GPS_MSG_FIX_READY, GET_RTC_OK, %.3lf, %.3lf\n", dfRtcD, dfAge);
                LOGD("Age = %d, RTCDiff = %d, Time_interval = %d\n", mnl_config.AVAILIABLE_AGE,
                    mnl_config.RTC_DRIFT, mnl_config.TIME_INTERVAL);
                if ((dfAge <= mnl_config.AVAILIABLE_AGE) && (dfRtcD >= mnl_config.RTC_DRIFT ||
                    dfRtcD <= -mnl_config.RTC_DRIFT) && dfRtcD < 5000) {
                    int fd_fmsta = -1;
                    unsigned char buf[2]= {0};
                    int status = -1;
                    fd_fmsta = open("/proc/fm", O_RDWR);
                    if (fd_fmsta < 0) {
                        LOGD("open /proc/fm error");
                    } else {
                        LOGD("open /proc/fm success!");
                        status = read(fd_fmsta, &buf, sizeof(buf));
                        if (status < 0)
                            LOGD("read fm status fails = %s", strerror(errno));
                        if (close(fd_fmsta) == -1)
                            LOGD("close fails = %s", strerror(errno));
                    }

                    if (buf[0] == '2') {
                        INT32 time_diff;
                        time(&current_time);
                        time_diff = current_time - last_send_time;
                        if ((0 == last_send_time) || (time_diff > mnl_config.TIME_INTERVAL)) {
                            int fd_fmdev = -1;
                            int ret = 0;
                            struct fm_gps_rtc_info rtcInfo;
                            fd_fmdev = open("dev/fm", O_RDWR);
                            if (fd_fmdev < 0) {
                                LOGD("open fm dev error\n");
                            }
                            else {
                                rtcInfo.retryCnt = 2;
                                rtcInfo.ageThd = mnl_config.AVAILIABLE_AGE;
                                rtcInfo.driftThd = mnl_config.RTC_DRIFT;
                                rtcInfo.tvThd.tv_sec = mnl_config.TIME_INTERVAL;
                                rtcInfo.age = dfAge;
                                rtcInfo.drift = dfRtcD;
                                rtcInfo.tv.tv_sec = current_time;
                                ret = ioctl(fd_fmdev, FM_IOCTL_GPS_RTC_DRIFT, &rtcInfo);
                                if (ret) {
                                    LOGD("send rtc info failed, [ret=%d]\n", ret);
                                }
                                ret = close(fd_fmdev);
                                if (ret) {
                                    LOGD("close fm dev error\n");
                                }
                            }
                        }
                    }
                }
            }
            else {
                LOGD("MTK_GPS_MSG_FIX_READY,GET_RTC_FAIL\n");
            }
     #if RAW_DATA_SUPPORT
            if (gps_raw_debug_mode && !mtk_msg_raw_meas_flag) {
                LOGD("raw_debug_mode is open, send MTK_MSG_RAW_MEAS to libmnl\n");

                INT32 ret = MTK_GPS_ERROR;
                ret = mtk_gps_set_param(MTK_MSG_RAW_MEAS, NULL);
                LOGD("mtk_gps_set_param,ret = %d\n", ret);
                if (ret != MTK_GPS_SUCCESS) {
                    LOGE("send MTK_MSG_RAW_MEASto mnl fail,please reopen gps\n");
                } else {
                    LOGD("send MTK_MSG_RAW_MEAS to mnl OK \n");
                    mtk_msg_raw_meas_flag = 1;  // Don't send MTK_MSG_RAW_MEAS when it was sent to mnl successfully
                }
            }

            /*get gps measurement and clock data*/
            if (mnld_is_gps_meas_enabled() && mnld_is_gps_started_done()) {
                get_gps_measurement_clock_data();
                get_gnss_measurement_clock_data();
                LOGD("gps_meas_enable");
            }

            /*get gps navigation event */
            if (mnld_is_gps_navi_enabled() && mnld_is_gps_started_done()) {
                LOGD("gps_navi_enable");
                get_gps_navigation_event();
                get_gnss_navigation_event();
            }
    #endif
            if (is_flp_user_exist() && !(mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
                UINT8 buff[1024] = {0};
                UINT8 playload[OFFLOAD_PAYLOAD_LEN] = {0};
                UINT32 p_get_len = 0;
                //MTK_FLP_OFFLOAD_MSG_T *prMsg = (MTK_FLP_OFFLOAD_MSG_T *)&buff[0];
                MTK_FLP_MNL_MSG_T *prMsg = (MTK_FLP_MNL_MSG_T *)&buff[0];
                mtk_gps_flp_get_location(playload, OFFLOAD_PAYLOAD_LEN, &p_get_len);
                //prMsg->type = FLPMNL_GPS_REPORT_LOC;
                prMsg->type = MNLD_FLP_TYPE_HBD_GPS_LOCATION;
                prMsg->length = p_get_len;
                prMsg->session = mnld_flp_session.id;
                memcpy(&prMsg->data[0], playload, p_get_len);
                if (-1 == mnl2flp_data_send(buff, sizeof(buff))) {
                    LOGE("[FLP2MNLD]Send to FLP failed, %s\n", strerror(errno));
                } else {
                    //LOGD("[FLP2MNLD]Send to FLP successfully\n");
                }
            }
            if (is_geofence_user_exist() && !(mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
                UINT8 buff[1024] = {0};
                UINT8 playload[OFFLOAD_PAYLOAD_LEN] = {0};
                UINT32 p_get_len = 0;
                //MTK_FLP_OFFLOAD_MSG_T *prMsg = (MTK_FLP_OFFLOAD_MSG_T *)&buff[0];
                MTK_GFC_MNL_MSG_T *prMsg = (MTK_GFC_MNL_MSG_T *)&buff[0];
                mtk_gps_flp_get_location(playload, OFFLOAD_PAYLOAD_LEN, &p_get_len);
                //prMsg->type = FLPMNL_GPS_REPORT_LOC;
                prMsg->type = MNLD_FLP_TYPE_HBD_GPS_LOCATION;
                prMsg->length = p_get_len;
                prMsg->session = mnld_gfc_session.id;
                memcpy(&prMsg->data[0], playload, p_get_len);
                if (-1 == mnl2gfc_data_send(buff, sizeof(buff))) {
                    LOGE("[GFC2MNLD]Send to GFC failed, %s\n", strerror(errno));
                } else {
                    //LOGD("[GFC2MNLD]Send to GFC successfully\n");
                }
            }

#else
	 #if RAW_DATA_SUPPORT
			unsigned char state = MNL_GPS_STATE_UNSUPPORTED;
			int err = mnl_get_state(&state);
			if ((err) || (state >= MNL_GPS_STATE_MAX)) {
				LOGE("mnl_get_state() = %d, %d\n", err, state);
			}

			/*get gps measurement and clock data*/
			if (mnld_is_gps_meas_enabled() && state == MNL_GPS_STATE_START) {
				get_gps_measurement_clock_data();
				get_gnss_measurement_clock_data();
				LOGD("gps_meas_enable, mnl state = %d", state);
			}

			/*get gps navigation event */
			if (mnld_is_gps_navi_enabled() && state == MNL_GPS_STATE_START) {
				LOGD("gps_navi_enable, mnl state = %d", state);
				get_gps_navigation_event();
				get_gnss_navigation_event();
			}
	#endif
#endif			
        }
        break;
    case MTK_GPS_MSG_FIX_PROHIBITED:
        {
            send_active_notify();
            LOGD("MTK_GPS_MSG_FIX_PROHIBITED\n");
        }
        break;
    default:
        break;
    }
    return  MTK_GPS_SUCCESS;
}

/*****************************************************************************/
/*Agps dispatcher state mode*/
typedef enum {
    ST_SI,
    ST_NI,
    ST_IDLE,
    ST_UNKNOWN,
}MTK_AGPS_DISPATCH_STATE;

MTK_AGPS_DISPATCH_STATE state_mode = ST_IDLE;
MTK_AGPS_DISPATCH_STATE last_state_mode = ST_UNKNOWN;
int AGENT_SET_ALARM = 0;
static void alarm_handler() {
    if (AGENT_SET_ALARM == 1) {
        if ((mtk_gps_get_gps_user()& GPS_USER_AGPS) != 0) {
            // SI alarm handler: ignore, as current user is AGPS, it may in NI session
            // NI alarm handler: ignore, as AGPSD send close_gps_req(timer be canceled)
            return;
        }
        if (mnld_is_gps_stopped()) {
            LOGD("GPS driver is stopped");
            AGENT_SET_ALARM = 0;
            last_state_mode = state_mode;
            state_mode = ST_IDLE;
            return;
        }
        // SI only session and GPS driver is running
        LOGD("Send MTK_AGPS_SUPL_END to MNL");
        int ret = mtk_agps_set_param(MTK_MSG_AGPS_MSG_SUPL_TERMINATE, NULL, MTK_MOD_DISPATCHER, MTK_MOD_AGENT);
        last_state_mode = state_mode;
        state_mode = ST_IDLE;
        LOGD("Receive MTK_AGPS_SUPL_END , last_state_mode = %d, state_mode = %d", last_state_mode, state_mode);
    }
    AGENT_SET_ALARM = 0;
}

void gps_controller_agps_session_done() {
    LOGD("agps_session_done");
    if (mnld_is_gps_stopped()) {
        LOGD("GPS driver is stopped");
        AGENT_SET_ALARM = 0;
        last_state_mode = state_mode;
        state_mode = ST_IDLE;
        return;
    }
    if (0 == AGENT_SET_ALARM) {
        LOGD("Set up signal & alarm!");
        signal(SIGALRM, alarm_handler);
        alarm(30);
        AGENT_SET_ALARM = 1;
    } else {
        LOGD("AGENT_SET_ALARM == 1");
    }
}
void gps_controller_rcv_pmtk(const char* pmtk) {
    // LOGD("rcv_pmtk: %s", pmtk);
    if (mnld_is_gps_stopped()) {
        LOGD("rcv_pmtk: MNL stopped, return");
        return;
    }
    int ret = mtk_agps_set_param(MTK_MSG_AGPS_MSG_SUPL_PMTK, pmtk, MTK_MOD_DISPATCHER, MTK_MOD_AGENT);
    if (ret != 0) {
        LOGD("mtk_agps_set_param fail, MTK_MSG_AGPS_MSG_SUPL_PMTK\n");
    }
}

INT32 mtk_gps_sys_agps_disaptcher_callback(UINT16 type, UINT16 length, char *data) {
    INT32 ret = MTK_GPS_SUCCESS;

    if (type == MTK_AGPS_CB_SUPL_PMTK || type == MTK_AGPS_CB_ASSIST_REQ || \
        type == MTK_AGPS_CB_START_REQ || type == MTK_AGPS_CB_LPPE_ASSIST_REQ) {
        if (mnl_config.SUPL_enabled) {
            if (type == MTK_AGPS_CB_SUPL_PMTK) {
                if (length != 0)
                  mnl2agps_pmtk(data);
                return 0;
            } else if (type == MTK_AGPS_CB_ASSIST_REQ) {
                if (state_mode == ST_IDLE || state_mode == ST_SI) {
                    LOGD("GPS re-aiding\n");
                    mnl2agps_reaiding_req();
                    return 0;
                } else {
                    LOGE("Dispatcher in %d mode, ignore current request\n", state_mode);
                    return MTK_GPS_ERROR;
                }
            }else if (type == MTK_AGPS_CB_LPPE_ASSIST_REQ) {
                if (length != 0) {
                    mnl2agps_lppe_assist_data_req(data,length);
                    LOGD("request lppe data\n");
                }
                return 0;
            } else if ((type == MTK_AGPS_CB_START_REQ) && (data != NULL)) {
                LOGD("MNL ready and assist req:%d", *data);
                int assist_req;
                if (*data == 1) {
                    //LOGD("Agent assist request");
                    assist_req = 1;
                } else if (*data == 0) {
                    //LOGD("Agent no assist request");
                    assist_req = 0;
                } else {
                    LOGD("unknown data");
                    assist_req = 0;
                }
                // mnl2agps_gps_open(assist_req);
                if (gps_restart == 1) {
                    release_condition(&lock_for_sync[M_RESTART]);
                    LOGD("release condition for restart");
                }
                mnld_gps_start_done(assist_req);
                return ret;
            }

        } else {
            LOGD("mtk_sys_agps_disaptcher_callback: SUPL disable");
            ret = MTK_GPS_ERROR;
        }
    }  else if ((type == MTK_AGPS_CB_BITMAP_UPDATE) && (data != NULL)) {
        LOGD("MNL NTP/NLP request:%d", *data);
        if ((*data & 0x01) == 0x01) {
            LOGD("Call request utc time request");
            mnl2hal_request_utc_time();
        }
        if ((*data & 0x02) == 0x02) {
            LOGD("Call nlp_server request");
            mnld_gps_request_nlp(NLP_REQUEST_SRC_MNL);
        }
    } else if (type == MTK_AGPS_CB_QEPO_DOWNLOAD_REQ) {
        UINT16 wn;
        UINT32 tow;
        UINT32 sys_time;
        char dl_bitmap = 0;
    #ifdef QEPO_BD
        if(data != NULL)
        {
            dl_bitmap = *data;
            ret = mtk_agps_agent_qepo_get_rqst_time(&wn, &tow, &sys_time);
            LOGD("wn, tow, sys_time = %d, %d, %d,bitmap:0x%x\n", wn, tow, sys_time, *data);
            gps_mnl_set_gps_time(wn, tow, sys_time);
            if (( dl_bitmap & (AGT_QEPO_GP_BIT|AGT_QEPO_GL_BIT)) != 0) {
                qepo_downloader_start();
            }

            if ((dl_bitmap & AGT_QEPO_BD_BIT) == AGT_QEPO_BD_BIT) {
                qepo_bd_downloader_start();
            }
        }else{
            LOGE("QEPO dl request , pointer of data is null!!!\n");
        }
    #else
		#ifndef CONFIG_GPS_MT3333
        ret = mtk_agps_agent_qepo_get_rqst_time(&wn, &tow, &sys_time);
		#else
		ret = mt3333_controller_Utc2GpsTime(&wn, &tow, &sys_time);
		#endif
        LOGD("wn, tow, sys_time = %d, %d, %d\n", wn, tow, sys_time);
        gps_mnl_set_gps_time(wn, tow, sys_time);
        qepo_downloader_start();
    #endif	
    }else if (type == MTK_AGPS_CB_MTKNAV_DOWNLOAD_REQ) {
        if(mtknav_downloader_start() == -1){
            LOGW("mtknav downloader start fail");
        }
    }
    return ret;
}

#if RAW_DATA_SUPPORT
int get_val(char *pStr, char** ppKey, char** ppVal) {
    int len = (int)strlen(pStr);
    char *end = pStr + len;
    char *key = NULL, *val = NULL;
    int stage = 0;

    LOGD("pStr = %s,len=%d!!\n", pStr, len);

    if (!len) {
        return -1;    // no data
    } else if (pStr[0] == '#') {   /*ignore comment*/
        *ppKey = *ppVal = NULL;
        return 0;
    } else if (pStr[len-1] != '\n') {
        if (len >= GPS_CONF_FILE_SIZE-1) {
            LOGD("buffer is not enough!!\n");
            return -1;
        } else {
            pStr[len] = '\n';
        }
    }
    key = pStr;

    LOGD("key = %s!!\n", key);
    while ((*pStr != '=') && (pStr < end)) pStr++;
    if (pStr >= end) {
        LOGD("'=' is not found!!\n");
        return -1;    // format error
    }

    *pStr++ = '\0';
    while (IS_SPACE(*pStr) && (pStr < end)) pStr++;    // skip space chars
    val = pStr;
    while (!IS_SPACE(*pStr) && (pStr < end)) pStr++;
    *pStr = '\0';
    *ppKey = key;
    *ppVal = val;

    LOGD("val = %s!!\n", val);
    return 0;
}

static int gps_raw_data_enable(void) {
    char result[GPS_CONF_FILE_SIZE] = {0};

    FILE *fp = fopen(RAW_DATA_CONTROL_FILE_PATH, "r");
    char *key, *val;
    if (!fp) {
        LOGD("%s: open %s fail!\n", __FUNCTION__, RAW_DATA_CONTROL_FILE_PATH);
        return 1;
    }

    while (fgets(result, sizeof(result), fp)) {
        if (get_val(result, &key, &val)) {
            LOGD("%s: Get data fails!!\n", __FUNCTION__);
            fclose(fp);
            return 1;
        }
        if (!key || !val) {
            continue;
        }
        if (!strcmp(key, "RAW_DEBUG_MODE")) {
            int len = strlen(val);
            gps_raw_debug_mode = str2int(val, val+len);  // *val-'0';
            if ((gps_raw_debug_mode != 1) && (gps_raw_debug_mode != 0)) {
                gps_raw_debug_mode = 0;
            }
            LOGD("gps_raw_debug_mode = %d\n", gps_raw_debug_mode);
        }
    }
    fclose(fp);
    return gps_raw_debug_mode;
}

void print_gps_measurement(gps_measurement in) {
    LOGD("===== print_gps_measurement ====");
    LOGD("flags=0x%x", in.flags);
    LOGD("prn=%d", in.prn);
    LOGD("time_offset_ns=%f", in.time_offset_ns);
    LOGD("state=0x%x", in.state);
    LOGD("received_gps_tow_ns=%lld", in.received_gps_tow_ns);
    LOGD("received_gps_tow_uncertainty_ns=%lld", in.received_gps_tow_uncertainty_ns);
    LOGD("c_n0_dbhz=%f", in.c_n0_dbhz);
    LOGD("pseudorange_rate_mps=%f", in.pseudorange_rate_mps);
    LOGD("pseudorange_rate_uncertainty_mps=%f", in.pseudorange_rate_uncertainty_mps);
    LOGD("accumulated_delta_range_state=0x%x", in.accumulated_delta_range_state);
    LOGD("accumulated_delta_range_m=%f", in.accumulated_delta_range_m);
    LOGD("accumulated_delta_range_uncertainty_m=%f", in.accumulated_delta_range_uncertainty_m);
    LOGD("pseudorange_m=%f", in.pseudorange_m);
    LOGD("pseudorange_uncertainty_m=%f", in.pseudorange_uncertainty_m);
    LOGD("code_phase_chips=%f", in.code_phase_chips);
    LOGD("code_phase_uncertainty_chips=%f", in.code_phase_uncertainty_chips);
    LOGD("carrier_frequency_hz=%f", in.carrier_frequency_hz);
    LOGD("carrier_cycles=%lld", in.carrier_cycles);
    LOGD("carrier_phase=%f", in.carrier_phase);
    LOGD("carrier_phase_uncertainty=%f", in.carrier_phase_uncertainty);
    LOGD("loss_of_lock=%d", in.loss_of_lock);
    LOGD("bit_number=%d", in.bit_number);
    LOGD("time_from_last_bit_ms=%d", in.time_from_last_bit_ms);
    LOGD("doppler_shift_hz=%f", in.doppler_shift_hz);
    LOGD("doppler_shift_uncertainty_hz=%f", in.doppler_shift_uncertainty_hz);
    LOGD("multipath_indicator=%d", in.multipath_indicator);
    LOGD("snr_db=%f", in.snr_db);
    LOGD("elevation_deg=%f", in.elevation_deg);
    LOGD("elevation_uncertainty_deg=%f", in.elevation_uncertainty_deg);
    LOGD("azimuth_deg=%f", in.azimuth_deg);
    LOGD("azimuth_uncertainty_deg=%f", in.azimuth_uncertainty_deg);
    LOGD("used_in_fix=%d", in.used_in_fix);
}

void print_gps_clock(gps_clock in) {
    LOGD("===== print_gps_clock ====");
    LOGD("flags=0x%x", in.flags);
    LOGD("leap_second=%d", in.leap_second);
    LOGD("type=%d", in.type);
    LOGD("time_ns=%lld", in.time_ns);
    LOGD("time_uncertainty_ns=%f", in.time_uncertainty_ns);
    LOGD("full_bias_ns=%lld", in.full_bias_ns);
    LOGD("bias_ns=%f", in.bias_ns);
    LOGD("bias_uncertainty_ns=%f", in.bias_uncertainty_ns);
    LOGD("drift_nsps=%f", in.drift_nsps);
    LOGD("drift_uncertainty_nsps=%f", in.drift_uncertainty_nsps);
}

void print_gps_nav_msg(gps_nav_msg in) {
    LOGD("===== print_gps_nav_msg ====");
    LOGD("prn=%d", in.prn);
    LOGD("type=%d", in.type);
    LOGD("status=0x%x", in.status);
    LOGD("message_id=%d", in.message_id);
    LOGD("submessage_id=%d", in.submessage_id);
    LOGD("data_length=%d", in.data_length);
}

void print_gnss_measurement(gnss_measurement in) {
    LOGD("===== print_gnss_measurement ====");
    LOGD("flags=0x%x", in.flags);
    LOGD("svid=%d", in.svid);
    LOGD("constellation=0x%x", in.constellation);
    LOGD("time_offset_ns=%f", in.time_offset_ns);
    LOGD("state=0x%x", in.state);
    LOGD("received_gps_tow_ns=%lld", in.received_sv_time_in_ns);
    LOGD("received_gps_tow_uncertainty_ns=%lld", in.received_sv_time_uncertainty_in_ns);
    LOGD("c_n0_dbhz=%f", in.c_n0_dbhz);
    LOGD("pseudorange_rate_mps=%f", in.pseudorange_rate_mps);
    LOGD("pseudorange_rate_uncertainty_mps=%f", in.pseudorange_rate_uncertainty_mps);
    LOGD("accumulated_delta_range_state=0x%x", in.accumulated_delta_range_state);
    LOGD("accumulated_delta_range_m=%f", in.accumulated_delta_range_m);
    LOGD("accumulated_delta_range_uncertainty_m=%f", in.accumulated_delta_range_uncertainty_m);;
    LOGD("carrier_frequency_hz=%f", in.carrier_frequency_hz);
    LOGD("carrier_cycles=%lld", in.carrier_cycles);
    LOGD("carrier_phase=%f", in.carrier_phase);
    LOGD("carrier_phase_uncertainty=%f", in.carrier_phase_uncertainty);
    LOGD("multipath_indicator=%d", in.multipath_indicator);
    LOGD("snr_db=%f", in.snr_db);
}

void print_gnss_clock(gnss_clock in) {
    LOGD("===== print_gnss_clock ====");
    LOGD("flags=0x%x", in.flags);
    LOGD("leap_second=%d", in.leap_second);
    LOGD("time_ns=%lld", in.time_ns);
    LOGD("time_uncertainty_ns=%f", in.time_uncertainty_ns);
    LOGD("full_bias_ns=%lld", in.full_bias_ns);
    LOGD("bias_ns=%f", in.bias_ns);
    LOGD("bias_uncertainty_ns=%f", in.bias_uncertainty_ns);
    LOGD("drift_nsps=%f", in.drift_nsps);
    LOGD("drift_uncertainty_nsps=%f", in.drift_uncertainty_nsps);
    LOGD("hw_clock_discontinuity_count=%d", in.hw_clock_discontinuity_count);
}

void print_gnss_nav_msg(gnss_nav_msg in) {
    LOGD("===== print_gnss_nav_msg ====");
    LOGD("svid=%d", in.svid);
    LOGD("type=%d", in.type);
    LOGD("status=0x%x", in.status);
    LOGD("message_id=%d", in.message_id);
    LOGD("submessage_id=%d", in.submessage_id);
    LOGD("data_length=%d", in.data_length);
}

#ifndef CONFIG_GPS_MT3333
static void get_gps_measurement_clock_data() {
    LOGD("get_gps_measurement_clock_data begin");

    int i;
    int num = 0;
    int ret;
    gps_data gpsdata;
    MTK_GPS_MEASUREMENT mtk_gps_measurement[GPS_MAX_MEASUREMENT];
    INT8 ch_proc_ord_prn[GPS_MAX_MEASUREMENT]={0};
    mtk_gps_get_measurement(mtk_gps_measurement, ch_proc_ord_prn);
    LOGD("sizeof(mtk_gps_get_measurement) = %d,sizeof(mtk_gps_get_measurement[0]) = %d\n",
        sizeof(mtk_gps_measurement), sizeof(mtk_gps_measurement[0]));

    MTK_GPS_CLOCK mtk_gps_clock;
    ret = mtk_gps_get_clock(&mtk_gps_clock);
    if (ret == 0) {
        LOGD("mtk_gps_get_clock fail,[ret=%d]\n", ret);
    }

    gps_clock gpsclock;
    memset(&gpsclock, 0, sizeof(gps_clock));
    // gpsclock.size = sizeof(gps_clock);
    gpsclock.bias_ns = mtk_gps_clock.BiasInNs;
    gpsclock.bias_uncertainty_ns = mtk_gps_clock.BiasUncertaintyInNs;
    gpsclock.drift_nsps = mtk_gps_clock.DriftInNsPerSec;
    gpsclock.flags = mtk_gps_clock.flag;
    gpsclock.leap_second = mtk_gps_clock.leapsecond;
    gpsclock.time_ns = mtk_gps_clock.TimeInNs;
    gpsclock.time_uncertainty_ns = mtk_gps_clock.TimeUncertaintyInNs;
    gpsclock.type = mtk_gps_clock.type;
    gpsclock.full_bias_ns = mtk_gps_clock.FullBiasInNs;
    gpsclock.drift_uncertainty_nsps = mtk_gps_clock.DriftUncertaintyInNsPerSec;
    if (gps_raw_debug_mode) {
        print_gps_clock(gpsclock);
    }

    memset(&gpsdata, 0, sizeof(gps_data));
    // gpsdata.size = sizeof(gps_data);
    for (i = 0; i < GPS_MAX_MEASUREMENT; i++) {
        if (mtk_gps_measurement[i].PRN != 0) {
            num = gpsdata.measurement_count;

            // gpsdata.measurements[num].size = sizeof(gps_measurement);
            gpsdata.measurements[num].accumulated_delta_range_m = mtk_gps_measurement[i].AcDRInMeters;
            gpsdata.measurements[num].accumulated_delta_range_state = mtk_gps_measurement[i].AcDRState10;
            gpsdata.measurements[num].accumulated_delta_range_uncertainty_m = \
            mtk_gps_measurement[i].AcDRUnInMeters;
            gpsdata.measurements[num].azimuth_deg = mtk_gps_measurement[i].AzInDeg;
            gpsdata.measurements[num].azimuth_uncertainty_deg = mtk_gps_measurement[i].AzUnInDeg;
            gpsdata.measurements[num].bit_number = mtk_gps_measurement[i].BitNumber;
            gpsdata.measurements[num].carrier_cycles = mtk_gps_measurement[i].CarrierCycle;
            gpsdata.measurements[num].carrier_phase = mtk_gps_measurement[i].CarrierPhase;
            gpsdata.measurements[num].carrier_phase_uncertainty = mtk_gps_measurement[i].CarrierPhaseUn;
            gpsdata.measurements[num].carrier_frequency_hz = mtk_gps_measurement[i].CFInhZ;
            gpsdata.measurements[num].c_n0_dbhz = mtk_gps_measurement[i].Cn0InDbHz;
            gpsdata.measurements[num].code_phase_chips = mtk_gps_measurement[i].CPInChips;
            gpsdata.measurements[num].code_phase_uncertainty_chips = mtk_gps_measurement[i].CPUnInChips;
            gpsdata.measurements[num].doppler_shift_hz = mtk_gps_measurement[i].DopperShiftInHz;
            gpsdata.measurements[num].doppler_shift_uncertainty_hz = mtk_gps_measurement[i].DopperShiftUnInHz;
            gpsdata.measurements[num].elevation_deg = mtk_gps_measurement[i].ElInDeg;
            gpsdata.measurements[num].elevation_uncertainty_deg = mtk_gps_measurement[i].ElUnInDeg;
            gpsdata.measurements[num].flags = mtk_gps_measurement[i].flag;
            gpsdata.measurements[num].loss_of_lock = mtk_gps_measurement[i].LossOfLock;
            gpsdata.measurements[num].multipath_indicator = mtk_gps_measurement[i].MultipathIndicater;
            gpsdata.measurements[num].pseudorange_m = mtk_gps_measurement[i].PRInMeters;
            gpsdata.measurements[num].prn = mtk_gps_measurement[i].PRN;
            gpsdata.measurements[num].pseudorange_rate_mps = mtk_gps_measurement[i].PRRateInMeterPreSec;
            gpsdata.measurements[num].pseudorange_rate_uncertainty_mps = \
            mtk_gps_measurement[i].PRRateUnInMeterPreSec;
            gpsdata.measurements[num].pseudorange_uncertainty_m = mtk_gps_measurement[i].PRUnInMeters;
            gpsdata.measurements[num].received_gps_tow_ns = mtk_gps_measurement[i].ReGpsTowInNs;
            gpsdata.measurements[num].received_gps_tow_uncertainty_ns = mtk_gps_measurement[i].ReGpsTowUnInNs;
            gpsdata.measurements[num].snr_db = mtk_gps_measurement[i].SnrInDb;
            gpsdata.measurements[num].state = mtk_gps_measurement[i].state;
            gpsdata.measurements[num].time_from_last_bit_ms = mtk_gps_measurement[i].TimeFromLastBitInMs;
            gpsdata.measurements[num].time_offset_ns = mtk_gps_measurement[i].TimeOffsetInNs;
            gpsdata.measurements[num].used_in_fix = mtk_gps_measurement[i].UsedInFix;
            if (gpsdata.measurements[num].state == 0) {
                gpsdata.measurements[num].received_gps_tow_ns = 0;
                gpsdata.measurements[num].pseudorange_rate_mps = 1;
            }
            LOGD("gpsdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gpsdata.measurements[num].prn);
            if (gps_raw_debug_mode) {
                print_gps_measurement(gpsdata.measurements[num]);
            }
            gpsdata.measurement_count++;
        }
    }
    memcpy(&gpsdata.clock , &gpsclock, sizeof(gpsclock));
    if (gpsdata.measurement_count > 0) {
        ret = mnl2hal_gps_measurements(gpsdata);
        LOGD("mnl2hal_gps_measurements done ,ret = %d", ret);
    }
}
static void get_gps_navigation_event() {
    LOGD("get_gps_navigation_event begin");

    int svid;
    int i;
    int ret;
    MTK_GPS_NAVIGATION_EVENT gps_navigation_event;
    gps_nav_msg gpsnavigation;

    for (svid = 0; svid < GPS_MAX_MEASUREMENT; svid++) {
        ret = mtk_gps_get_navigation_event(&gps_navigation_event, svid);
        if (ret == 0) {
            LOGD("mtk_gps_get_navigation_event fail,SVID = %d,[ret=%d]\n", svid, ret);
            memset(&gps_navigation_event, 0, sizeof(MTK_GPS_NAVIGATION_EVENT));
        }

        memset(&gpsnavigation, 0, sizeof(gps_nav_msg));
        // gpsnavigation.size = sizeof(gps_nav_msg);
        gpsnavigation.prn = gps_navigation_event.prn;
        gpsnavigation.type = gps_navigation_event.type;
        gpsnavigation.message_id = gps_navigation_event.messageID;
        gpsnavigation.submessage_id = gps_navigation_event.submessageID;
        gpsnavigation.data_length = sizeof(gps_navigation_event.data);
        memcpy(gpsnavigation.data, gps_navigation_event.data, sizeof(gps_navigation_event.data));

        if (gps_raw_debug_mode) {
            print_gps_nav_msg(gpsnavigation);
            for (i = 0; i < 40; i++) {
                LOGD("gpsnavigation.data[%d] = %x, %p",
                    i, gpsnavigation.data[i], &gpsnavigation.data[i]);
            }
        }
        ret = mnl2hal_gps_navigation(gpsnavigation);
        LOGD("mnl2hal_gps_navigation done ,ret = %d", ret);
    }
}
#else
 void get_gps_measurement_clock_data() {
    LOGD("get_gps_measurement_clock_data begin");

    int i;
    int num = 0;
    int ret;
    gps_data gpsdata;
    MTK_GPS_MEASUREMENT mtk_gps_measurement[GPS_MAX_MEASUREMENT];
    INT8 ch_proc_ord_prn[GPS_MAX_MEASUREMENT]={0};

	memset(mtk_gps_measurement,0,sizeof(mtk_gps_measurement));
	
    LOGD("sizeof(mtk_gps_get_measurement) = %d,sizeof(mtk_gps_get_measurement[0]) = %d\n",
        sizeof(mtk_gps_measurement), sizeof(mtk_gps_measurement[0]));

    MTK_GPS_CLOCK mtk_gps_clock;
    ret = 0;
	memset(&mtk_gps_clock,0,sizeof(mtk_gps_clock));
    if (ret == 0) {
        LOGD("mtk_gps_get_clock fail,[ret=%d]\n", ret);
    }

    gps_clock gpsclock;
    memset(&gpsclock, 0, sizeof(gps_clock));
    // gpsclock.size = sizeof(gps_clock);
    gpsclock.bias_ns = mtk_gps_clock.BiasInNs;
    gpsclock.bias_uncertainty_ns = mtk_gps_clock.BiasUncertaintyInNs;
    gpsclock.drift_nsps = mtk_gps_clock.DriftInNsPerSec;
    gpsclock.flags = mtk_gps_clock.flag;
    gpsclock.leap_second = mtk_gps_clock.leapsecond;
    gpsclock.time_ns = mtk_gps_clock.TimeInNs;
    gpsclock.time_uncertainty_ns = mtk_gps_clock.TimeUncertaintyInNs;
    gpsclock.type = mtk_gps_clock.type;
    gpsclock.full_bias_ns = mtk_gps_clock.FullBiasInNs;
    gpsclock.drift_uncertainty_nsps = mtk_gps_clock.DriftUncertaintyInNsPerSec;
    if (gps_raw_debug_mode) {
        print_gps_clock(gpsclock);
    }

    memset(&gpsdata, 0, sizeof(gps_data));
    // gpsdata.size = sizeof(gps_data);
    for (i = 0; i < GPS_MAX_MEASUREMENT; i++) {
        if (mtk_gps_measurement[i].PRN != 0) {
            num = gpsdata.measurement_count;

            // gpsdata.measurements[num].size = sizeof(gps_measurement);
            gpsdata.measurements[num].accumulated_delta_range_m = mtk_gps_measurement[i].AcDRInMeters;
            gpsdata.measurements[num].accumulated_delta_range_state = mtk_gps_measurement[i].AcDRState10;
            gpsdata.measurements[num].accumulated_delta_range_uncertainty_m = \
            mtk_gps_measurement[i].AcDRUnInMeters;
            gpsdata.measurements[num].azimuth_deg = mtk_gps_measurement[i].AzInDeg;
            gpsdata.measurements[num].azimuth_uncertainty_deg = mtk_gps_measurement[i].AzUnInDeg;
            gpsdata.measurements[num].bit_number = mtk_gps_measurement[i].BitNumber;
            gpsdata.measurements[num].carrier_cycles = mtk_gps_measurement[i].CarrierCycle;
            gpsdata.measurements[num].carrier_phase = mtk_gps_measurement[i].CarrierPhase;
            gpsdata.measurements[num].carrier_phase_uncertainty = mtk_gps_measurement[i].CarrierPhaseUn;
            gpsdata.measurements[num].carrier_frequency_hz = mtk_gps_measurement[i].CFInhZ;
            gpsdata.measurements[num].c_n0_dbhz = mtk_gps_measurement[i].Cn0InDbHz;
            gpsdata.measurements[num].code_phase_chips = mtk_gps_measurement[i].CPInChips;
            gpsdata.measurements[num].code_phase_uncertainty_chips = mtk_gps_measurement[i].CPUnInChips;
            gpsdata.measurements[num].doppler_shift_hz = mtk_gps_measurement[i].DopperShiftInHz;
            gpsdata.measurements[num].doppler_shift_uncertainty_hz = mtk_gps_measurement[i].DopperShiftUnInHz;
            gpsdata.measurements[num].elevation_deg = mtk_gps_measurement[i].ElInDeg;
            gpsdata.measurements[num].elevation_uncertainty_deg = mtk_gps_measurement[i].ElUnInDeg;
            gpsdata.measurements[num].flags = mtk_gps_measurement[i].flag;
            gpsdata.measurements[num].loss_of_lock = mtk_gps_measurement[i].LossOfLock;
            gpsdata.measurements[num].multipath_indicator = mtk_gps_measurement[i].MultipathIndicater;
            gpsdata.measurements[num].pseudorange_m = mtk_gps_measurement[i].PRInMeters;
            gpsdata.measurements[num].prn = mtk_gps_measurement[i].PRN;
            gpsdata.measurements[num].pseudorange_rate_mps = mtk_gps_measurement[i].PRRateInMeterPreSec;
            gpsdata.measurements[num].pseudorange_rate_uncertainty_mps = \
            mtk_gps_measurement[i].PRRateUnInMeterPreSec;
            gpsdata.measurements[num].pseudorange_uncertainty_m = mtk_gps_measurement[i].PRUnInMeters;
            gpsdata.measurements[num].received_gps_tow_ns = mtk_gps_measurement[i].ReGpsTowInNs;
            gpsdata.measurements[num].received_gps_tow_uncertainty_ns = mtk_gps_measurement[i].ReGpsTowUnInNs;
            gpsdata.measurements[num].snr_db = mtk_gps_measurement[i].SnrInDb;
            gpsdata.measurements[num].state = mtk_gps_measurement[i].state;
            gpsdata.measurements[num].time_from_last_bit_ms = mtk_gps_measurement[i].TimeFromLastBitInMs;
            gpsdata.measurements[num].time_offset_ns = mtk_gps_measurement[i].TimeOffsetInNs;
            gpsdata.measurements[num].used_in_fix = mtk_gps_measurement[i].UsedInFix;
            if (gpsdata.measurements[num].state == 0) {
                gpsdata.measurements[num].received_gps_tow_ns = 0;
                gpsdata.measurements[num].pseudorange_rate_mps = 1;
            }
            LOGD("gpsdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gpsdata.measurements[num].prn);
            if (gps_raw_debug_mode) {
                print_gps_measurement(gpsdata.measurements[num]);
            }
            gpsdata.measurement_count++;
        }
    }
    memcpy(&gpsdata.clock , &gpsclock, sizeof(gpsclock));
    if (gpsdata.measurement_count > 0) {
        ret = mnl2hal_gps_measurements(gpsdata);
        LOGD("mnl2hal_gps_measurements done ,ret = %d", ret);
    }
}
 void get_gps_navigation_event() {
    LOGD("get_gps_navigation_event begin");

    int svid;
    int i;
    int ret;
    MTK_GPS_NAVIGATION_EVENT gps_navigation_event;
    gps_nav_msg gpsnavigation;

    for (svid = 0; svid < GPS_MAX_MEASUREMENT; svid++) {
        ret = 0;
		
        if (ret == 0) {
            LOGD("mtk_gps_get_navigation_event fail,SVID = %d,[ret=%d]\n", svid, ret);
            memset(&gps_navigation_event, 0, sizeof(MTK_GPS_NAVIGATION_EVENT));
        }

        memset(&gpsnavigation, 0, sizeof(gps_nav_msg));
        // gpsnavigation.size = sizeof(gps_nav_msg);
        gpsnavigation.prn = svid+1;
        gpsnavigation.type = GPS_NAV_MSG_TYPE_L1CA;
        gpsnavigation.message_id = 1;
        gpsnavigation.submessage_id = 1;
        gpsnavigation.data_length = sizeof(gps_navigation_event.data);
        memcpy(gpsnavigation.data, gps_navigation_event.data, sizeof(gps_navigation_event.data));

        if (gps_raw_debug_mode) {
            print_gps_nav_msg(gpsnavigation);
            for (i = 0; i < 40; i++) {
                LOGD("gpsnavigation.data[%d] = %x, %p",
                    i, gpsnavigation.data[i], &gpsnavigation.data[i]);
            }
        }
        ret = mnl2hal_gps_navigation(gpsnavigation);
        LOGD("mnl2hal_gps_navigation done ,ret = %d", ret);
    }
}

#endif

static void update_gnss_measurement(gnss_measurement* dst, Gnssmeasurement* src) {
    LOGD("update_gnss_measurement begin");

    fieldp_copy(dst, src, flags);
    fieldp_copy(dst, src, svid);
    fieldp_copy(dst, src, constellation);
    fieldp_copy(dst, src, time_offset_ns);
    fieldp_copy(dst, src, state);
    fieldp_copy(dst, src, received_sv_time_in_ns);
    fieldp_copy(dst, src, received_sv_time_uncertainty_in_ns);
    fieldp_copy(dst, src, c_n0_dbhz);
    fieldp_copy(dst, src, pseudorange_rate_mps);
    fieldp_copy(dst, src, pseudorange_rate_uncertainty_mps);
    fieldp_copy(dst, src, accumulated_delta_range_state);
    fieldp_copy(dst, src, accumulated_delta_range_m);
    fieldp_copy(dst, src, accumulated_delta_range_uncertainty_m);
    fieldp_copy(dst, src, carrier_frequency_hz);
    fieldp_copy(dst, src, carrier_cycles);
    fieldp_copy(dst, src, carrier_phase);
    fieldp_copy(dst, src, carrier_phase_uncertainty);
    fieldp_copy(dst, src, multipath_indicator);
    fieldp_copy(dst, src, snr_db);

    if (gps_raw_debug_mode) {
        print_gnss_measurement((*dst));
    }
}

static void update_gnss_clock(gnss_clock* dst, Gnssclock* src) {
    LOGD("update_gnss_clock begin");

    fieldp_copy(dst, src, flags);
    fieldp_copy(dst, src, leap_second);
    fieldp_copy(dst, src, time_ns);
    fieldp_copy(dst, src, time_uncertainty_ns);
    fieldp_copy(dst, src, full_bias_ns);
    fieldp_copy(dst, src, bias_ns);
    fieldp_copy(dst, src, bias_uncertainty_ns);
    fieldp_copy(dst, src, drift_nsps);
    fieldp_copy(dst, src, drift_uncertainty_nsps);
    fieldp_copy(dst, src, hw_clock_discontinuity_count);

    if (gps_raw_debug_mode) {
        print_gnss_clock((*dst));
    }
}

static void update_gnss_navigation(gnss_nav_msg* dst, GnssNavigationmessage* src) {
    LOGD("update_gnss_navigation begin");

    fieldp_copy(dst, src, svid);
    fieldp_copy(dst, src, type);
    fieldp_copy(dst, src, status);
    fieldp_copy(dst, src, message_id);
    fieldp_copy(dst, src, submessage_id);
    fieldp_copy(dst, src, data_length);

    if (gps_raw_debug_mode) {
        print_gnss_nav_msg((*dst));
    }
}

#ifndef CONFIG_GPS_MT3333
static void get_gnss_measurement_clock_data() {
    LOGD("get_gnss_measurement_clock_data begin");

    int i;
    int num = 0;
    int ret;
    gnss_data gnssdata;
    Gnssmeasurement gpqzmeasurement[40];
    Gnssmeasurement glomeasurement[24];
    Gnssmeasurement bdmeasurement[37];
    Gnssmeasurement galmeasurement[36];
    Gnssmeasurement sbasmeasurement[42];

    INT8 GpQz_Ch_Proc_Ord_PRN[40] = {0};
    INT8 Glo_Ch_Proc_Ord_PRN[24] = {0};
    INT8 BD_Ch_Proc_Ord_PRN[37] = {0};
    INT8 Gal_Ch_Proc_Ord_PRN[36] = {0};
    INT8 SBAS_Ch_Proc_Ord_PRN[42] = {0};

    mtk_gnss_get_measurement(gpqzmeasurement, glomeasurement, bdmeasurement, galmeasurement, sbasmeasurement,
        GpQz_Ch_Proc_Ord_PRN, Glo_Ch_Proc_Ord_PRN, BD_Ch_Proc_Ord_PRN, Gal_Ch_Proc_Ord_PRN, SBAS_Ch_Proc_Ord_PRN);

    Gnssclock gnssclock;
    if (mtk_gnss_get_clock(&gnssclock) == 0) {
        LOGD("mtk_gnss_get_clock fail\n");
        memset(&gnssclock, 0, sizeof(Gnssclock));
    }
    gnss_clock gnss_clock;
    memset(&gnss_clock, 0, sizeof(gnss_clock));
    update_gnss_clock(&gnss_clock, &gnssclock);


    memset(&gnssdata, 0, sizeof(gnssdata));
    // For GPS & QZSS
    for (i = 0; i < 40; i++) {
        if (gpqzmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &gpqzmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Glonass
    for (i = 0; i < 24; i++) {
        if (glomeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &glomeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Beidou
    for (i = 0; i < 37; i++) {
        if (bdmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &bdmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Galileo
    for (i = 0; i < 36; i++) {
        if (galmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &galmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For SBAS
    for (i = 0; i < 42; i++) {
        if (sbasmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &sbasmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    memcpy(&gnssdata.clock , &gnss_clock, sizeof(gnss_clock));
    LOGD("gnssdata.measurement_count = %d, sizeof(gnssdata) = %d", gnssdata.measurement_count, sizeof(gnssdata));
    if (gnssdata.measurement_count > 0) {
        ret = mnl2hal_gnss_measurements(gnssdata);
        LOGD("mnl2hal_gnss_measurements done ,ret = %d", ret);
    }
}

static void get_gnss_navigation_event() {
    LOGD("get_gnss_navigation_event begin");

    int svid;
    int i;
    int ret;
    GnssNavigationmessage gnss_navigation_msg;
    gnss_nav_msg gnssnavigation;

    // GPS
    for (svid = 1; svid <= GPS_MAX_MEASUREMENT; svid++) {
        ret = mtk_gnss_get_navigation_event(&gnss_navigation_msg, svid, 1);
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event GPS sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GP_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("GPS sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("GPS sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // glonass
    for (svid = 1; svid <= 24; svid++) {
        ret = mtk_gnss_get_navigation_event(&gnss_navigation_msg, svid, 16);
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event Glonass sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GL_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 12; i++) {
                LOGD("Glonass sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("Glonass sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // beidou
    for (svid = 1; svid <= 37; svid++) {
        ret = mtk_gnss_get_navigation_event(&gnss_navigation_msg, svid, 32);
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event BD sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.BD_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("BD sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("BD sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // galileo
    for (svid = 1; svid <= 36; svid++) {
        ret = mtk_gnss_get_navigation_event(&gnss_navigation_msg, svid, 4);
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event Galileo sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GA_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("Galileo sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("Galileo sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }
}

#else
gnss_sv_info cts_sv_data;

 void get_gnss_measurement_clock_data() {
    LOGD("get_gnss_measurement_clock_data begin");

    int i;
    int num = 0;
    int ret;
	int cnt;
    gnss_data gnssdata;
    Gnssmeasurement gpqzmeasurement[40];
    Gnssmeasurement glomeasurement[24];
    Gnssmeasurement bdmeasurement[37];
    Gnssmeasurement galmeasurement[36];
    Gnssmeasurement sbasmeasurement[42];

    INT8 GpQz_Ch_Proc_Ord_PRN[40] = {0};
    INT8 Glo_Ch_Proc_Ord_PRN[24] = {0};
    INT8 BD_Ch_Proc_Ord_PRN[37] = {0};
    INT8 Gal_Ch_Proc_Ord_PRN[36] = {0};
    INT8 SBAS_Ch_Proc_Ord_PRN[42] = {0};

	
	

    for (i = 0; i < 40; i++) {
		gpqzmeasurement[i].accumulated_delta_range_m=0;
		gpqzmeasurement[i].accumulated_delta_range_state=0x0;
		gpqzmeasurement[i].accumulated_delta_range_uncertainty_m=0;
		gpqzmeasurement[i].carrier_cycles=0;
		gpqzmeasurement[i].carrier_frequency_hz=1575420032.000000;
		gpqzmeasurement[i].carrier_phase=0;
		gpqzmeasurement[i].carrier_phase_uncertainty=0;

		gpqzmeasurement[i].svid=0;
		gpqzmeasurement[i].snr_db=0;
		gpqzmeasurement[i].constellation=0;
		gpqzmeasurement[i].c_n0_dbhz=0 ;
		for(cnt=cts_sv_data.num_svs; cnt>0; cnt--){
			if(cts_sv_data.sv_list[cnt-1].svid == (i+1)){
				if(cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17 < 0){
					gpqzmeasurement[i].snr_db=0;
				}else{
					gpqzmeasurement[i].snr_db=cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17;
				}
				
				gpqzmeasurement[i].constellation=cts_sv_data.sv_list[cnt-1].constellation;
				gpqzmeasurement[i].c_n0_dbhz=cts_sv_data.sv_list[cnt-1].c_n0_dbhz ;
				if(gpqzmeasurement[i].c_n0_dbhz <= 5.0){
					gpqzmeasurement[i].svid=0;
				}else{
					gpqzmeasurement[i].svid=cts_sv_data.sv_list[cnt-1].svid;
				}
				break;
			}
			
		}
		
		gpqzmeasurement[i].flags=0x40201;
		gpqzmeasurement[i].multipath_indicator=0;
		gpqzmeasurement[i].pseudorange_rate_mps=14522.145508;
		gpqzmeasurement[i].pseudorange_rate_uncertainty_mps=0.150000;
		gpqzmeasurement[i].received_sv_time_in_ns=20000;
		gpqzmeasurement[i].received_sv_time_uncertainty_in_ns=33353074;
		gpqzmeasurement[i].size=sizeof(gpqzmeasurement[i]);
		gpqzmeasurement[i].state=0x02;
		gpqzmeasurement[i].time_offset_ns=421235.186561;
	}

	for (i = 0; i < 24; i++) {
		glomeasurement[i].accumulated_delta_range_m=0;
		glomeasurement[i].accumulated_delta_range_state=0x0;
		glomeasurement[i].accumulated_delta_range_uncertainty_m=0;
		glomeasurement[i].carrier_cycles=0;
		glomeasurement[i].carrier_frequency_hz=1575420032.000000;
		glomeasurement[i].carrier_phase=0;
		glomeasurement[i].carrier_phase_uncertainty=0;

		glomeasurement[i].svid=0;
		glomeasurement[i].snr_db=0;
		glomeasurement[i].constellation=0;
		glomeasurement[i].c_n0_dbhz=0 ;
		for(cnt=cts_sv_data.num_svs; cnt>0; cnt--){
			if(cts_sv_data.sv_list[cnt-1].svid == (i+65)){
				if(cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17 < 0){
					glomeasurement[i].snr_db=0;
				}else{
					glomeasurement[i].snr_db=cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17;
				}
				
				glomeasurement[i].constellation=cts_sv_data.sv_list[cnt-1].constellation;
				glomeasurement[i].c_n0_dbhz=cts_sv_data.sv_list[cnt-1].c_n0_dbhz ;
				if(glomeasurement[i].c_n0_dbhz <= 5.0){
					glomeasurement[i].svid=0;
				}else{
					glomeasurement[i].svid=cts_sv_data.sv_list[cnt-1].svid - 65 + 1;
				}
				break;
			}
			
		}
		
		glomeasurement[i].flags=0x40201;
		glomeasurement[i].multipath_indicator=0;
		glomeasurement[i].pseudorange_rate_mps=14522.145508;
		glomeasurement[i].pseudorange_rate_uncertainty_mps=0.150000;
		glomeasurement[i].received_sv_time_in_ns=20000;
		glomeasurement[i].received_sv_time_uncertainty_in_ns=33353074;
		glomeasurement[i].size=sizeof(glomeasurement[i]);
		glomeasurement[i].state=0x02;
		glomeasurement[i].time_offset_ns=421235.186561;
	}
	for (i = 0; i < 37; i++) {
		bdmeasurement[i].accumulated_delta_range_m=0;
		bdmeasurement[i].accumulated_delta_range_state=0x0;
		bdmeasurement[i].accumulated_delta_range_uncertainty_m=0;
		bdmeasurement[i].carrier_cycles=0;
		bdmeasurement[i].carrier_frequency_hz=1575420032.000000;
		bdmeasurement[i].carrier_phase=0;
		bdmeasurement[i].carrier_phase_uncertainty=0;

		bdmeasurement[i].svid=0;
		bdmeasurement[i].snr_db=0;
		bdmeasurement[i].constellation=0;
		bdmeasurement[i].c_n0_dbhz=0 ;
		for(cnt=cts_sv_data.num_svs; cnt>0; cnt--){
			if(cts_sv_data.sv_list[cnt-1].svid == (i+201)){
				if(cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17 < 0){
					bdmeasurement[i].snr_db=0;
				}else{
					bdmeasurement[i].snr_db=cts_sv_data.sv_list[cnt-1].c_n0_dbhz - 17;
				}
				
				bdmeasurement[i].constellation=cts_sv_data.sv_list[cnt-1].constellation;
				bdmeasurement[i].c_n0_dbhz=cts_sv_data.sv_list[cnt-1].c_n0_dbhz ;
				if(bdmeasurement[i].c_n0_dbhz <= 5.0){
					bdmeasurement[i].svid=0;
				}else{
					bdmeasurement[i].svid=cts_sv_data.sv_list[cnt-1].svid - 201 + 1;
				}
				break;
			}
			
		}
		
		bdmeasurement[i].flags=0x40201;
		bdmeasurement[i].multipath_indicator=0;
		bdmeasurement[i].pseudorange_rate_mps=14522.145508;
		bdmeasurement[i].pseudorange_rate_uncertainty_mps=0.150000;
		bdmeasurement[i].received_sv_time_in_ns=20000;
		bdmeasurement[i].received_sv_time_uncertainty_in_ns=33353074;
		bdmeasurement[i].size=sizeof(bdmeasurement[i]);
		bdmeasurement[i].state=0x02;
		bdmeasurement[i].time_offset_ns=421235.186561;
	}
	for (i = 0; i < 36; i++) {
		memset(galmeasurement,0,sizeof(galmeasurement));
	}
	for (i = 0; i < 42; i++) {
		memset(sbasmeasurement,0,sizeof(sbasmeasurement));
	}

    Gnssclock gnssclock;
	memset(&gnssclock,0,sizeof(gnssclock));
	gnssclock.bias_ns=0.186561;
	gnssclock.bias_uncertainty_ns=609.745098;
	gnssclock.drift_nsps=7183.074831;
	gnssclock.drift_uncertainty_nsps=16.441889;
	gnssclock.flags=0x01;
	gnssclock.full_bias_ns=421235;
	gnssclock.hw_clock_discontinuity_count=0;
	gnssclock.leap_second=18;
	gnssclock.size=sizeof(gnssclock);
	gnssclock.time_ns=1185356046700000000;
	gnssclock.time_uncertainty_ns=33353073.878863;
	
    gnss_clock gnss_clock;
    memset(&gnss_clock, 0, sizeof(gnss_clock));
    update_gnss_clock(&gnss_clock, &gnssclock);


    memset(&gnssdata, 0, sizeof(gnssdata));
    // For GPS & QZSS
    for (i = 0; i < 40; i++) {
        if (gpqzmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &gpqzmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Glonass
    for (i = 0; i < 24; i++) {
        if (glomeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &glomeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Beidou
    for (i = 0; i < 37; i++) {
        if (bdmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &bdmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For Galileo
    for (i = 0; i < 36; i++) {
        if (galmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &galmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    // For SBAS
    for (i = 0; i < 42; i++) {
        if (sbasmeasurement[i].svid != 0) {
            num = gnssdata.measurement_count;
            if (num >= MTK_MNLD_GNSS_MAX_MEASUREMENT) {
                LOGD("measurement_count exceed the upper limit!");
                break;
            }

            update_gnss_measurement(&gnssdata.measurements[num], &sbasmeasurement[i]);
            if (gnssdata.measurements[num].state == 0) {
                gnssdata.measurements[num].pseudorange_rate_mps = 1;
            }
            gnssdata.measurement_count++;
            LOGD("gnssdata measurements[%d] memcpy completed, old _num = %d, prn = %d\n",
                num, i, gnssdata.measurements[num].svid);
        }
    }

    memcpy(&gnssdata.clock , &gnss_clock, sizeof(gnss_clock));
    LOGD("gnssdata.measurement_count = %d, sizeof(gnssdata) = %d", gnssdata.measurement_count, sizeof(gnssdata));
    if (gnssdata.measurement_count > 0) {
        ret = mnl2hal_gnss_measurements(gnssdata);
        LOGD("mnl2hal_gnss_measurements done ,ret = %d", ret);
    }

	memset(&cts_sv_data,0,sizeof(cts_sv_data));
}

 void get_gnss_navigation_event() {
    LOGD("get_gnss_navigation_event begin");

    int svid;
    int i;
    int ret;
    GnssNavigationmessage gnss_navigation_msg;
    gnss_nav_msg gnssnavigation;

    // GPS
    for (svid = 1; svid <= GPS_MAX_MEASUREMENT; svid++) {
        ret = 0;
		gnss_navigation_msg.data_length=40;
		gnss_navigation_msg.message_id=1;
		gnss_navigation_msg.submessage_id=1;
		gnss_navigation_msg.size=sizeof(gnss_navigation_msg);
		gnss_navigation_msg.status=0x0;
		gnss_navigation_msg.svid=svid;
		gnss_navigation_msg.type=GNSS_NAVIGATION_MESSAGE_TYPE_GPS_L1CA;
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event GPS sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GP_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("GPS sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("GPS sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // glonass
    for (svid = 1; svid <= 24; svid++) {
        ret = 0;
		gnss_navigation_msg.data_length=12;
		gnss_navigation_msg.message_id=1;
		gnss_navigation_msg.submessage_id=1;
		gnss_navigation_msg.size=sizeof(gnss_navigation_msg);
		gnss_navigation_msg.status=0x0;
		gnss_navigation_msg.svid=svid;
		gnss_navigation_msg.type=GNSS_NAVIGATION_MESSAGE_TYPE_GLO_L1CA;
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event Glonass sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GL_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 12; i++) {
                LOGD("Glonass sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("Glonass sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // beidou
    for (svid = 1; svid <= 37; svid++) {
        ret = 0;
		gnss_navigation_msg.data_length=40;
		gnss_navigation_msg.message_id=1;
		gnss_navigation_msg.submessage_id=1;
		gnss_navigation_msg.size=sizeof(gnss_navigation_msg);
		gnss_navigation_msg.status=0x0;
		gnss_navigation_msg.svid=svid;
		gnss_navigation_msg.type=GNSS_NAVIGATION_MESSAGE_TYPE_BDS_D1;
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event BD sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.BD_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("BD sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("BD sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }

    // galileo
    for (svid = 1; svid <= 36; svid++) {
        ret = 0;
		gnss_navigation_msg.data_length=40;
		gnss_navigation_msg.message_id=1;
		gnss_navigation_msg.submessage_id=1;
		gnss_navigation_msg.size=sizeof(gnss_navigation_msg);
		gnss_navigation_msg.status=0x0;
		gnss_navigation_msg.svid=svid;
		gnss_navigation_msg.type=GNSS_NAVIGATION_MESSAGE_TYPE_GAL_I;
        if (ret == 0) {
            LOGD("mtk_gnss_get_navigation_event Galileo sv fail, svid = %d,[ret=%d]\n", svid, ret);
            memset(&gnss_navigation_msg, 0, sizeof(GnssNavigationmessage));
        }

        memset(&gnssnavigation, 0, sizeof(gnss_nav_msg));
        update_gnss_navigation(&gnssnavigation, &gnss_navigation_msg);
        memcpy(gnssnavigation.data, gnss_navigation_msg.uData.GA_data, gnssnavigation.data_length);
        if (gps_raw_debug_mode) {
            for (i = 0; i < 40; i++) {
                LOGD("Galileo sv gnssnavigation.data[%d] = %x, %p",
                i, gnssnavigation.data[i], &gnssnavigation.data[i]);
            }
        }
        ret = mnl2hal_gnss_navigation(gnssnavigation);
        LOGD("Galileo sv, mnl2hal_gnss_navigation done ,ret = %d", ret);
    }
}

#endif


#endif

#if ANDROID_MNLD_PROP_SUPPORT
/*---------------------------------------------------------------------------*/
#define  MNL_CONFIG_STATUS      "persist.radio.mnl.prop"
static int get_prop(int index) {
    // Read property
    char result[PROPERTY_VALUE_MAX] = {0};
    int ret = 0;
    if (property_get(MNL_CONFIG_STATUS, result, NULL)) {
        ret = result[index] - '0';
        LOGD("gps.log: %s, %d\n", &result[index], ret);
    } else {
        if (index == 7) {
            ret = 1;
        } else {
            ret = 0;
        }
        LOGD("Config is not set yet, use default value");
    }
    return ret;
}

int get_gps_cmcc_log_enabled() {
    int is_enabled = get_prop(6);
    return is_enabled;
}
#endif
static int gps_control_event_hdlr(int fd) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    main2gps_event cmd;
    int read_len;

    read_len = safe_recvfrom(fd, buff, sizeof(buff));
    if (read_len <= 0) {
        LOGE("gps_control_event_hdlr() safe_recvfrom() failed read_len=%d", read_len);
        return -1;
    }

    cmd = get_int(buff, &offset);
    switch (cmd) {
    case MAIN2GPS_EVENT_START: {
        int delete_aiding_data_flags = get_int(buff, &offset);
        // need to call mnld_gps_start_done() when GPS is started
        LOGW("mnld version: %s", MNLD_VERSION);
        mnld_gps_start(delete_aiding_data_flags);
        break;
    }
    case MAIN2GPS_EVENT_STOP: {
        // need to call mnld_gps_stop_done() when GPS is stopped
        mnld_gps_stop();
        break;
    }
    case MAIN2GPS_DELETE_AIDING_DATA: {
        int delete_aiding_data_flags = get_int(buff, &offset);
        LOGW("mnld_gps_delete_aiding_data() before delete_aiding_data_flags=0x%x",
            delete_aiding_data_flags);
        // need to call mnld_gps_reset_done() when GPS is reset
        mnld_gps_delete_aiding_data(delete_aiding_data_flags);
        LOGW("mnld_gps_delete_aiding_data() after");
        break;
    }
    case GPS2GPS_NMEA_DATA_TIMEOUT: {
        // not to start nmea timer if only LINK user
        // although send_ative_noitfy already add the protection, it's also needed here
        // to handle race condition:
        //   1. send_ative_noitfy send a GPS2GPS_NMEA_DATA_TIMEOUT msg
        //   2. MAIN thread change user to FLP only, and mnld_fsm stop nmea timer
        //   3. GPS2GPS_NMEA_DATA_TIMEOUT reach here, and restart the timer
        // then, GPS2MAIN_EVENT_NMEA_TIMEOUT may be caused
        int gps_user = mtk_gps_get_gps_user();
        if (((gps_user & (GPS_USER_FLP | GPS_USER_OFL_TEST | GPS_USER_GEOFENCE)) == gps_user) &&
            mnl_offload_is_enabled() &&
            (mnld_offload_is_auto_mode() || mnld_offload_is_always_on_mode())) {
            break;
        }

        mnld_gps_controller_mnl_nmea_timeout();
        break;
    }
    default: {
        LOGE("gps_control_event_hdlr() unknown cmd=%d", cmd);
        return -1;
    }
    }
    return 0;
}

static void gps_control_thread_timeout() {
    if (mnld_timeout_ne_enabled() == false) {
        LOGE("gps_control_thread_timeout() dump and exit.");
        mnld_block_exit();
    } else {
        LOGE("gps_control_thread_timeout() crash here for debugging");
        CRASH_TO_DEBUG();
    }
}

static void* gps_control_thread(void *arg) {
    #define MAX_EPOLL_EVENT 50
    timer_t hdlr_timer = init_timer(gps_control_thread_timeout);
    struct epoll_event events[MAX_EPOLL_EVENT];
    UNUSED(arg);

    int epfd = epoll_create(MAX_EPOLL_EVENT);
    if (epfd == -1) {
        LOGE("gps_control_thread() epoll_create failure reason=[%s]%d",
            strerror(errno), errno);
        return 0;
    }

    if (epoll_add_fd(epfd, g_fd_gps) == -1) {
        LOGE("gps_control_thread() epoll_add_fd() failed for g_fd_gps failed");
        return 0;
    }

    while (1) {
        int i;
        int n;
        LOGD("gps_control_thread wait");
        n = epoll_wait(epfd, events, MAX_EPOLL_EVENT , -1);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                LOGE("gps_control_thread() epoll_wait failure reason=[%s]%d",
                    strerror(errno), errno);
                return 0;
            }
        }
        start_timer(hdlr_timer, MNLD_GPS_HANDLER_TIMEOUT);
        for (i = 0; i < n; i++) {
            if (events[i].data.fd == g_fd_gps) {
                if (events[i].events & EPOLLIN) {
                    gps_control_event_hdlr(g_fd_gps);
                }
            } else {
                LOGE("gps_control_thread() unknown fd=%d",
                    events[i].data.fd);
            }
        }
        stop_timer(hdlr_timer);
    }

    LOGE("gps_control_thread() exit");
    return 0;
}

int gps_control_gps_start(int delete_aiding_data_flags) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, MAIN2GPS_EVENT_START);
    put_int(buff, &offset, delete_aiding_data_flags);
    return safe_sendto(MNLD_GPS_CONTROL_SOCKET, buff, offset);
}

int gps_control_gps_stop() {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, MAIN2GPS_EVENT_STOP);
    return safe_sendto(MNLD_GPS_CONTROL_SOCKET, buff, offset);
}

int gps_control_gps_reset(int delete_aiding_data_flags) {
    char buff[MNLD_INTERNAL_BUFF_SIZE] = {0};
    int offset = 0;
    put_int(buff, &offset, MAIN2GPS_DELETE_AIDING_DATA);
    put_int(buff, &offset, delete_aiding_data_flags);
    return safe_sendto(MNLD_GPS_CONTROL_SOCKET, buff, offset);
}

int gps_control_init() {
    pthread_t pthread_gps;

    hasAlmanac();
    g_fd_gps = socket_bind_udp(MNLD_GPS_CONTROL_SOCKET);
    if (g_fd_gps < 0) {
        LOGE("socket_bind_udp(MNLD_GPS_CONTROL_SOCKET) failed");
        return -1;
    }

    pthread_create(&pthread_gps, NULL, gps_control_thread, NULL);

    return 0;
}

// for kernel wakelock control when offload is enabled
#if 1
static int gps_kernel_wakelock_fd = C_INVALID_FD;
static int gps_kernel_wakelock_to_take = 1;

void gps_control_kernel_wakelock_init() {
    if (gps_kernel_wakelock_fd != dsp_fd && dsp_fd >= 0) {
        gps_kernel_wakelock_fd = dsp_fd;
        LOGD("dsp_fd is opened, fellow wakelock to take = %d, dsp_fd = %d",
            gps_kernel_wakelock_to_take, gps_kernel_wakelock_fd);
        if (!gps_kernel_wakelock_to_take) {
            gps_control_kernel_wakelock_give();
        } else {
            LOGD("dsp_fd is opened, wakelock is taken defaultly, no need to take again");
        }
    }
}

void gps_control_kernel_wakelock_uninit() {
    gps_kernel_wakelock_fd = C_INVALID_FD;
}

void gps_control_kernel_wakelock_take() {
    int ret, fd;
    fd = gps_kernel_wakelock_fd;
    gps_kernel_wakelock_to_take = 1;
    if (fd >= 0) {
        ret = ioctl(fd, COMBO_IOC_TAKE_GPS_WAKELOCK, NULL);
        LOGD("take kernel wakelock, fd = %d, ret = %d", fd, ret);
    } else {
        LOGD("dsp_fd not opened, record wakelock to take = %d", gps_kernel_wakelock_to_take);
    }
}

void gps_control_kernel_wakelock_give() {
    int ret, fd;
    fd = gps_kernel_wakelock_fd;
    gps_kernel_wakelock_to_take = 0;
    if (fd >= 0) {
        ret = ioctl(fd, COMBO_IOC_GIVE_GPS_WAKELOCK, NULL);
        LOGD("give kernel wakelock, fd = %d, ret = %d", fd, ret);
    } else {
        LOGD("dsp_fd not opened, record wakelock to take = %d", gps_kernel_wakelock_to_take);
    }
}
#endif

/////////////////////////////////////////////////////////////////////////////
// META mode
/*****************************************************************************/
void linux_signal_handler(int signo) {
    int ret = 0;
    pthread_t self = pthread_self();
    if (signo == SIGTERM) {
        int gps_user = GPS_USER_UNKNOWN;
        gps_user = mtk_gps_get_gps_user();
        if ((gps_user & GPS_USER_APP) != 0) {
            LOGD("Normal mode,sdcard storage send SIGTERM to mnld");
            gps_debuglog_state = MTK_GPS_DISABLE_DEBUG_MSG_WR_BY_MNLD;
            if (mnld_is_gps_started_done()) {
                ret = mtk_gps_set_debug_type(gps_debuglog_state);
                if (MTK_GPS_ERROR== ret) {
                    LOGD("sdcard storage send SIGTERM to mnld, stop gpsdebuglog, mtk_gps_set_debug_type fail");
                }
            }
        } else {
            LOGD("Meta or factory or adb shell mode done");
            if (gps_user & GPS_USER_META) {
                mnld_gps_stop();
            } else if (gps_user & GPS_USER_OFL_TEST) {
                // flp_test2mnl_gps_start();
                mnld_gps_start(FLAG_HOT_START);
            }
#ifdef CONFIG_GPS_MT3333
			else{
				mnld_gps_stop();
			}
#endif
            exit_meta_factory = 1;
        }
    }
    LOGD("Signal handler of %.8x -> %s\n", (unsigned int)self, sys_siglist[signo]);
}

int linux_setup_signal_handler(void) {
    struct sigaction actions;
    int err;
    /*the signal handler is MUST, otherwise, the thread will not be killed*/
    memset(&actions, 0, sizeof(actions));
    sigemptyset(&actions.sa_mask);
    actions.sa_flags = 0;
    actions.sa_handler = linux_signal_handler;
    if ((err = sigaction(SIGTERM, &actions, NULL))) {
        LOGD("register signal hanlder for SIGTERM: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int mnld_factory_test_entry(int argc, char** argv) {
    int res = 0;

    LOGD("Meta or factory or adb shell mode");

    in_meta_factory = 1;
    res = unlink(NV_FILE);
    LOGD("unlink NV_FILE, errno=%d, res=%d\n", errno, res);
    MNLD_STRNCPY(gps_debuglog_file_name, LOG_FILE, GPS_DEBUG_LOG_FILE_NAME_MAX_LEN);
    if (argc >= 4) {
        if (!strncmp(argv[3], "od", 2)) {
            LOGD("MNL is non-offload");
            mnld_cfg.OFFLOAD_enabled = 0;
            // factory_mnld_gps_start();
            mnld_gps_start(FLAG_HOT_START);
        } else if (!strncmp(argv[3], "ot", 2)) {
            LOGD("MNL is offload, run on test mode");
            // flp_test2mnl_gps_start();
            mnld_gps_start(FLAG_HOT_START);
        } else if (!strncmp(argv[2], "PDNTest", 7)) {
            PDN_test_enable = atoi(argv[3]);
            LOGD("PDN test start, option is %d\n",PDN_test_enable);
            mnld_gps_start(FLAG_HOT_START);
        } else {
            LOGD("MNL is offload, for meta/factory mode");
            // factory_mnld_gps_start();
#ifdef CONFIG_GPS_MT3333
		mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_FACTORYMETA);
#endif
            mnld_gps_start(FLAG_HOT_START);
        }
    } else {
        LOGD("MNL is offload, for meta/factory mode");
        // factory_mnld_gps_start();
#ifdef CONFIG_GPS_MT3333
		mt3333_controller_socket_send_cmd(MAIN_MT3333_CONTROLLER_EVENT_FACTORYMETA);
#endif
        mnld_gps_start(FLAG_HOT_START);
    }
    while (1) {
        usleep(100000);
        if (exit_meta_factory == 1) {
            LOGD("Meta or factory mode exit");
            exit_meta_factory = 0;
            in_meta_factory = 0;
            exit(1);
        }
        LOGD("Meta or factory mode testing...");
    }
    in_meta_factory = 0;
}

