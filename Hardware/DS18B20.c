#include "stm32f10x.h"
#include "Delay.h"

/* 引脚配置：PB10（DQ），开漏输出 + 外部 4.7k 上拉，免反复切换输入/输出模式 */
#define DS18B20_DQ_GPIO   GPIOB
#define DS18B20_DQ_PIN    GPIO_Pin_10

/**
  * @brief  复位 DS18B20 并检测存在脉冲
  * @retval 0 存在；1 不存在
  */
static uint8_t DS18B20_Rst(void)
{
	uint16_t timeout = 0;

	GPIO_ResetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);		//拉低 480us 以上
	Delay_us(500);
	GPIO_SetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);			//释放总线
	Delay_us(60);											//等待 15~60us 后器件拉低

	while (GPIO_ReadInputDataBit(DS18B20_DQ_GPIO, DS18B20_DQ_PIN) == 1 && timeout < 300)
	{
		Delay_us(1);
		timeout++;
	}
	if (timeout >= 300)										//超时未检测到存在脉冲
	{
		return 1;
	}
	Delay_us(420);											//等待存在脉冲结束
	return 0;
}

/**
  * @brief  初始化 DS18B20
  * @retval 0 成功；1 失败
  */
uint8_t DS18B20_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);	//开启 GPIOB 时钟

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_OD;			//开漏输出，需外部上拉
	GPIO_InitStructure.GPIO_Pin = DS18B20_DQ_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(DS18B20_DQ_GPIO, &GPIO_InitStructure);

	GPIO_SetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);			//默认释放总线

	return DS18B20_Rst();
}

/**
  * @brief  向 DS18B20 写一个字节
  */
void DS18B20_Write_Byte(uint8_t byte)
{
	uint8_t i;

	for (i = 0; i < 8; i++)
	{
		GPIO_ResetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);	//拉低开始写时隙
		Delay_us(2);										//写 1：1~15us 后释放
		if (byte & 0x01)
		{
			GPIO_SetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);
		}
		Delay_us(60);										//写时隙至少 60us
		GPIO_SetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);		//释放总线
		Delay_us(2);
		byte >>= 1;
	}
}

/**
  * @brief  从 DS18B20 读一个字节
  */
uint8_t DS18B20_Read_Byte(void)
{
	uint8_t i;
	uint8_t byte = 0;

	for (i = 0; i < 8; i++)
	{
		GPIO_ResetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);	//拉低开始读时隙
		Delay_us(2);
		GPIO_SetBits(DS18B20_DQ_GPIO, DS18B20_DQ_PIN);		//释放总线
		Delay_us(10);										//等待器件输出数据
		byte >>= 1;
		if (GPIO_ReadInputDataBit(DS18B20_DQ_GPIO, DS18B20_DQ_PIN) == 1)
		{
			byte |= 0x80;
		}
		Delay_us(50);										//等待读时隙结束
	}
	return byte;
}

/**
  * @brief  启动温度转换（需等待最多 750ms 再读取）
  */
void DS18B20_Start(void)
{
	if (DS18B20_Rst() == 1)
	{
		return;
	}
	DS18B20_Write_Byte(0xCC);								//跳过 ROM（单器件）
	DS18B20_Write_Byte(0x44);								//启动温度转换
}

/**
  * @brief  读取温度值
  * @retval 温度（℃），失败返回 -100
  */
float DS18B20_Get_Temp(void)
{
	uint8_t lsb, msb;
	int16_t raw;

	if (DS18B20_Rst() == 1)
	{
		return -100.0f;
	}
	DS18B20_Write_Byte(0xCC);								//跳过 ROM
	DS18B20_Write_Byte(0xBE);								//读暂存器
	lsb = DS18B20_Read_Byte();
	msb = DS18B20_Read_Byte();
	raw = (int16_t)((msb << 8) | lsb);

	return (float)raw * 0.0625f;							//12 位分辨率，最低位 0.0625℃
}
