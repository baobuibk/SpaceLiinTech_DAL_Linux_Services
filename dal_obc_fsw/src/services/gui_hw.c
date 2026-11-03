#include "services/gui_hw.h"
#include "dal_obc_config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MIPI_LANE "/sys/bus/i2c/devices/1-0070/lane_switch/current_lane"
#define MIPI_SENSOR "/sys/bus/i2c/devices/1-0020/sensor_switch/current_sensor"
#define PWM_PATH "/sys/class/pwm/pwmchip0/pwm3"
#define MIPI_RAW_BYTES (5120LL * 3840LL * 2LL)

static bool valid_png(const char *path, struct stat *st)
{
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    unsigned char header[8];
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    ssize_t n = read(fd, header, sizeof(header));
    close(fd);
    return n == (ssize_t)sizeof(header) && memcmp(header, signature, sizeof(header)) == 0 &&
           stat(path, st) == 0 && S_ISREG(st->st_mode) && st->st_size > 8;
}

static const char *gpio_names[]={"pow_onoff_ai","pow_onoff_fpga",
    "usb_1_pwr_en","usb_2_pwr_en","usb_3_pwr_en","usb_4_pwr_en",
    "pow_mipicam_1","pow_mipicam_2","en_i2c_mipicam_1","en_i2c_mipicam_2",
    "sel_mipicam_on2_off1"};

static const char *setting(const char *key,const char *fallback)
{
    const char *v=getenv(key);return v && *v ? v : fallback;
}
static int number(const char *text,unsigned max,unsigned *out)
{
    if (!text || !*text) return -1;
    char *end;errno=0;unsigned long value=strtoul(text,&end,10);
    if (errno || *end || value>max) return -1;
    *out=(unsigned)value;return 0;
}
static int write_text(const char *path,const char *text)
{
    int fd=open(path,O_WRONLY|O_CLOEXEC);
    if (fd<0) return -1;
    size_t len=strlen(text);int result=write(fd,text,len)==(ssize_t)len ? 0 : -1;
    close(fd);return result;
}
static int read_int(const char *path,int *value)
{
    FILE *fp=fopen(path,"r");if (!fp) return -1;
    int result=fscanf(fp,"%d",value)==1?0:-1;fclose(fp);return result;
}
static int run(char *const argv[])
{
    pid_t pid=fork();if (pid<0) return -1;
    if (pid==0) {execvp(argv[0],argv);_exit(127);}
    int status;if (waitpid(pid,&status,0)<0) return -1;
    return WIFEXITED(status) && WEXITSTATUS(status)==0 ? 0 : -1;
}
static int gpio_set(const char *verb,const char *name)
{
    bool known=false;
    for (size_t i=0;i<sizeof(gpio_names)/sizeof(gpio_names[0]);i++)
        if (!strcmp(name,gpio_names[i])) known=true;
    if (!known || (strcmp(verb,"on") && strcmp(verb,"off"))) return -1;
    char path[PATH_MAX];
    const char *root=setting("DAL_GUI_LED_ROOT","/sys/class/leds");
    if (snprintf(path,sizeof(path),"%s/%s/brightness",root,name)>=(int)sizeof(path)) return -1;
    int target=!strcmp(verb,"on"),actual=-1;
    if (write_text(path,target?"1":"0")) {
        fprintf(stderr,"GPIO %s %s: cannot write %s: %s\n",name,verb,path,strerror(errno));
        return -1;
    }
    if (read_int(path,&actual) || actual!=target) {
        fprintf(stderr,"GPIO %s %s: readback failed at %s\n",name,verb,path);
        return -1;
    }
    return 0;
}
static int hardware_lock(void)
{
    const char *path=setting("DAL_GUI_MIPI_LOCK","/tmp/obc-experimentd-hardware.lock");
    int fd=open(path,O_RDWR|O_CREAT|O_CLOEXEC,0644);
    if (fd<0 || flock(fd,LOCK_EX|LOCK_NB)!=0) {
        fprintf(stderr,"MIPI hardware busy or lock unavailable\n");if (fd>=0) close(fd);return -1;
    }
    return fd;
}
static int mipi_names(unsigned board,const char **power,const char **i2c,const char **other_i2c,
                      const char **selector)
{
    if (board==1) {*power="pow_mipicam_1";*i2c="en_i2c_mipicam_1";
        *other_i2c="en_i2c_mipicam_2";*selector="off";return 0;}
    if (board==2) {*power="pow_mipicam_2";*i2c="en_i2c_mipicam_2";
        *other_i2c="en_i2c_mipicam_1";*selector="on";return 0;}
    return -1;
}
static int mipi_route(unsigned board,unsigned sensor)
{
    const char *power,*i2c,*other,*selector;
    if (sensor>3 || mipi_names(board,&power,&i2c,&other,&selector)!=0) return -1;
    if (gpio_set("off",other) || gpio_set("on",power) ||
        gpio_set(selector,"sel_mipicam_on2_off1") || gpio_set("on",i2c)) return -1;
    sleep(1);
    char value[3];snprintf(value,sizeof(value),"%u",sensor);
    const char *sensor_path=setting("DAL_GUI_MIPI_SENSOR",MIPI_SENSOR);
    const char *lane_path=setting("DAL_GUI_MIPI_LANE",MIPI_LANE);
    /* These drivers may report EIO after switching; readback decides success. */
    (void)write_text(sensor_path,value);(void)write_text(lane_path,value);
    usleep(100000);
    int lane=-1,mux=-1;
    if (read_int(lane_path,&lane)!=0 || read_int(sensor_path,&mux)!=0 ||
        lane!=(int)sensor || mux!=(int)sensor) {
        fprintf(stderr,"MIPI route readback mismatch: lane=%d sensor=%d\n",lane,mux);return -1;
    }
    printf("MIPI camera %u sensor %u selected\n",board,sensor);
    return 0;
}
static int camera_led(unsigned duty)
{
    const char *pwm=setting("DAL_GUI_CAMERA_LED_PWM",PWM_PATH);
    char path[PATH_MAX],value[40];
    if (access(pwm,F_OK)!=0) {
        const char *last=strrchr(pwm,'/');
        if (!last || strncmp(last+1,"pwm",3)!=0) {
            fprintf(stderr,"CAMERA_LED PWM path unavailable: %s (set DAL_GUI_CAMERA_LED_PWM)\n",pwm);
            return -1;
        }
        unsigned channel;if (number(last+4,31,&channel)!=0) return -1;
        size_t parent=(size_t)(last-pwm);
        if (parent+8>=sizeof(path)) return -1;
        memcpy(path,pwm,parent);path[parent]=0;strcat(path,"/export");
        if (access(path,F_OK)!=0) {
            fprintf(stderr,"CAMERA_LED PWM export missing: %s (check PWM driver and DAL_GUI_CAMERA_LED_PWM)\n",path);
            return -1;
        }
        snprintf(value,sizeof(value),"%u",channel);
        if (write_text(path,value)!=0) {
            fprintf(stderr,"CAMERA_LED PWM export %s channel %u failed: %s\n",path,channel,strerror(errno));
            return -1;
        }
    }
    snprintf(path,sizeof(path),"%s/period",pwm);
    int period=0;if (read_int(path,&period)!=0 || period<=0) period=1000000;
    snprintf(path,sizeof(path),"%s/enable",pwm);
    if (write_text(path,"0")!=0) return -1;
    snprintf(path,sizeof(path),"%s/period",pwm);
    snprintf(value,sizeof(value),"%d",period);if (write_text(path,value)!=0) return -1;
    snprintf(path,sizeof(path),"%s/duty_cycle",pwm);
    snprintf(value,sizeof(value),"%u",(unsigned)((uint64_t)period*duty/100));
    if (write_text(path,value)!=0) return -1;
    snprintf(path,sizeof(path),"%s/enable",pwm);
    if (write_text(path,duty?"1":"0")!=0) return -1;
    printf("CAMERA_LED duty set to %u%%\n",duty);return 0;
}
int gui_hw_set_led(unsigned duty)
{
    return duty<=100 ? camera_led(duty) : -1;
}
int gui_hw_set_mipi_power(unsigned board, bool on)
{
    const char *power=NULL,*i2c=NULL,*other=NULL,*selector=NULL;
    if (mipi_names(board,&power,&i2c,&other,&selector)!=0) return -1;
    int lock=hardware_lock();if (lock<0) return -1;
    int result=on?gpio_set("on",power):(gpio_set("off",i2c)||gpio_set("off",power));
    close(lock);
    if (!result) printf("MIPI camera %u power %s\n",board,on?"ON":"OFF");
    return result;
}
int gui_hw_route_mipi(unsigned board,unsigned sensor)
{
    if (board<1 || board>2 || sensor>3) return -1;
    int lock=hardware_lock();if (lock<0) return -1;
    int result=mipi_route(board,sensor);
    close(lock);
    return result;
}
int gui_hw_capture_mipi(unsigned board,unsigned sensor,char *saved_path,
                        size_t path_capacity,uint64_t *saved_bytes)
{
    if (board<1 || board>2 || sensor>3) return -1;
    int lock=hardware_lock();if (lock<0) return -1;
    int result=mipi_route(board,sensor);
    char path[PATH_MAX]={0},png[PATH_MAX]={0},high[PATH_MAX]={0},low[PATH_MAX]={0};
    if (!result) {
        const char *directory=setting("DAL_GUI_MIPI_DATA_DIR",BEEIF_DATA_DIR "/images/mipi");
        if (!getenv("DAL_GUI_MIPI_DATA_DIR")) {
            if ((mkdir(BEEIF_DATA_DIR,0755)&&errno!=EEXIST) ||
                (mkdir(BEEIF_DATA_DIR "/images",0755)&&errno!=EEXIST)) result=-1;
        }
        if (!result && mkdir(directory,0755)&&errno!=EEXIST) result=-1;
        if (!result) {
            unsigned sequence;
            for (sequence=1;sequence<=999999;sequence++) {
                if (snprintf(path,sizeof(path),"%s/MIPI%u.%u_H_%06u.raw",
                             directory,board-1,sensor,sequence)>=(int)sizeof(path)) {result=-1;break;}
                if (snprintf(png,sizeof(png),"%s/MIPI%u.%u_H_%06u.png",
                             directory,board-1,sensor,sequence)>=(int)sizeof(png) ||
                    snprintf(high,sizeof(high),"%s/MIPI%u.%u_H_%06u_high.png",
                             directory,board-1,sensor,sequence)>=(int)sizeof(high) ||
                    snprintf(low,sizeof(low),"%s/MIPI%u.%u_H_%06u_low.png",
                             directory,board-1,sensor,sequence)>=(int)sizeof(low)) {result=-1;break;}
                if (access(path,F_OK)!=0 && errno==ENOENT &&
                    access(png,F_OK)!=0 && errno==ENOENT &&
                    access(high,F_OK)!=0 && errno==ENOENT &&
                    access(low,F_OK)!=0 && errno==ENOENT) break;
            }
            if (sequence>999999) result=-1;
        }
        if (!result) {
            char *const args[]={"bash",(char *)setting("DAL_GUI_MIPI_CAPTURE","/home/root/tools/camera_tools/capture_ar2020.sh"),path,NULL};
            result=run(args);
            struct stat st;
            if (result || stat(path,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size!=MIPI_RAW_BYTES) {
                unlink(path);fprintf(stderr,"MIPI capture failed or incomplete RAW frame\n");result=-1;
            } else {
                printf("Saved %s (%ld bytes)\n",path,(long)st.st_size);
                const char *converter=setting("DAL_GUI_MIPI_PNG","/home/root/tools/bin/raw_png");
                char *const png_args[]={(char *)converter,"-H","3840","-W","5120",
                    "-b","grbg","-o",path,path,NULL};
                if (run(png_args)!=0 || !valid_png(high,&st) || rename(high,png)!=0) {
                    unlink(high);unlink(low);
                    fprintf(stderr,"MIPI PNG conversion failed; RAW retained at %s\n",path);
                    result=-1;
                } else {
                    unlink(low);
                    if (saved_path && path_capacity) snprintf(saved_path,path_capacity,"%s",png);
                    if (saved_bytes) *saved_bytes=(uint64_t)st.st_size;
                    printf("Saved %s (%ld bytes)\n",png,(long)st.st_size);
                }
            }
        }
    }
    close(lock);
    return result;
}
int gui_hw_main(int argc,char **argv)
{
    if (argc<3 || strcmp(argv[1],"--gui-hw")) return 2;
    unsigned board,sensor;
    if (!strcmp(argv[2],"gpio-list") && argc==3) {
        const char *root=setting("DAL_GUI_LED_ROOT","/sys/class/leds");
        for (size_t i=0;i<sizeof(gpio_names)/sizeof(gpio_names[0]);i++) {
            char path[PATH_MAX];int value;
            if (snprintf(path,sizeof(path),"%s/%s/brightness",root,gpio_names[i])>=(int)sizeof(path))
                return 1;
            if (read_int(path,&value)) printf("%-25s UNAVAILABLE\n",gpio_names[i]);
            else printf("%-25s %s\n",gpio_names[i],value?"ON":"OFF");
        }
        return 0;
    }
    if (!strcmp(argv[2],"gpio") && argc==5) {
        int lock=-1;
        if (!strcmp(argv[3],"pow_onoff_fpga")) {
            lock=open(setting("DAL_GUI_FPGA_LOCK","/run/lock/dal-fpga-prog.lock"),
                      O_RDWR|O_CREAT|O_CLOEXEC,0644);
            if (lock<0 || flock(lock,LOCK_EX|LOCK_NB)!=0) {
                fprintf(stderr,"FPGA programming lock busy or unavailable\n");
                if (lock>=0) close(lock);
                return 1;
            }
        }
        int result=gpio_set(argv[4],argv[3]);
        if (lock>=0) close(lock);
        if (!result) printf("%s %s\n",argv[3],argv[4]);
        return result?1:0;
    }
    if (!strcmp(argv[2],"camera-led") && argc==4 && number(argv[3],100,&sensor)==0)
        return gui_hw_set_led(sensor)==0?0:1;
    if (!strcmp(argv[2],"mipi-power") && argc==5 && number(argv[3],2,&board)==0 && board>=1 &&
        (!strcmp(argv[4],"on") || !strcmp(argv[4],"off"))) {
        return gui_hw_set_mipi_power(board,!strcmp(argv[4],"on"))==0?0:1;
    }
    if ((!strcmp(argv[2],"mipi-route") || !strcmp(argv[2],"mipi-capture")) && argc==5 &&
        number(argv[3],2,&board)==0 && board>=1 && number(argv[4],3,&sensor)==0) {
        return !strcmp(argv[2],"mipi-capture") ?
            (gui_hw_capture_mipi(board,sensor,NULL,0,NULL)==0?0:1) :
            (gui_hw_route_mipi(board,sensor)==0?0:1);
    }
    fprintf(stderr,"Usage: dal_obc_fsw --gui-hw gpio-list | gpio NAME on|off | camera-led 0..100 | mipi-power 1..2 on|off | mipi-route 1..2 0..3 | mipi-capture 1..2 0..3\n");
    return 2;
}
