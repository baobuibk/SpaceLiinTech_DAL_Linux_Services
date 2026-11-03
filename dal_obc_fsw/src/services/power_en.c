#include "services/power_en.h"
#include "services/exp_uart_service.h"
#include "services/gui_state.h"
#include "services/exp_csp_service.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const obc_lines[8]={
    "pow_onoff_ai", "pow_onoff_fpga", "usb_1_pwr_en", "usb_2_pwr_en",
    "usb_3_pwr_en", "usb_4_pwr_en", "pow_mipicam_1", "pow_mipicam_2"
};
static const char *const exp_lines[3]={"exp12v", "sln_hd", "motor"};
static const char *const exp_labels[3]={"12V_EXP", "SLN HD", "MOTOR"};

static int obc_led(uint8_t bit, bool write_state, bool desired, bool *state)
{
    const char *root=getenv("DAL_GUI_LED_ROOT");
    if (!root || !*root) root="/sys/class/leds";
    char path[512];
    if (snprintf(path,sizeof(path),"%s/%s/brightness",root,obc_lines[bit]) >= (int)sizeof(path)) return -1;
    if (write_state) {
        /* A powered-down camera must not leave its I2C switch enabled. */
        if (bit>=6 && !desired) {
            char i2c_path[512];
            if (snprintf(i2c_path,sizeof(i2c_path),"%s/en_i2c_mipicam_%u/brightness",
                         root,(unsigned)bit-5U) >= (int)sizeof(i2c_path)) return -1;
            FILE *i2c=fopen(i2c_path,"w");
            if (!i2c) return -1;
            int written=fputs("0\n",i2c);
            int closed=fclose(i2c);
            if (written<0 || closed!=0) return -1;
        }
        FILE *fp=fopen(path,"w");
        if (!fp) return -1;
        int written=fputs(desired ? "1\n" : "0\n",fp);
        int closed=fclose(fp);
        if (written<0 || closed!=0) return -1;
    }
    FILE *fp=fopen(path,"r");
    if (!fp) return -1;
    int value=-1;
    int ok=fscanf(fp,"%d",&value)==1 && (value==0 || value==1);
    fclose(fp);
    if (!ok) return -1;
    *state=value!=0;
    if (write_state && *state!=desired) return -1;
    return 0;
}

static void exp_read_all(power_en_result_t *result)
{
    char response[2048], marker[64];
    if (exp_uart_command("power_all_get",response,sizeof(response))!=0) return;
    for (uint8_t bit=0;bit<3;bit++) {
        snprintf(marker,sizeof(marker),"Power %s requested=",exp_labels[bit]);
        const char *line=strstr(response,marker);
        if (!line) continue;
        line+=strlen(marker);
        if (!strncmp(line,"ON",2) && (line[2]==' ' || line[2]=='\r' || line[2]=='\n')) {
            result->known|=(uint16_t)(1U<<bit);
            result->on|=(uint16_t)(1U<<bit);
        } else if (!strncmp(line,"OFF",3) &&
                   (line[3]==' ' || line[3]=='\r' || line[3]=='\n')) {
            result->known|=(uint16_t)(1U<<bit);
        }
    }
}

static int exp_set(uint8_t bit, bool desired, bool force)
{
    char command[48], response[1024], expected[64];
    snprintf(command,sizeof(command),"power_%s_%s%s",exp_lines[bit],
             desired ? "on" : "off",force ? " force" : "");
    if (exp_uart_command(command,response,sizeof(response))!=0) return -1;
    snprintf(expected,sizeof(expected),"Power %s %s: OK",exp_labels[bit],
             desired ? "ON" : "OFF");
    return strstr(response,expected) ? 0 : -1;
}

int power_en_execute(uint8_t addr, uint16_t select, uint16_t on, uint8_t flags,
                     power_en_result_t *result)
{
    if (!result) return -1;
    memset(result,0,sizeof(*result));
    uint16_t allowed=addr==3 ? 0x00ffU : addr==4 ? 0x0007U : addr==5 ? 0x0003U : 0;
    if (!allowed || (select & ~allowed) || (on & ~select) || (flags & ~1U) ||
        (flags && (addr!=4 || !select || (on & select))) ||
        (addr==5 && (select & 3U)==3U && (on & 3U)==1U)) return -2;
    if (addr==5)
        return gui_pdu_power(select,on,&result->on,&result->known,&result->failed);
    if (addr==4 && !select)
        return exp_csp_rail_state(&result->known, &result->on);
    if (addr==4 && select==0x0007U && (on==0 || on==0x0007U)) {
        char response[1024];
        char command[32];
        snprintf(command,sizeof(command),"power_all_%s%s",
                 on ? "on" : "off",flags ? " force" : "");
        /* The MCU handles all three domains in one CLI transaction. The
         * readback below determines which domains reached the requested state. */
        exp_uart_command(command,response,sizeof(response));
    } else {
        for (uint8_t bit=0;bit<(addr==3 ? 8U : 3U);bit++) {
            uint16_t mask=(uint16_t)(1U<<bit);
            if (select & mask) {
                bool desired=(on & mask)!=0;
                int rc=addr==3 ? obc_led(bit,true,desired,&desired) :
                    exp_set(bit,desired,(flags & 1U)!=0);
                if (rc!=0) result->failed|=mask;
            }
            if (addr==4) continue;
            bool state=false;
            if (obc_led(bit,false,false,&state)==0) {
                result->known|=mask;
                if (state) result->on|=mask;
                if ((select & mask) && state!=((on & mask)!=0)) result->failed|=mask;
            } else if (select & mask) result->failed|=mask;
        }
    }
    if (addr==4) {
        exp_read_all(result);
        /* UART may lose the command reply even though the MCU applied the rail
         * request. Its readback is authoritative for the requested state. */
        result->failed=(uint16_t)((select & ~result->known) |
                                  (select & result->known & (result->on ^ on)));
    }
    return result->known ? 0 : -1;
}
