#ifndef __DAC_OUTPUT_H
#define __DAC_OUTPUT_H

#include "stm32f10x.h"

void DAC_Output_Init(void);                                   /* PWM 初始化（PA6 / TIM3_CH1） */
void DAC_Output_SetMap(float tLow, float tHigh,              /* 设置温度→电压映射（E2 预留） */
                       float vLow, float vHigh);
void DAC_Output_SetTemp(float temp);                          /* 按当前温度更新输出电压 */
float DAC_Output_GetVoltage(float temp);                      /* 计算当前温度对应的电压 */

#endif
