/* include/services/bee_dal_tmtc_service.h
 */
#ifndef BEE_DAL_TMTC_SERVICE_H
#define BEE_DAL_TMTC_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include "utils/error.h"
#include "bee_dal_proto.h"

typedef struct {
    uint8_t ack_err;
    uint8_t addr_errcode;
    uint16_t length;
    uint8_t payload[BEE_DAL_MAX_PAYLOAD];
    bool ready;
} bee_dal_local_reply_t;

#ifdef __cplusplus
extern "C" {
#endif

xerr_t bee_dal_tmtc_service_init(void);
xerr_t bee_dal_tmtc_service_start(void);
void   bee_dal_tmtc_service_stop(void);
bool   bee_dal_tmtc_service_is_running(void);
bool   bee_dal_tmtc_service_debug_handoff_requested(void);
int bee_dal_tmtc_execute_local(uint8_t type, uint8_t addr,
                               const uint8_t *payload, uint16_t length,
                               bee_dal_local_reply_t *reply);

#ifdef __cplusplus
}
#endif
#endif /* BEE_DAL_TMTC_SERVICE_H */
