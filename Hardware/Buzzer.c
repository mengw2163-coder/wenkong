#include "stm32f10x.h"

/* 引脚配置：PB0 */
#define BUZZER_GPIO       GPIOB
#define BUZZER_PIN        GPIO_Pin_0

/* 蜂鸣器触发电平：1 高电平触发；若为低电平触发的模块，改为 0 */
#define BUZZER_ON_LEVEL   1

/**
  * @brief  蜂鸣器初始化
  */
void Buzzer_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);		//开启 GPIOB 时钟

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;			//推挽输出
	GPIO_InitStructure.GPIO_Pin = BUZZER_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(BUZZER_GPIO, &GPIO_InitStructure);

	GPIO_WriteBit(BUZZER_GPIO, BUZZER_PIN, (BitAction)(!BUZZER_ON_LEVEL));	//默认关闭
}

/**
  * @brief  蜂鸣器开启
  */
void Buzzer_ON(void)
{
	GPIO_WriteBit(BUZZER_GPIO, BUZZER_PIN, (BitAction)BUZZER_ON_LEVEL);
}

/**
  * @brief  蜂鸣器关闭
  */
void Buzzer_OFF(void)
{
	GPIO_WriteBit(BUZZER_GPIO, BUZZER_PIN, (BitAction)(!BUZZER_ON_LEVEL));
}

/**
  * @brief  蜂鸣器状态翻转
  */
void Buzzer_Turn(void)
{
	if (GPIO_ReadOutputDataBit(BUZZER_GPIO, BUZZER_PIN) == (BitAction)BUZZER_ON_LEVEL)
	{
		Buzzer_OFF();
	}
	else
	{
		Buzzer_ON();
	}
}
