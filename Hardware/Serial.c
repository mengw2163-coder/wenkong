#include "stm32f10x.h"
#include <stdio.h>
#include <stdarg.h>

/**
  * 函    数：串口初始化（USART1，PA9 发送，115200）
  */
void Serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);

	USART_Cmd(USART1, ENABLE);
}

/**
  * 函    数：串口发送一个字节
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

/**
  * 函    数：串口发送一个数组
  */
void Serial_SendArray(uint8_t *Array, uint16_t Length)
{
	uint16_t i;
	for (i = 0; i < Length; i++)
	{
		Serial_SendByte(Array[i]);
	}
}

/**
  * 函    数：串口发送一个字符串
  */
void Serial_SendString(const char *String)
{
	uint8_t i;
	for (i = 0; String[i] != '\0'; i++)
	{
		Serial_SendByte(String[i]);
	}
}

/**
  * 函    数：次方函数（内部使用）
  */
uint32_t Serial_Pow(uint32_t X, uint32_t Y)
{
	uint32_t Result = 1;
	while (Y--)
	{
		Result *= X;
	}
	return Result;
}

/**
  * 函    数：串口发送数字
  */
void Serial_SendNumber(uint32_t Number, uint8_t Length)
{
	uint8_t i;
	for (i = 0; i < Length; i++)
	{
		Serial_SendByte(Number / Serial_Pow(10, Length - i - 1) % 10 + '0');
	}
}

/**
  * 函    数：printf 底层重定向
  */
int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);
	return ch;
}

/**
  * 函    数：封装的 printf
  */
void Serial_Printf(const char *format, ...)
{
	char String[100];
	va_list arg;
	va_start(arg, format);
	vsnprintf(String, sizeof(String), format, arg);
	va_end(arg);
	Serial_SendString(String);
}
