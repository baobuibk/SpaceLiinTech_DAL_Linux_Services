#ifndef DAL_UVC_CAPTURE_H
#define DAL_UVC_CAPTURE_H

#include <stdint.h>

/* State: 0 unknown, 1 queued, 2 capturing, 3 saved, 4 failed. */
int uvc_capture_queue(uint8_t camera, uint32_t request_id, uint8_t reply[8]);
int uvc_capture_status(uint8_t camera, uint32_t request_id, uint8_t reply[8]);

#endif
