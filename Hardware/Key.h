#ifndef __KEY_H
#define __KEY_H

void Key_Init(void);
uint8_t Key_Scan(void);			/* 非阻塞扫描：0 无按键，1 Key1，2 Key2 */

#endif
