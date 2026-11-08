#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"

void USART1_Init(void);                       /* 115200, 8N1, RX 中断 */
void USART1_SendByte(uint8_t byte);
void USART1_SendString(const char *str);
uint8_t USART1_RxAvailable(void);             /* 接收缓冲区是否有数据 */
uint8_t USART1_GetRxByte(void);               /* 读取一个接收字节 */
void USART1_RxHandler(void);                  /* 中断服务，供 it.c 调用 */

#endif
