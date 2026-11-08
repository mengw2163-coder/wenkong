#ifndef __DS18B20_H
#define __DS18B20_H

#include "stm32f10x.h"

uint8_t DS18B20_Init(void);          /* 初始化 DS18B20，返回 0 表示成功 */
void DS18B20_Start(void);            /* 启动一次温度转换 */
float DS18B20_Get_Temp(void);        /* 读取温度（℃），失败返回 -100 */
void DS18B20_Write_Byte(uint8_t byte);
uint8_t DS18B20_Read_Byte(void);

#endif
