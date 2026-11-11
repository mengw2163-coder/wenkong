#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "LED.h"
#include "Key.h"
#include "DS18B20.h"
#include "Buzzer.h"
#include "USART.h"
#include "Serial.h"
#include "NRF24L01.h"
#include "DAC_Output.h"
#include "Tick.h"

/*==================== 角色配置 ====================*/
/* 同一工程两种角色：
   监测端（STM32 + 传感器 + 发送）      -> NRF_ROLE_MONITOR
   接收端（第二块开发板 + 接收 + 串口）  -> NRF_ROLE_RECEIVER
   下载到哪块板子就改成对应的角色 */
#define NRF_ROLE_MONITOR    0
#define NRF_ROLE_RECEIVER   1
#define NRF_ROLE            NRF_ROLE_MONITOR

#if (NRF_ROLE == NRF_ROLE_RECEIVER)

/*==================== 接收端：NRF24L01 接收 -> 串口 -> 电脑 ====================*/
int main(void)
{
	uint8_t data[NRF24L01_PAYLOAD];
	uint8_t i;

	USART1_Init();
	if (NRF24L01_InitRX() != 0)
	{
		USART1_SendString("NRF24L01 init failed\r\n");
	}

	while (1)
	{
		if (NRF24L01_Receive(data, NRF24L01_PAYLOAD))
		{
			for (i = 0; i < NRF24L01_PAYLOAD; i++)
			{
				USART1_SendByte(data[i]);
			}
		}
	}
}

#else

/*==================== 监测端 ====================*/

/*==================== 参数配置 ====================*/
#define TEMP_LOW_MIN     0.0f
#define TEMP_LOW_MAX     59.0f
#define TEMP_HIGH_MIN    1.0f
#define TEMP_HIGH_MAX    60.0f
#define TEMP_MIN_GAP     1.0f     /* 上下限最小间隔 */
#define TEMP_HYST        0.5f     /* 报警回差（防抖动） */
#define REPORT_INTERVAL  1000     /* 上报间隔（ms） */
#define DISPLAY_INTERVAL 300      /* 刷新间隔（ms） */
#define BEEP_INTERVAL    200      /* 报警声光切换间隔（ms） */
#define KEY_DEBOUNCE_MS  10       /* 按键去抖（ms） */
#define KEY_LONG_MS      800      /* 长按阈值（ms） */
#define KEY_REPEAT_MS    150      /* 长按重复间隔（ms） */

/*==================== 全局变量 ====================*/
static float   g_tempLow  = 20.0f;		//温度下限
static float   g_tempHigh = 60.0f;		//温度上限
static float   g_temp     = 0.0f;		//当前温度
static uint8_t g_tempValid = 0;			//0 还没拿到有效温度；1 已拿到
static uint8_t g_alarm    = 0;			//0 正常；1 报警
static uint8_t g_nrfOk    = 0;			//0 NRF24L01 初始化失败；1 正常
static uint8_t g_setField = 0;			//0 监测；1 设置下限；2 设置上限

/*==================== 函数原型 ====================*/
static void Show_Temp(uint8_t Line, uint8_t Col, float t);
static void Show_Volt(uint8_t Line, uint8_t Col, float v);
static void Show_Int(uint8_t Line, uint8_t Col, int16_t v);
static void Show_Main(void);
static void Show_Set(void);
static uint8_t Key_GetEvent(void);
static void Limit_Params(void);
static void Temp_Task(void);
static void Alarm_Task(void);
static void Display_Task(void);
//static void NRF24L01_Report_Task(void);
static void Serial_Report_Task(void);
static void Alarm_Output(void);
static void Status_LED(void);

/*==================== 显示辅助 ====================*/
/**
  * @brief  显示温度（1 位小数），如 25.3 / -5.3
  */
static void Show_Temp(uint8_t Line, uint8_t Col, float t)
{
	int16_t t10, ip, dp;
	uint8_t col = Col;

	if (t < -50.0f)
	{
		OLED_ShowString(Line, Col, "--.-");
		return;
	}
	t10 = (int16_t)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f));
	ip = t10 / 10;
	dp = t10 % 10;
	if (dp < 0) dp = -dp;

	if (ip < 0)
	{
		OLED_ShowChar(Line, col, '-');
		col++;
		ip = -ip;
	}
	if (ip >= 10)
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 2);
		col += 2;
	}
	else
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 1);
		col += 1;
	}
	OLED_ShowChar(Line, col, '.');
	col++;
	OLED_ShowNum(Line, col, (uint32_t)dp, 1);
}

/**
  * @brief  显示电压（2 位小数），如 1.65
  */
static void Show_Volt(uint8_t Line, uint8_t Col, float v)
{
	int16_t v100, ip, dp;
	uint8_t col = Col;

	if (v < 0.0f) v = 0.0f;
	v100 = (int16_t)(v * 100.0f + 0.5f);
	ip = v100 / 100;
	dp = v100 % 100;

	if (ip >= 10)
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 2);
		col += 2;
	}
	else
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 1);
		col += 1;
	}
	OLED_ShowChar(Line, col, '.');
	col++;
	OLED_ShowNum(Line, col, (uint32_t)dp, 2);
}

/**
  * @brief  显示整数
  */
static void Show_Int(uint8_t Line, uint8_t Col, int16_t v)
{
	if (v < 0) v = 0;
	if (v >= 10)
	{
		OLED_ShowNum(Line, Col, (uint32_t)v, 2);
	}
	else
	{
		OLED_ShowNum(Line, Col, (uint32_t)v, 1);
	}
}

/*==================== 显示界面 ====================*/
static void Show_Main(void)
{
	OLED_ShowString(1, 1, "T:");
	if (g_tempValid)
	{
		Show_Temp(1, 3, g_temp);
	}
	else
	{
		OLED_ShowString(1, 3, "--.-");
	}
	OLED_ShowString(1, 8, "C");

	OLED_ShowString(2, 1, "L:");
	Show_Int(2, 3, (int16_t)g_tempLow);
	OLED_ShowString(2, 6, "H:");
	Show_Int(2, 8, (int16_t)g_tempHigh);

	OLED_ShowString(3, 1, "V:");
	if (g_tempValid)
	{
		Show_Volt(3, 3, DAC_Output_GetVoltage(g_temp));
	}
	else
	{
		OLED_ShowString(3, 3, "--.--");
	}
	OLED_ShowString(3, 7, "V");

	if (g_alarm)
	{
		OLED_ShowString(4, 1, "ALARM!");
	}
	else if (!g_nrfOk)
	{
		OLED_ShowString(4, 1, "NRF ERR");
	}
	else
	{
		OLED_ShowString(4, 1, "READY");
	}
}

static void Show_Set(void)
{
	OLED_ShowString(1, 1, "T:");
	if (g_tempValid)
	{
		Show_Temp(1, 3, g_temp);
	}
	else
	{
		OLED_ShowString(1, 3, "--.-");
	}
	OLED_ShowString(1, 8, "C");

	OLED_ShowString(2, 1, "L:");
	Show_Int(2, 3, (int16_t)g_tempLow);
	OLED_ShowString(2, 6, "H:");
	Show_Int(2, 8, (int16_t)g_tempHigh);

	if (g_setField == 1)
	{
		OLED_ShowString(4, 1, "SET LOW");
	}
	else
	{
		OLED_ShowString(4, 1, "SET HIGH");
	}
}

/*==================== 按键事件 ====================*/
#define KEY_EVT_NONE   0
#define KEY1_SHORT     1
#define KEY2_SHORT     2
#define KEY2_LONG      3

/**
  * @brief  读取按键事件（带消抖与长按识别）
  * @retval 0 无；1 Key1 短按；2 Key2 短按；3 Key2 长按
  */
static uint8_t Key_GetEvent(void)
{
	static uint8_t stableState = 0;
	static uint8_t lastStable = 0;
	static uint32_t changeMs = 0;
	static uint32_t pressMs = 0;
	static uint32_t repeatMs = 0;
	static uint8_t longFired = 0;
	uint8_t raw = Key_Scan();
	uint8_t evt = KEY_EVT_NONE;

	if (raw != stableState)						//状态变化，重新计时去抖
	{
		stableState = raw;
		changeMs = millis();
		return KEY_EVT_NONE;
	}
	if (millis() - changeMs < KEY_DEBOUNCE_MS)
	{
		return KEY_EVT_NONE;
	}

	if (stableState != 0)
	{
		if (lastStable == 0)
		{
			pressMs = millis();
			longFired = 0;
		}
		else if (stableState == 2 && !longFired && millis() - pressMs >= KEY_LONG_MS)
		{
			longFired = 1;
			repeatMs = millis();
			evt = KEY2_LONG;
		}
		else if (stableState == 2 && longFired && millis() - repeatMs >= KEY_REPEAT_MS)
		{
			repeatMs = millis();
			evt = KEY2_LONG;
		}
	}
	else if (lastStable != 0)
	{
		if (lastStable == 1)
		{
			evt = KEY1_SHORT;
		}
		else if (lastStable == 2 && !longFired)
		{
			evt = KEY2_SHORT;
		}
	}

	lastStable = stableState;
	return evt;
}

/*==================== 参数限幅 ====================*/
static void Limit_Params(void)
{
	if (g_tempLow < TEMP_LOW_MIN)  g_tempLow = TEMP_LOW_MIN;
	if (g_tempLow > TEMP_LOW_MAX)  g_tempLow = TEMP_LOW_MAX;
	if (g_tempHigh < TEMP_HIGH_MIN) g_tempHigh = TEMP_HIGH_MIN;
	if (g_tempHigh > TEMP_HIGH_MAX) g_tempHigh = TEMP_HIGH_MAX;

	if (g_tempHigh < g_tempLow + TEMP_MIN_GAP)
	{
		g_tempHigh = g_tempLow + TEMP_MIN_GAP;
		if (g_tempHigh > TEMP_HIGH_MAX)
		{
			g_tempHigh = TEMP_HIGH_MAX;
			g_tempLow = g_tempHigh - TEMP_MIN_GAP;
		}
	}
}

/*==================== 任务：温度采集 ====================*/
static void Temp_Task(void)
{
	static uint32_t lastMs = 0;
	static uint8_t state = 0;					//0 启动转换；1 等待读取

	if (state == 0)
	{
		DS18B20_Start();
		state = 1;
		lastMs = millis();
	}
	else if (millis() - lastMs >= 750)			//12 位分辨率最长转换时间 750ms
	{
		float t = DS18B20_Get_Temp();
		if (t > -50.0f)
		{
			g_temp = t;
			g_tempValid = 1;
		}
		else
		{
			g_tempValid = 0;					//读取失败：显示 --.-，且不参与报警判断
		}
		state = 0;
	}
}

/*==================== 任务：报警判断 ====================*/
static void Alarm_Task(void)
{
	if (!g_tempValid)							//还没拿到有效温度，不判报警
	{
		g_alarm = 0;
		return;
	}

	if (g_alarm == 0)
	{
		if (g_temp > g_tempHigh || g_temp < g_tempLow)
		{
			g_alarm = 1;
		}
	}
	else
	{
		if (g_temp <= g_tempHigh - TEMP_HYST && g_temp >= g_tempLow + TEMP_HYST)
		{
			g_alarm = 0;
		}
	}
}

/*==================== 任务：显示 ====================*/
static void Display_Task(void)
{
	static uint32_t last = 0;

	if (millis() - last >= DISPLAY_INTERVAL)
	{
		last = millis();
		if (g_setField == 0)
		{
			Show_Main();
		}
		else
		{
			Show_Set();
		}
	}
}

/*==================== 任务：NRF24L01 上报（暂时停用，保留代码） ====================*/
#if 0
/*
 * 负载格式（16 字节，小端）：
 *  [0]    0xAA   帧头
 *  [1]    0x55   帧头
 *  [2-3]  int16  温度 × 10
 *  [4]    uint8  报警状态
 *  [5-6]  int16  温度下限
 *  [7-8]  int16  温度上限
 *  [9-10] int16  输出电压 × 100
 *  [11]   uint8  校验和（[0..10] 异或）
 *  [12-15] 保留
 */
static void NRF24L01_Report_Task(void)
{
	static uint32_t last = 0;
	uint8_t packet[NRF24L01_PAYLOAD];
	int16_t t10, tl, th, v100;
	uint8_t i, sum;

	if (millis() - last < REPORT_INTERVAL)
	{
		return;
	}
	last = millis();

	t10  = (int16_t)(g_temp * 10.0f + (g_temp >= 0 ? 0.5f : -0.5f));
	tl   = (int16_t)g_tempLow;
	th   = (int16_t)g_tempHigh;
	v100 = (int16_t)(DAC_Output_GetVoltage(g_temp) * 100.0f + 0.5f);

	packet[0]  = 0xAA;
	packet[1]  = 0x55;
	packet[2]  = (uint8_t)(t10 & 0xFF);
	packet[3]  = (uint8_t)((t10 >> 8) & 0xFF);
	packet[4]  = g_alarm;
	packet[5]  = (uint8_t)(tl & 0xFF);
	packet[6]  = (uint8_t)((tl >> 8) & 0xFF);
	packet[7]  = (uint8_t)(th & 0xFF);
	packet[8]  = (uint8_t)((th >> 8) & 0xFF);
	packet[9]  = (uint8_t)(v100 & 0xFF);
	packet[10] = (uint8_t)((v100 >> 8) & 0xFF);

	sum = 0;
	for (i = 0; i < 11; i++)
	{
		sum ^= packet[i];
	}
	packet[11] = sum;
	packet[12] = 0;
	packet[13] = 0;
	packet[14] = 0;
	packet[15] = 0;

	NRF24L01_Send(packet, NRF24L01_PAYLOAD);
}
#endif

/*==================== 任务：串口上报（无线串口模块） ====================*/
static void Serial_Report_Task(void)
{
	static uint32_t last = 0;
	int16_t t10, tl, th, v100;

	if (millis() - last < REPORT_INTERVAL)
	{
		return;
	}
	last = millis();

	t10  = (int16_t)(g_temp * 10.0f + (g_temp >= 0 ? 0.5f : -0.5f));
	tl   = (int16_t)g_tempLow;
	th   = (int16_t)g_tempHigh;
	v100 = (int16_t)(DAC_Output_GetVoltage(g_temp) * 100.0f + 0.5f);

	Serial_Printf("T:%d.%d,A:%d,TL:%d,TH:%d,V:%d.%02d\r\n",
	              t10 / 10, (t10 < 0 ? -t10 : t10) % 10,
	              g_alarm, tl, th, v100 / 100, (v100 < 0 ? -v100 : v100) % 100);
}

/*==================== 报警输出 ====================*/
static void Alarm_Output(void)
{
	static uint32_t lastBeep = 0;

	if (g_alarm)
	{
		if (millis() - lastBeep >= BEEP_INTERVAL)
		{
			lastBeep = millis();
			Buzzer_Turn();
			LED1_Turn();
		}
	}
	else
	{
		Buzzer_OFF();
		LED1_OFF();
	}
}

/*==================== 状态指示灯 ====================*/
static void Status_LED(void)
{
	static uint32_t last = 0;

	if (millis() - last >= 1000)
	{
		last = millis();
		LED2_Turn();
	}
}

/*==================== 主函数 ====================*/
int main(void)
{
	LED_Init();
	Key_Init();
	OLED_Init();
	DS18B20_Init();
	Buzzer_Init();
	//USART1_Init();                        // 旧串口（RX 命令），暂时停用
	Serial_Init();                          // 新串口（无线串口模块，TX）
	//g_nrfOk = (NRF24L01_Init() == 0);     // NRF24L01 暂时停用
	g_nrfOk = 1;
	DAC_Output_Init();
	Tick_Init();

	OLED_Clear();
	OLED_ShowString(1, 1, "Temp Monitor");
	OLED_ShowString(2, 1, "Init...");
	Delay_ms(500);
	OLED_Clear();

	while (1)
	{
		Temp_Task();
		Alarm_Task();
		Display_Task();
		//NRF24L01_Report_Task();
		Serial_Report_Task();                   // 无线串口模块上报

		switch (Key_GetEvent())
		{
			case KEY1_SHORT:					//Key1 短按：切换设置项
				g_setField = (uint8_t)((g_setField + 1) % 3);
				break;

			case KEY2_SHORT:					//Key2 短按：当前项 +1
				if (g_setField == 1)
				{
					g_tempLow += 1.0f;
				}
				else if (g_setField == 2)
				{
					g_tempHigh += 1.0f;
				}
				Limit_Params();
				break;

			case KEY2_LONG:						//Key2 长按：当前项 -1
				if (g_setField == 1)
				{
					g_tempLow -= 1.0f;
				}
				else if (g_setField == 2)
				{
					g_tempHigh -= 1.0f;
				}
				Limit_Params();
				break;

			default:
				break;
		}

		DAC_Output_SetTemp(g_temp);
		Alarm_Output();
		Status_LED();
	}
}

#endif /* NRF_ROLE */
