#include "stm32f10x.h"                  // Device header

/**
  * @brief  呼吸灯初始化（TIM2 通道 3 → PA2，PWM 输出）
  * @note   TIM2 已被 Tick 用作 1ms 时基，这里复用其通道 3；
  *         ARR = 999，故 PWM 周期 1ms，占空比分辨率 1/1000。
  */
void BreathLED_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);	

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;			
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse       = 0;				// 初始占空比 0
	TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
	TIM_OC3Init(TIM2, &TIM_OCInitStructure);
	TIM_OC3PreloadConfig(TIM2, TIM_OCPreload_Enable);
}


void BreathLED_Set(uint8_t duty)
{
	if (duty > 100) duty = 100;
	TIM_SetCompare3(TIM2, (uint16_t)(duty * 10));	// ARR=999，100% 对应比较值 1000
}
