#include "stm32f10x.h"
#include "USART.h"

#define USART1_RX_BUF_SIZE   128

static uint8_t  s_rxBuf[USART1_RX_BUF_SIZE];
static volatile uint16_t s_rxHead = 0;
static volatile uint16_t s_rxTail = 0;

/**
  * @brief  USART1 初始化（PA9 TX / PA10 RX），接 USB-TTL 连电脑
  */
void USART1_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;			//TX 复用推挽输出
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);【【=【-

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;	//RX 浮空输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_Init 
  * @brief  发送一个字节
  */
void USART1_SendByte(uint8_t byte)
{
	USART_SendData(USART1, byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);	//等最后一个字节发完
}

/**
  * @brief  发送字符串
  */
void USART1_SendString(const char *str)
{
	while (*str != '\0')
	{
		USART1_SendByte((uint8_t)*str);
		str++;
	}
}

/**
  * @brief  接收缓冲区是否有数据
  */
uint8_t USART1_RxAvailable(void)
{
	return (uint8_t)(s_rxHead != s_rxTail);
}

/**
  * @brief  读取一个接收字节
  */
uint8_t USART1_GetRxByte(void)
{
	uint8_t data;

	if (s_rxHead == s_rxTail)
	{
		return 0;
	}
	data = s_rxBuf[s_rxTail];
	s_rxTail = (uint16_t)((s_rxTail + 1) % USART1_RX_BUF_SIZE);
	return data;
}

/**
  * @brief  接收中断处理（在 USART1_IRQHandler 中调用）
  */
void USART1_RxHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
	{
		uint8_t data = (uint8_t)USART_ReceiveData(USART1);
		uint16_t next = (uint16_t)((s_rxHead + 1) % USART1_RX_BUF_SIZE);

		if (next != s_rxTail)							//缓冲区未满则写入
		{
			s_rxBuf[s_rxHead] = data;
			s_rxHead = next;
		}
	}

	/* 溢出错误：清 ORE，否则中断会一直触发 */
	if (USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET)
	{
		(void)USART_ReceiveData(USART1);
	}
}
