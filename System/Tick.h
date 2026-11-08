#ifndef __TICK_H
#define __TICK_H

#include "stm32f10x.h"

void Tick_Init(void);          /* 1 ms 定时基准（TIM2） */
uint32_t millis(void);         /* 返回开机以来的毫秒数 */
void Tick_Inc(void);           /* 中断中调用，累加毫秒 */

#endif
