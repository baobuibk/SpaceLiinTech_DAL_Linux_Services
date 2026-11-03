/* utils/error.h*/
#ifndef LIBUTILS_UTILS_ERROR_H
#define LIBUTILS_UTILS_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

typedef enum {
    XERR_OK        = 0,
    XERR_IO        = -5,   /* I/O error */
    XERR_NOMEM     = -12,  /* Out of memory */
    XERR_INVAL     = -22,  /* Invalid argument */
    XERR_TIMEOUT   = -110, /* Timed out */
    XERR_NOT_FOUND = -201, /* Resource not found */
    XERR_RANGE     = -203, /* Value/index out of range */
    XERR_STALE     = -206, /* Data is old/no longer valid  */
    XERR_STATE     = -208, /* Invalid system state */
} xerr_t;

const char *xerr_to_str(xerr_t err, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif
#endif /* LIBUTILS_UTILS_ERROR_H */
