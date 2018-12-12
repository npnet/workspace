#
LOCAL_DIR := $(GET_LOCAL_DIR)
TARGET := k63v2_b70
MODULES += app/mt_boot \
           dev/lcm
MTK_EMMC_SUPPORT=yes
MTK_KERNEL_POWER_OFF_CHARGING = yes
MTK_SMI_SUPPORT = yes
DEFINES += MTK_GPT_SCHEME_SUPPORT
MTK_PUMP_EXPRESS_PLUS_SUPPORT := no
MTK_MT6370_PMU_CHARGER_SUPPORT := yes
MTK_MT6370_PMU_BLED_SUPPORT := yes
MTK_CHARGER_INTERFACE := yes
MTK_LCM_PHYSICAL_ROTATION = 0
CUSTOM_LK_LCM="r69429_wuxga_dsi_vdo"
#nt35595_fhd_dsi_cmd_truly_nt50358 = yes
MTK_SECURITY_SW_SUPPORT = yes
MTK_VERIFIED_BOOT_SUPPORT = yes
MTK_SEC_FASTBOOT_UNLOCK_SUPPORT = yes
SPM_FW_USE_PARTITION = yes
BOOT_LOGO := wuxga
DEBUG := 2
#DEFINES += WITH_DEBUG_DCC=1
DEFINES += WITH_DEBUG_UART=1
#DEFINES += WITH_DEBUG_FBCON=1
CUSTOM_LK_USB_UNIQUE_SERIAL=no
MTK_TINYSYS_SCP_SUPPORT = no
MTK_TINYSYS_SSPM_SUPPORT = yes
#DEFINES += NO_POWER_OFF=y
DEFINES += MTK_NEW_COMBO_EMMC_SUPPORT
#DEFINES += FPGA_BOOT_FROM_LK=y
MTK_PROTOCOL1_RAT_CONFIG = C/Lf/Lt/W/T/G
MTK_GOOGLE_TRUSTY_SUPPORT = no
DEFINES += MTK_MT6370_PMU
MTK_DM_VERITY_OFF = no
MTK_DYNAMIC_CCB_BUFFER_GEAR_ID =
