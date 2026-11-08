#ifndef __NRF24L01_H
#define __NRF24L01_H

#include "stm32f10x.h"

#define NRF24L01_PAYLOAD   16      /* 负载长度（字节） */

uint8_t NRF24L01_Init(void);                   /* 初始化，返回 0 成功 */
void NRF24L01_Send(uint8_t *data, uint8_t len); /* 发送一帧 */
uint8_t NRF24L01_Test(void);                   /* 通信测试，返回 0 正常 */

#endif
