#include "stm32f10x.h"      
#include "Delay.h"          // 延时函数（Delay_us / Delay_ms / Delay_s）
#include "OLED.h"           // OLED 显示屏驱动（软件 I2C，PB8/PB9）
#include "LED.h"            // LED 驱动（PA1 报警灯 / PA2 状态灯）
#include "Key.h"            // 按键驱动（PB1 / PB11）
#include "DS18B20.h"        // DS18B20 温度传感器驱动（PB10，1-Wire）
#include "Buzzer.h"         // 蜂鸣器驱动（PB0）
#include "Serial.h"         // 串口驱动（USART1 / PA9，115200，用于无线串口模块）
#include "DAC_Output.h"     // PWM 电压输出驱动（PA6 / TIM3_CH1，0~3.3V）
#include "Tick.h"           // 毫秒时基（TIM2 中断，提供 millis()）
#include "PWM.h"            // PWM 输出（TIM2 通道 3 / PA2）

/*==================== 参数配置 ====================*/
#define TEMP_LOW_MIN     0.0f      // 温度下限最小值
#define TEMP_LOW_MAX     59.0f     // 温度下限最大值（必须小于上限）
#define TEMP_HIGH_MIN    1.0f      // 温度上限最小值
#define TEMP_HIGH_MAX    60.0f     // 温度上限最大值
#define TEMP_MIN_GAP     1.0f      // 上下限最小间隔（防止区间无效）
#define TEMP_HYST        0.5f      // 报警回差（防止温度在阈值附近抖动时反复报警）
#define REPORT_INTERVAL  1000      // 串口上报间隔（毫秒），每 1 秒发一帧
#define DISPLAY_INTERVAL 300       // OLED 刷新间隔（毫秒）
#define LED_BLINK_MS     1000      // 状态灯闪烁间隔（毫秒），改成 2000 就是 2 秒闪一次
#define BEEP_INTERVAL    200       // 报警时蜂鸣器/LED 的切换间隔（毫秒）
#define KEY_DEBOUNCE_MS  10        // 按键去抖时间（毫秒）
#define KEY_LONG_MS      800       // 长按判定阈值（毫秒）
#define KEY_REPEAT_MS    150       // 长按后连续触发的间隔（毫秒）

/*==================== 全局变量 ====================*/
// 文件级 static：只在本文件内可见（别的文件 extern 也连不上），避免被外部乱改。
// g_temp 是 DS18B20 实测温度（一直变）；g_tempLow / g_tempHigh 是按键设定的报警阈值
// （由用户设定），报警时用实测值与阈值比较，两者不是一回事。
static float   g_tempLow  = 20.0f;   // 当前设定的温度下限
static float   g_tempHigh = 60.0f;   // 当前设定的温度上限
static float   g_temp     = 0.0f;    // 最新一次测到的温度
static uint8_t g_tempValid = 0;      // 温度是否有效：0 无效 / 1 有效
static uint8_t g_alarm    = 0;       // 报警状态：0 正常 / 1 报警
static uint8_t g_setField = 0;       // 设置模式：0 监测 / 1 设下限 / 2 设上限
static uint8_t g_led     = 0;       // 状态灯当前状态：0 灭 / 1 亮（供串口上报）

/*==================== 函数原型 ====================*/
static void Show_Temp(uint8_t Line, uint8_t Col, float t);   // 显示温度（1 位小数）
static void Show_Volt(uint8_t Line, uint8_t Col, float v);   // 显示电压（2 位小数）
static void Show_Int(uint8_t Line, uint8_t Col, int16_t v);  // 显示整数
static void Show_Main(void);                                 // 主界面
static void Show_Set(void);                                  // 设置界面
static uint8_t Key_GetEvent(void);                           // 读取按键事件
static void Limit_Params(void);                              // 参数限幅
static void Temp_Task(void);                                 // 温度采集任务
static void Alarm_Task(void);                                // 报警判断任务
static void Display_Task(void);                              // 显示任务
static void Serial_Report_Task(void);                        // 串口上报任务
static void Alarm_Output(void);                              // 报警声光输出
static void Blink_Task(void);                               // 状态灯闪烁任务

/*==================== 显示辅助 ====================*/
/**
  * @brief  显示温度（1 位小数），如 25.3 / -5.3
  * @param  Line 行号（1~4）
  * @param  Col  起始列（1~16）
  * @param  t    温度值
  */
static void Show_Temp(uint8_t Line, uint8_t Col, float t)
{
	int16_t t10, ip, dp;             // t10=温度×10；ip=整数部分；dp=小数部分
	uint8_t col = Col;               // 当前列位置（边显示边右移）

	if (t < -50.0f)                  // 小于 -50 认为是传感器错误
	{
		OLED_ShowString(Line, Col, "--.-");   // 显示错误占位
		return;                      // 直接返回
	}
	t10 = (int16_t)(t * 10.0f + (t >= 0 ? 0.5f : -0.5f));  // 乘 10 并四舍五入
	ip = t10 / 10;                   // 取整数部分
	dp = t10 % 10;                   // 取小数部分
	if (dp < 0) dp = -dp;            // 小数部分取绝对值

	if (ip < 0)                      // 整数部分为负
	{
		OLED_ShowChar(Line, col, '-'); // 显示负号
		col++;                       // 列右移一位
		ip = -ip;                    // 整数部分取绝对值
	}
	if (ip >= 10)                    // 整数部分是两位数
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 2);  // 显示两位
		col += 2;                    // 列右移两位
	}
	else                             // 整数部分是一位数
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 1);  // 显示一位
		col += 1;                    // 列右移一位
	}
	OLED_ShowChar(Line, col, '.');   // 显示小数点
	col++;                           // 列右移一位
	OLED_ShowNum(Line, col, (uint32_t)dp, 1);  // 显示小数位
}

/**
  * @brief  显示电压（2 位小数），如 1.65
  */
static void Show_Volt(uint8_t Line, uint8_t Col, float v)
{
	int16_t v100, ip, dp;            // v100=电压×100；ip=整数部分；dp=小数部分
	uint8_t col = Col;               // 当前列位置

	if (v < 0.0f) v = 0.0f;          // 负电压按 0 处理
	v100 = (int16_t)(v * 100.0f + 0.5f);  // 乘 100 并四舍五入
	ip = v100 / 100;                 // 整数部分
	dp = v100 % 100;                 // 小数部分（两位）

	if (ip >= 10)                    // 整数部分两位数
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 2);
		col += 2;
	}
	else                             // 整数部分一位数
	{
		OLED_ShowNum(Line, col, (uint32_t)ip, 1);
		col += 1;
	}
	OLED_ShowChar(Line, col, '.');   // 小数点
	col++;
	OLED_ShowNum(Line, col, (uint32_t)dp, 2);  // 小数两位
}

/**
  * @brief  显示整数（不足两位不补零）
  */
static void Show_Int(uint8_t Line, uint8_t Col, int16_t v)
{
	if (v < 0) v = 0;                // 负数按 0 处理
	if (v >= 10)                     // 两位数
	{
		OLED_ShowNum(Line, Col, (uint32_t)v, 2);
	}
	else                             // 一位数
	{
		OLED_ShowNum(Line, Col, (uint32_t)v, 1);
	}
}

/*==================== 显示界面 ====================*/
// 主界面：第 1 行温度，第 2 行阈值，第 3 行电压，第 4 行状态
static void Show_Main(void)
{
	OLED_ShowString(1, 1, "T:");     // 第 1 行：温度标签
	if (g_tempValid)                 // 温度有效
	{
		Show_Temp(1, 3, g_temp);     // 显示实时温度
	}
	else                             // 温度无效
	{
		OLED_ShowString(1, 3, "--.-");  // 显示占位
	}
	OLED_ShowString(1, 8, "C");      // 单位 ℃ 的 C

	OLED_ShowString(2, 1, "L:");     // 第 2 行：下限标签
	Show_Int(2, 3, (int16_t)g_tempLow);   // 显示温度下限
	OLED_ShowString(2, 6, "H:");     // 上限标签
	Show_Int(2, 8, (int16_t)g_tempHigh);  // 显示温度上限

	OLED_ShowString(3, 1, "V:");     // 第 3 行：电压标签
	if (g_tempValid)                 // 温度有效
	{
		Show_Volt(3, 3, DAC_Output_GetVoltage(g_temp));  // 显示对应输出电压
	}
	else
	{
		OLED_ShowString(3, 3, "--.--");  // 占位
	}
	OLED_ShowString(3, 7, "V");      // 单位 V

	if (g_alarm)                     // 报警中
	{
		OLED_ShowString(4, 1, "ALARM!");
	}
	else                             // 正常
	{
		OLED_ShowString(4, 1, "READY");
	}
}

// 设置界面：显示温度与上下限，第 4 行提示当前在设哪一项
static void Show_Set(void)
{
	OLED_ShowString(1, 1, "T:");     // 温度标签
	if (g_tempValid)
	{
		Show_Temp(1, 3, g_temp);     // 实时温度
	}
	else
	{
		OLED_ShowString(1, 3, "--.-");
	}
	OLED_ShowString(1, 8, "C");

	OLED_ShowString(2, 1, "L:");     // 下限标签
	Show_Int(2, 3, (int16_t)g_tempLow);   // 下限值
	OLED_ShowString(2, 6, "H:");     // 上限标签
	Show_Int(2, 8, (int16_t)g_tempHigh);  // 上限值

	if (g_setField == 1)             // 正在设下限
	{
		OLED_ShowString(4, 1, "SET LOW");
	}
	else                             // 正在设上限
	{
		OLED_ShowString(4, 1, "SET HIGH");
	}
}

/*==================== 按键事件 ====================*/
#define KEY_EVT_NONE   0             // 没有按键事件
#define KEY1_SHORT     1             // 按键 1 短按
#define KEY2_SHORT     2             // 按键 2 短按
#define KEY2_LONG      3             // 按键 2 长按（含连续触发）

/**
  * @brief  读取按键事件（带消抖与长按识别）
  * @retval 0 无；1 Key1 短按；2 Key2 短按；3 Key2 长按
  */
static uint8_t Key_GetEvent(void)
{
	static uint8_t stableState = 0;  // 消抖后的稳定状态（0 无 / 1 K1 / 2 K2）
	static uint8_t lastStable = 0;   // 上一次的稳定状态（用于检测沿）
	static uint32_t changeMs = 0;    // 状态变化时刻（用于去抖计时）
	static uint32_t pressMs = 0;     // 按下时刻（用于判断长按）
	static uint32_t repeatMs = 0;    // 上次长按触发时刻（用于连续触发）
	static uint8_t longFired = 0;    // 长按是否已触发过（防止一次按下触发两次）
	uint8_t raw = Key_Scan();        // 读取原始按键状态
	uint8_t evt = KEY_EVT_NONE;      // 本次事件，默认无

	if (raw != stableState)          // 状态发生变化
	{
		stableState = raw;           // 记录新状态
		changeMs = millis();         // 重新开始去抖计时
		return KEY_EVT_NONE;         // 变化当期不产生事件
	}
	if (millis() - changeMs < KEY_DEBOUNCE_MS)  // 还没过去抖时间
	{
		return KEY_EVT_NONE;         // 丢弃
	}

	if (stableState != 0)            // 当前有按键按下
	{
		if (lastStable == 0)         // 从无到有：刚按下
		{
			pressMs = millis();      // 记录按下时刻
			longFired = 0;           // 清除长按标志
		}
		else if ((stableState == 2) && (!longFired) && (millis() - pressMs >= KEY_LONG_MS))
		{                            // 按键 2 按住超过长按阈值
			longFired = 1;           // 标记已触发长按
			repeatMs = millis();     // 记录本次触发时刻
			evt = KEY2_LONG;         // 产生长按事件
		}
		else if (stableState == 2 && longFired && millis() - repeatMs >= KEY_REPEAT_MS)
		{                            // 长按持续：每隔 repeat 时间连续触发
			repeatMs = millis();     // 更新触发时刻
			evt = KEY2_LONG;         // 继续产生长按事件（实现连续减）
		}
	}
	else if (lastStable != 0)        // 从有到无：刚松手
	{
		if (lastStable == 1)         // 松手前是按键 1
		{
			evt = KEY1_SHORT;        // 产生短按事件
		}
		else if (lastStable == 2 && !longFired)  // 松手前是按键 2 且没触发过长按
		{
			evt = KEY2_SHORT;        // 产生短按事件
		}
	}

	lastStable = stableState;        // 更新上一次状态
	return evt;                      // 返回本次事件
}
//只要状态一变，就重新计时 10ms。机械按键按下/松开的瞬间会在几毫秒内反复跳变，每次跳变都把计时清零；等连续 10ms 状态都不再变化，才认为这次变化是「真的」。
/*==================== 参数限幅 ====================*/
// 限幅（clamp）：把上下限各自夹在 [MIN, MAX] 合法区间内，再保证上限 ≥ 下限 + 最小间隔。
// 注意：这里的 g_tempLow / g_tempHigh 是用户设定的报警阈值，不是传感器实测温度。
static void Limit_Params(void)
{
	if (g_tempLow < TEMP_LOW_MIN)  g_tempLow = TEMP_LOW_MIN;    // 下限不得低于最小值
	if (g_tempLow > TEMP_LOW_MAX)  g_tempLow = TEMP_LOW_MAX;    // 下限不得高于最大值
	if (g_tempHigh < TEMP_HIGH_MIN) g_tempHigh = TEMP_HIGH_MIN; // 上限不得低于最小值
	if (g_tempHigh > TEMP_HIGH_MAX) g_tempHigh = TEMP_HIGH_MAX; // 上限不得高于最大值

	if (g_tempHigh < g_tempLow + TEMP_MIN_GAP)   // 上限必须比下限大至少一个间隔
	{
		g_tempHigh = g_tempLow + TEMP_MIN_GAP;   // 先抬高上限
		if (g_tempHigh > TEMP_HIGH_MAX)          // 抬高后超出最大值
		{
			g_tempHigh = TEMP_HIGH_MAX;          // 上限取最大值
			g_tempLow = g_tempHigh - TEMP_MIN_GAP;  // 再反向压低下限
		}
	}
}

/*==================== 任务：温度采集 ====================*/
// 状态机：先启动转换，等 750ms 后再读取（DS18B20 转换需要时间）。
// lastMs / state 用 static：函数返回后仍保留，把「启动→等待→读取」拆到多圈循环里完成；
// 若用普通局部变量，每次进来 state 都归 0，只会反复发启动命令，永远读不到温度。
static void Temp_Task(void)
{
	static uint32_t lastMs = 0;      // 启动转换的时刻（static：跨调用保存）
	static uint8_t state = 0;        // 0 启动转换；1 等待读取（static：跨调用保存）

	if (state == 0)                  // 需要启动一次转换
	{
		DS18B20_Start();             // 发送启动转换命令
		state = 1;                   // 进入等待状态
		lastMs = millis();           // 记录启动时刻
	}
	else if (millis() - lastMs >= 750)  // 已等待 750ms（12 位分辨率最长转换时间）
	{
		float t = DS18B20_Get_Temp();   // 读取温度值
		if (t > -50.0f )              // 读数有效（-50到100认为是有效温度）
		{
			g_temp = t;              // 保存温度
			g_tempValid = 1;         // 标记有效
		}
		else                         // 读数无效（传感器未接/故障）
		{
			g_tempValid = 0;         // 标记无效：显示 --.-，且不参与报警判断
		}
		state = 0;                   // 回到启动状态，开始下一次转换
	}
}

/*==================== 任务：报警判断 ====================*/
// 触发条件：温度 > 上限 或 < 下限；解除条件：回到回差区间内
static void Alarm_Task(void)
{
	if (!g_tempValid)                // 还没有效温度
	{
		g_alarm = 0;                 // 不报警
		return;                      // 直接返回
	}

	if (g_alarm == 0)                // 当前未报警：判断是否触发
	{
		if (g_temp > g_tempHigh || g_temp < g_tempLow)  // 温度越限
		{
			g_alarm = 1;             // 触发报警
		}
	}
	else                             // 当前已报警：判断是否解除
	{
		if (g_temp <= g_tempHigh - TEMP_HYST && g_temp >= g_tempLow + TEMP_HYST)
		{
			g_alarm = 0;             // 回到回差区间内，解除报警
		}
	}
}

/*==================== 任务：显示 ====================*/
// 每隔 DISPLAY_INTERVAL 刷新一次 OLED
static void Display_Task(void)
{
	static uint32_t last = 0;        // 上次刷新时刻

	if (millis() - last >= DISPLAY_INTERVAL)  // 到达刷新时间
	{
		last = millis();             // 更新刷新时刻
		if (g_setField == 0)         // 非设置模式
		{
			Show_Main();             // 显示主界面
		}
		else                         // 设置模式
		{
			Show_Set();              // 显示设置界面
		}
	}
}

/*==================== 任务：串口上报（无线串口模块） ====================*/
// 每 1 秒通过串口发送一帧：T:温度,A:报警,TL:下限,TH:上限,V:电压
static void Serial_Report_Task(void)
{
	static uint32_t last = 0;        // 上次上报时刻
	int16_t t10, tl, th, v100;       // t10=温度×10；tl/th=上下限；v100=电压×100

	if (millis() - last < REPORT_INTERVAL)  // 还没到上报时间
	{
		return;                      // 直接返回
	}
	last = millis();                 // 更新上报时刻

	t10  = (int16_t)(g_temp * 10.0f + (g_temp >= 0 ? 0.5f : -0.5f));  // 温度放大 10 倍
	tl   = (int16_t)g_tempLow;       // 温度下限
	th   = (int16_t)g_tempHigh;      // 温度上限
	v100 = (int16_t)(DAC_Output_GetVoltage(g_temp) * 100.0f + 0.5f);  // 电压放大 100 倍

	Serial_Printf(" T:%d.%d,A:%d,TL:%d,TH:%d,V:%d.%02d,L:%d\r\n",  // 发送一帧文本
	              t10 / 10, (t10 < 0 ? -t10 : t10) % 10,     // 温度的整数与小数
	              g_alarm, tl, th, v100 / 100, (v100 < 0 ? -v100 : v100) % 100, g_led);  // 报警/阈值/电压/灯状态
}

/*==================== 报警输出 ====================*/
// 报警时蜂鸣器与 LED1 每 200ms 翻转一次（鸣叫 + 闪烁）
static void Alarm_Output(void)
{
	static uint32_t lastBeep = 0;    // 上次翻转时刻

	if (g_alarm)                     // 报警中
	{
		if (millis() - lastBeep >= BEEP_INTERVAL)  // 到达翻转时间
		{
			lastBeep = millis();     // 更新时刻
			Buzzer_Turn();           // 蜂鸣器翻转
			LED1_Turn();             // 报警灯翻转
		}
	}
	else                             // 未报警
	{
		Buzzer_OFF();                // 关闭蜂鸣器
		LED1_OFF();                  // 关闭报警灯
	}
}


/*==================== 状态灯闪烁 ====================*/
// LED2 每隔 LED_BLINK_MS 切换一次亮/灭。
// 江协风格：用 LED2_ON() / LED2_OFF() 控制灯。
// 改闪烁快慢：把上面宏 LED_BLINK_MS 改成 1000（1 秒）或 2000（2 秒）。
// g_led 记录当前亮/灭，供串口上报（0 灭 / 1 亮）。
static void Blink_Task(void)
{
	static uint32_t last = 0;        // 上次切换时刻

	if (millis() - last >= LED_BLINK_MS)
	{
		last = millis();

		if (g_led == 0)
		{
			LED2_ON();               // 亮
			g_led = 1;
		}
		else
		{
			LED2_OFF();              // 灭
			g_led = 0;
		}
	}
}

/*==================== 主函数 ====================*/
int main(void)
{
	/*---- 外设初始化 ----*/
	LED_Init();          // 初始化 LED（PA1 / PA2）
	Key_Init();          // 初始化按键（PB1 / PB11，上拉输入）
	OLED_Init();         // 初始化 OLED（PB8/PB9 软件 I2C）
	DS18B20_Init();      // 初始化 DS18B20（PB10）
	Buzzer_Init();       // 初始化蜂鸣器（PB0，默认关闭）
	Serial_Init();       // 初始化串口（USART1 / PA9，115200，接无线串口模块）
	DAC_Output_Init();   // 初始化 PWM 电压输出（PA6 / TIM3_CH1）
	Tick_Init();         // 初始化毫秒时基（TIM2 中断，提供 millis()）
	// PWM 驱动保留备用；当前状态灯用 GPIO 闪烁，暂不初始化 PWM

	/*---- 开机画面 ----*/
	OLED_Clear();                          // 清屏
	OLED_ShowString(1, 1, "Temp Monitor"); // 显示标题
	OLED_ShowString(2, 1, "Init...");      // 显示初始化提示
	Delay_ms(500);                         // 停留 0.5 秒
	OLED_Clear();                          // 再次清屏，准备显示主界面

	/*---- 主循环 ----*/
	// 协作式调度：每个任务函数每次只执行一小步就返回，把 CPU 让给下一个任务；
	// 任务内部的 static 局部变量用于保存跨调用的状态与时刻（见各任务注释）。
	while (1)
	{
		Temp_Task();             // 采集温度（DS18B20）
		Alarm_Task();            // 判断是否报警
		Display_Task();          // 刷新 OLED 显示
		Serial_Report_Task();    // 通过串口上报数据

		/*---- 按键处理 ----*/
		switch (Key_GetEvent())  // 读取按键事件
		{
			case KEY1_SHORT:     // Key1 短按：在 正常/下限/上限 之间切换
				g_setField = (uint8_t)((g_setField + 1) % 3);  // 切换设置项
				break;

			case KEY2_SHORT:     // Key2 短按：当前项 +1
				if (g_setField == 1)         // 当前设下限
				{
					g_tempLow += 1.0f;       // 下限加 1
				}
				else if (g_setField == 2)    // 当前设上限
				{
					g_tempHigh += 1.0f;      // 上限加 1
				}
				Limit_Params();              // 限幅，保证范围合法
				break;

			case KEY2_LONG:      // Key2 长按：当前项 -1（连续减）
				if (g_setField == 1)         // 当前设下限
				{
					g_tempLow -= 1.0f;       // 下限减 1
				}
				else if (g_setField == 2)    // 当前设上限
				{
					g_tempHigh -= 1.0f;      // 上限减 1
				}
				Limit_Params();              // 限幅
				break;

			default:             // 无事件
				break;
		}

		/*---- 输出 ----*/
		DAC_Output_SetTemp(g_temp);  // 按当前温度更新 PWM 电压输出
		Alarm_Output();              // 报警声光输出
		Blink_Task();                // 状态灯闪烁
	}
}
