/* include/dal_obc_config.h */
#ifndef DAL_OBC_CONFIG_H
#define DAL_OBC_CONFIG_H

#include <termios.h>
#include <stdint.h>

#define DAL_OBC_VERSION_MAJOR 0
#define DAL_OBC_VERSION_MINOR 1
#define DAL_OBC_VERSION_PATCH 0

#define DAL_OBC_HW_VERSION_MAJOR 1
#define DAL_OBC_HW_VERSION_MINOR 0
#define DAL_OBC_HW_VERSION_PATCH 0

/* Board hardware revisions reported by sys_info. */
#define DAL_PDU_HW_VERSION_MAJOR 1
#define DAL_PDU_HW_VERSION_MINOR 0
#define DAL_PDU_HW_VERSION_PATCH 0
#define DAL_EXP_HW_VERSION_MAJOR 1
#define DAL_EXP_HW_VERSION_MINOR 0
#define DAL_EXP_HW_VERSION_PATCH 0

#define DAL_OBC_VERSION_BYTE(major, minor, patch) \
    ((uint8_t)((major) * 100 + (minor) * 10 + (patch)))

#define DAL_OBC_RTC_DEVICE "/dev/rtc2"

/* =========================================================================
 * RS-422 link to the satellite OBC
 * ========================================================================= */
#define DAL_OBC_RS422_PORT       "/dev/ttyS1"
#define DAL_OBC_RS422_BAUD       B921600
#define DAL_OBC_RS422_BAUD_NUM   921600
#define DAL_OBC_RS422_TIMEOUT_MS 2000

#define DAL_OBC_RS422_READ_TIMEOUT_MS 200

#define DAL_OBC_HK_SNAPSHOT_PATH "/run/dal/dal_obc_fsw/obc_data.json"
#define DAL_OBC_HK_MAX_AGE_SEC   30U
#define DAL_OBC_CONSOLE_SOCKET   "/run/dal/dal_obc_fsw/console.sock"

/* Main loop status print interval */
#define SYSTEM_STATUS_INTERVAL_SEC 60

/* ICD data-transfer backend reads hk_service's persistent CSV directory. */
#define BEEIF_DATA_DIR         "/home/bee/data"

/* =========================================================================
 * Debug handoff 
 * ========================================================================= */
#define DAL_OBC_DEBUG_GETTY_UNIT "serial-getty@ttyS1.service"

#endif /* DAL_OBC_CONFIG_H */
