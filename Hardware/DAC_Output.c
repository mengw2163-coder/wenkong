#include "stm32f10x.h"
#include "DAC_Output.h"

/* PWM：PA6 / TIM3_CH1，20 kHz，ARR = 3599 */
#define DAC_PWM_GPIO    GPIOA
#define DAC_PWM_PIN     GPIO_Pin_6
#define DAC_PWM_ARR     3599
#define DAC_V_REF       3.3f

/* 映射参数：温度 [tLow, tHigh] → 电压 [vLow, vHigh] */
static float s_tLow  = 0.0f;
static float s_tHigh = 60.0f;
static float s_vLow  = 0.0f;
static float s_vHigh = 3.3f;

/**
  * @brief  PWM 电压输出初始化
  */
void DAC_Output_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;			//复用推挽输出
	GPIO_InitStructure.GPIO_Pin = DAC_PWM_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DAC_PWM_GPIO, &GPIO_InitStructure);

	TIM_TimeBaseStructure.TIM_Period = DAC_PWM_ARR;			//72MHz / 3600 = 20kHz
	TIM_TimeBaseStructure.TIM_Prescaler = 0;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_Pulse = 0;
	TIM_OC1Init(TIM3, &TIM_OCInitStructure);

	TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM3, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
}

/**
  * @brief  设置温度到电压的线性映射
  */
void DAC_Output_SetMap(float tLow, float tHigh, float vLow, float vHigh)
{
	s_tLow = tLow;
	s_tHigh = tHigh;
	s_vLow = vLow;
	s_vHigh = vHigh;
}

/**
  * @brief  计算温度对应的输出电压
  */
float DAC_Output_GetVoltage(float temp)
{
	float ratio;

	if (s_tHigh <= s_tLow)
	{
		return 0.0f;
	}
	if (temp < s_tLow)  temp = s_tLow;
	if (temp > s_tHigh) temp = s_tHigh;

	ratio = (temp - s_tLow) / (s_tHigh - s_tLow);
	return s_vLow + ratio * (s_vHigh - s_vLow);
}

/**
  * @brief  按当前温度更新 PWM 输出电压
  */
void DAC_Output_SetTemp(float temp)
{
	float vout = DAC_Output_GetVoltage(temp);

	if (vout < 0.0f) vout = 0.0f;
	if (vout > DAC_V_REF) vout = DAC_V_REF;

	TIM_SetCompare1(TIM3, (uint32_t)(vout / DAC_V_REF * (DAC_PWM_ARR + 1)));
}
