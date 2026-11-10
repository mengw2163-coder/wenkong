#include "stm32f10x.h"                  // Device header

/**
  * 函    数：按键初始化
  * 参    数：无
  * 返 回 值：无
  */
void Key_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);		//开启GPIOB的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);						//将PB1和PB11引脚初始化为上拉输入
}

/**
  * @brief  按键扫描（非阻塞）
  * @retval 0 无按键；1 Key1 按下；2 Key2 按下
  */
uint8_t Key_Scan(void)
{
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)			//读 PB1，低电平表示按下
	{
		return 1;
	}
	if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)			//读 PB11
	{
		return 2;
	}
	return 0;
}
