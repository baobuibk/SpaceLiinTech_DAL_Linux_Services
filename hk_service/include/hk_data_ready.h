#pragma once
/*
 * hk_data_ready.h - MCU -> MPU data handoff over a shared SPI PSRAM
 *
 * The EXP MCU and the OBC (MPU) both reach the same SPI
 * PSRAM chip through a switch whose SEL line the MCU normally holds.
 * When the MCU has data ready to hand off, it drives a dedicated GPIO
 * (xio pin sig_input_exp1) high. hk_service polls
 * that pin; once it has read high continuously for >= debounce_ms (to
 * reject glitches) AND the board is currently online, it:
 *
 *   1. lite-client run <node> "data_ready_collect"
 *        -> MCU replies with "TypeName: X\nSize: 0xNNNN\nCRC16XModem: 0xMMMM\n"
 *           and switches the PSRAM SEL line over to the MPU.
 *   2. spi_flash -d <bus> --dump -a 0 -s <Size> -o <file>
 *        -> reads exactly Size bytes starting at address 0 (per the
 *           MCU's own accounting) into
 *           <data_dir>/<YYYYMMDD>/<TypeName>_<epoch_ms>.bin
 *   3. lite-client run <node> "data_ready_done"
 *        -> tells the MCU to switch SEL back and prepare new data.
 *        Sent whenever step 1 succeeded, even if step 2 failed, so
 *        the MCU is never left stuck holding the switch over to MPU.
 *
 * CRC16XModem is logged for reference only -- per spec, a bad CRC is
 * not retried.
 *
 * NOTE: the exact `xio` command/format for reading sig_input_exp1
 * was not confirmed on real hardware when this was written -- see the
 * ASSUMED note in hk_xio.h. Everything downstream of "pin read as
 * active" (the rgosh/spi_flash sequencing here) is confirmed against
 * real command syntax (`lite-client run`, `spi_flash --dump`).
 */

#include <stdint.h>
#include "hk_can.h"
#include "hk_xio.h"

typedef enum {
    HK_DR_IDLE = 0,      /* pin low, waiting for a rising edge          */
    HK_DR_DEBOUNCE,      /* pin seen high, timing continuous-high hold  */
    HK_DR_WAIT_LOW,      /* sequence handled, waiting for pin to fall   */
} HkDataReadyState;

typedef struct {
    HkDataReadyState state;
    uint32_t         high_ms;   /* continuous-high time accumulated in DEBOUNCE */
} HkDataReadyCtx;

typedef struct {
    const char *spi_flash_path;
    const char *data_dir;      /* e.g. /home/bee/data, dated subfolder created per file */
    uint32_t    debounce_ms;   /* default 500 */
    uint32_t    dump_timeout_base_ms;
} HkDataReadyCfg;

void hk_data_ready_ctx_init(HkDataReadyCtx *ctx);

/*
 * Call once per poll tick (recommended every 100ms) for one board.
 * elapsed_ms is how much wall time passed since the previous call for
 * this ctx (used to accumulate debounce time -- not hard-coded, so the
 * caller's actual poll cadence doesn't need to match any assumption
 * here). online must reflect that board's current CAN/power status
 * (from the existing heartbeat) -- when false, the pin is not even
 * queried and the state machine is held at IDLE, per spec ("board
 * phải online"). spi_bus is the spidev index for `spi_flash -d
 * <spi_bus>` (0 for the installed EXP/FLEXCOM0).
 */
void hk_data_ready_tick(const HkDataReadyCfg *dr_cfg, const HkXioCfg *xio_cfg,
                         const HkCanCfg *can_cfg, HkDataReadyCtx *ctx,
                         const char *board_name, int node_addr,
                         const char *pin_name, int spi_bus, int online,
                         uint32_t elapsed_ms);
