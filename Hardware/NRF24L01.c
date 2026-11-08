#include "stm32f10x.h"
#include "Delay.h"
#include "NRF24L01.h"

/*
 * NRF24L01 驱动（SPI2）
 * 引脚：PB12 CSN / PB13 SCK / PB14 MISO / PB15 MOSI / PB6 CE
 * 频道：76（2476 MHz），1 Mbps，0 dBm，地址 5 字节，负载 16 字节
 * IRQ 引脚不接，使用状态寄存器轮询
 */

#define NRF_CE_GPIO    GPIOB
#define NRF_CE_PIN     GPIO_Pin_6
#define NRF_CSN_GPIO   GPIOB
#define NRF_CSN_PIN    GPIO_Pin_12

#define NRF_CHANNEL    76
#define NRF_PAYLOAD    16

static const uint8_t NRF_ADDR[5] = {0x12, 0x34, 0x56, 0x78, 0x9A};

/* 寄存器 */
#define NRF_REG_CONFIG      0x00
#define NRF_REG_EN_AA       0x01
#define NRF_REG_EN_RXADDR   0x02
#define NRF_REG_SETUP_AW    0x03
#define NRF_REG_SETUP_RETR  0x04
#define NRF_REG_RF_CH       0x05
#define NRF_REG_RF_SETUP    0x06
#define NRF_REG_STATUS      0x07
#define NRF_REG_TX_ADDR     0x10
#define NRF_REG_RX_ADDR_P0  0x0A
#define NRF_REG_RX_PW_P0    0x11

/* 命令 */
#define NRF_CMD_R_REGISTER   0x00
#define NRF_CMD_W_REGISTER   0x20
#define NRF_CMD_R_RX_PAYLOAD 0x61
#define NRF_CMD_W_TX_PAYLOAD 0xA0
#define NRF_CMD_FLUSH_TX     0xE1
#define NRF_CMD_FLUSH_RX     0xE2
#define NRF_CMD_NOP          0xFF

/* 状态位 */
#define NRF_ST_RX_DR   0x40
#define NRF_ST_TX_DS   0x20
#define NRF_ST_MAX_RT  0x10

static uint8_t SPI2_WriteByte(uint8_t byte)
{
	while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);
	SPI_I2S_SendData(SPI2, byte);
	while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET);
	return (uint8_t)SPI_I2S_ReceiveData(SPI2);
}

static uint8_t NRF_ReadReg(uint8_t reg)
{
	uint8_t value;

	GPIO_ResetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
	SPI2_WriteByte(NRF_CMD_R_REGISTER | (reg & 0x1F));
	value = SPI2_WriteByte(NRF_CMD_NOP);
	GPIO_SetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
	return value;
}

static void NRF_WriteReg(uint8_t reg, uint8_t value)
{
	GPIO_ResetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
	SPI2_WriteByte(NRF_CMD_W_REGISTER | (reg & 0x1F));
	SPI2_WriteByte(value);
	GPIO_SetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
}

static void NRF_WriteBuf(uint8_t reg, uint8_t *buf, uint8_t len)
{
	uint8_t i;

	GPIO_ResetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
	SPI2_WriteByte(reg);
	for (i = 0; i < len; i++)
	{
		SPI2_WriteByte(buf[i]);
	}
	GPIO_SetBits(NRF_CSN_GPIO, NRF_CSN_PIN);
}

/**
  * @brief  GPIO 与 SPI2 初始化
  */
static void NRF_PortInit(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	SPI_InitTypeDef SPI_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;		//CSN / CE 推挽输出
	GPIO_InitStructure.GPIO_Pin = NRF_CSN_PIN | NRF_CE_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;			//SCK / MOSI 复用推挽
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_15;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;	//MISO 浮空输入
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_Init(SPI2, &SPI_InitStructure);
	SPI_Cmd(SPI2, ENABLE);

	GPIO_SetBits(NRF_CSN_GPIO, NRF_CSN_PIN);				//CSN 拉高（空闲）
	GPIO_ResetBits(NRF_CE_GPIO, NRF_CE_PIN);				//CE 拉低（待机）
	Delay_ms(20);											//上电延时
}

/**
  * @brief  NRF24L01 初始化（发送模式）
  * @retval 0 成功；1 失败
  */
uint8_t NRF24L01_Init(void)
{
	NRF_PortInit();

	NRF_WriteReg(NRF_REG_EN_AA, 0x01);						//通道 0 自动应答
	NRF_WriteReg(NRF_REG_EN_RXADDR, 0x01);					//使能通道 0
	NRF_WriteReg(NRF_REG_SETUP_AW, 0x03);					//5 字节地址
	NRF_WriteReg(NRF_REG_SETUP_RETR, 0x1A);				//重发 10 次，间隔 500us
	NRF_WriteReg(NRF_REG_RF_CH, NRF_CHANNEL);				//频道 76
	NRF_WriteReg(NRF_REG_RF_SETUP, 0x07);					//1 Mbps，0 dBm
	NRF_WriteReg(NRF_REG_RX_PW_P0, NRF_PAYLOAD);			//负载 16 字节
	NRF_WriteBuf(NRF_REG_RX_ADDR_P0, (uint8_t *)NRF_ADDR, 5);
	NRF_WriteBuf(NRF_REG_TX_ADDR, (uint8_t *)NRF_ADDR, 5);
	NRF_WriteReg(NRF_REG_CONFIG, 0x0E);					//TX 模式，上电，CRC 2 字节
	Delay_ms(2);

	return NRF24L01_Test();
}

/**
  * @brief  NRF24L01 初始化（接收模式）
  * @retval 0 成功；1 失败
  */
uint8_t NRF24L01_InitRX(void)
{
	NRF_PortInit();

	NRF_WriteReg(NRF_REG_EN_AA, 0x01);						//通道 0 自动应答
	NRF_WriteReg(NRF_REG_EN_RXADDR, 0x01);					//使能接收通道 0
	NRF_WriteReg(NRF_REG_SETUP_AW, 0x03);					//5 字节地址
	NRF_WriteReg(NRF_REG_SETUP_RETR, 0x1A);
	NRF_WriteReg(NRF_REG_RF_CH, NRF_CHANNEL);
	NRF_WriteReg(NRF_REG_RF_SETUP, 0x07);					//1 Mbps，0 dBm
	NRF_WriteReg(NRF_REG_RX_PW_P0, NRF_PAYLOAD);			//负载 16 字节
	NRF_WriteBuf(NRF_REG_RX_ADDR_P0, (uint8_t *)NRF_ADDR, 5);
	NRF_WriteBuf(NRF_REG_TX_ADDR, (uint8_t *)NRF_ADDR, 5);	//自动应答也要 TX 地址
	NRF_WriteReg(NRF_REG_CONFIG, 0x0F);					//RX 模式，上电，CRC 2 字节
	Delay_ms(2);
	GPIO_SetBits(NRF_CE_GPIO, NRF_CE_PIN);					//CE 拉高，进入接收

	return NRF24L01_Test();
}

/**
  * @brief  通信测试：读回 CONFIG 寄存器
  * @retval 0 正常；1 异常
  */
uint8_t NRF24L01_Test(void)
{
	uint8_t cfg = NRF_ReadReg(NRF_REG_CONFIG);

	if (cfg == 0x0E || cfg == 0x0F)
	{
		return 0;
	}
	return 1;
}

/**
  * @brief  发送一帧数据
  */
void NRF24L01_Send(uint8_t *data, uint8_t len)
{
	uint16_t timeout = 0;

	if (len > NRF_PAYLOAD)
	{
		len = NRF_PAYLOAD;
	}

	NRF_WriteReg(NRF_CMD_FLUSH_TX, NRF_CMD_NOP);
	NRF_WriteReg(NRF_REG_STATUS, NRF_ST_RX_DR | NRF_ST_TX_DS | NRF_ST_MAX_RT);
	NRF_WriteBuf(NRF_CMD_W_TX_PAYLOAD, data, len);

	GPIO_SetBits(NRF_CE_GPIO, NRF_CE_PIN);					//CE 拉高，启动发送
	Delay_us(15);
	GPIO_ResetBits(NRF_CE_GPIO, NRF_CE_PIN);

	while ((NRF_ReadReg(NRF_REG_STATUS) & NRF_ST_TX_DS) == 0)	//等待发送完成
	{
		if (++timeout > 5000)
		{
			break;											//超时退出
		}
	}
	NRF_WriteReg(NRF_REG_STATUS, NRF_ST_TX_DS);				//清除标志
}

/**
  * @brief  接收一帧数据
  * @retval 1 收到数据；0 没有数据
  */
uint8_t NRF24L01_Receive(uint8_t *data, uint8_t len)
{
	uint8_t status;

	if (len > NRF_PAYLOAD)
	{
		len = NRF_PAYLOAD;
	}

	status = NRF_ReadReg(NRF_REG_STATUS);
	if ((status & NRF_ST_RX_DR) == 0)						//无接收数据
	{
		return 0;
	}

	GPIO_ResetBits(NRF_CSN_GPIO, NRF_CSN_PIN);				//读负载
	SPI2_WriteByte(NRF_CMD_R_RX_PAYLOAD);
	{
		uint8_t i;
		for (i = 0; i < len; i++)
		{
			data[i] = SPI2_WriteByte(NRF_CMD_NOP);
		}
	}
	GPIO_SetBits(NRF_CSN_GPIO, NRF_CSN_PIN);

	NRF_WriteReg(NRF_REG_STATUS, NRF_ST_RX_DR);				//清除接收标志
	NRF_WriteReg(NRF_CMD_FLUSH_RX, NRF_CMD_NOP);
	return 1;
}
