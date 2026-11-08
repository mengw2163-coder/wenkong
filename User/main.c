#include "stm32f10x.h"

int main(void)
{
  RCC->APB2ENR =0X00000010;
	GPIOC->CRH = 0X00300000;
	GPIOC->ODR = 0X00002000;
	while (1)
	{
		
	}
}
