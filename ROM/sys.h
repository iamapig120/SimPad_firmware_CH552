#ifndef __SYS_H
#define __SYS_H

#include <stdint.h>
#include <stdbool.h>

#define	 FREQ_SYS       24000000	        //系统主频24MHz
#define  OSC_EN_XT      0                   //外部晶振使能，默认开启内部晶振
#define  MARCO_MAX      256                 //单个宏指令大小
#define  SYS_CFG_A      0x0160              //系统配置存储起始地址（xRAM），需确认USB占用后的结束地址

void	sysClockConfig();                   //时钟选择和配置
void	delay_us(uint16_t n);               //以uS为单位延时
void	delay(uint16_t n);                  //以mS为单位延时

void    sysTickConfig();

typedef enum { KeyNone = 0, KeyDown = 1, KeyUp = 2, KeyPress = 3 } KeyMode;

typedef struct {
    KeyMode mode;
    bool marco;
    uint8_t code;
    uint8_t program[MARCO_MAX];
    uint16_t length;
} KeyConfig;

typedef struct {
    bool marco;
    uint32_t color;
    uint8_t program[MARCO_MAX];
    uint16_t length;
} LEDConfig;

typedef struct {
    KeyConfig keyCofig[KEY_COUNT];
    LEDConfig ledCofig[LED_COUNT];
} SysConfig;

extern SysConfig sysConfig;
void sysLoadConfig();

#endif
