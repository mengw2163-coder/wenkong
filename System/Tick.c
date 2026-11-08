#include "stm32f10x.h"
#include "Tick.h"

static volatile uint32_t s_ms = 0;

/**
  * @brief  毫秒时基初始化（TIM2，1 ms 中断）
  */
void Tick_Init(void)
{
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	/* 72MHz / 72 / 1000 = 1kHz → 1ms */
	TIM_TimeBaseStructure.TIM_Period = 999;
	TIM_TimeBaseStructure.TIM_Prescaler = 71;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM2, ENABLE);
}

/**
  * @brief  返回开机以来的毫秒数
  */
uint32_t millis(void)
{
	return s_ms;
}

/**
  * @brief  中断中累加毫秒（在 TIM2_IRQHandler 中调用）
  */
void Tick_Inc(void)
{
	s_ms++;
}
