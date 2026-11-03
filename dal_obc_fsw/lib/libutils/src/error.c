/* utils/error.c */
#include "utils/error.h"
#include <stdio.h>

const char *xerr_to_str(xerr_t err, char *buf, size_t buflen)
{
    const char *s;
    switch (err) {
    case XERR_OK:        s = "OK";                 break;
    case XERR_IO:        s = "I/O error";          break;
    case XERR_NOMEM:     s = "out of memory";      break;
    case XERR_INVAL:     s = "invalid argument";   break;
    case XERR_TIMEOUT:   s = "timed out";          break;
    case XERR_NOT_FOUND: s = "not found";          break;
    case XERR_RANGE:     s = "out of range";       break;
    case XERR_STALE:     s = "stale";              break;
    case XERR_STATE:     s = "invalid state";      break;
    default:             s = "unknown error";      break;
    }
    snprintf(buf, buflen, "%s (%d)", s, (int)err);
    return buf;
}
