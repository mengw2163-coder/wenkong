#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
智能温度监测报警器 —— 电脑接收端上位机

通过串口读取 NRF24L01 接收器转发的数据，实时显示温度、阈值、电压与报警状态。
报警时界面变红并发出提示音。

依赖：pyserial
     pip install pyserial
运行：python temp_monitor.py

串口协议（16 字节二进制负载，与 STM32 端 NRF24L01 上报格式一致）：
  [0]    0xAA   帧头
  [1]    0x55   帧头
  [2-3]  int16  温度 × 10（小端）
  [4]    uint8  报警状态（0 正常 / 1 报警）
  [5-6]  int16  温度下限
  [7-8]  int16  温度上限
  [9-10] int16  输出电压 × 100（小端）
  [11]   uint8  校验和（[0..10] 异或）
  [12-15] 保留
"""

import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
import serial.tools.list_ports

try:
    import winsound
except ImportError:          # 非 Windows 平台没有 winsound
    winsound = None

PAYLOAD_LEN = 16


def to_int16(hi, lo):
    """两个字节（小端）转有符号 16 位整数"""
    v = lo | (hi << 8)
    return v - 0x10000 if v >= 0x8000 else v


def parse_packet(data):
    """解析 16 字节负载，失败返回 None"""
    if len(data) != PAYLOAD_LEN:
        return None
    if data[0] != 0xAA or data[1] != 0x55:
        return None

    checksum = 0
    for b in data[:11]:
        checksum ^= b
    if checksum != data[11]:
        return None

    return {
        "temp": to_int16(data[3], data[2]) / 10.0,
        "alarm": data[4],
        "tl": to_int16(data[6], data[5]),
        "th": to_int16(data[8], data[7]),
        "volt": to_int16(data[10], data[9]) / 100.0,
    }


class App:
    def __init__(self, root):
        self.root = root
        self.root.title("智能温度监测报警器 —— 接收端")
        self.root.geometry("380x440")
        self.ser = None
        self.running = False
        self.was_alarm = False

        top = ttk.Frame(root, padding=10)
        top.pack(fill="x")
        ttk.Label(top, text="串口").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(top, textvariable=self.port_var, width=10, state="readonly")
        self.port_combo.pack(side="left", padx=4)
        ttk.Button(top, text="刷新", command=self.refresh_ports, width=5).pack(side="left")

        ttk.Label(top, text="波特率").pack(side="left", padx=(10, 0))
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(top, textvariable=self.baud_var, width=7, state="readonly",
                     values=["9600", "19200", "38400", "57600", "115200"]).pack(side="left", padx=4)

        self.btn = ttk.Button(top, text="连接", command=self.toggle, width=6)
        self.btn.pack(side="left", padx=4)

        self.refresh_ports()

        self.card = ttk.Frame(root, padding=20)
        self.card.pack(fill="both", expand=True)

        self.temp_label = tk.Label(self.card, text="--.-", font=("Segoe UI", 52, "bold"), fg="#1f2937")
        self.temp_label.pack(pady=10)
        tk.Label(self.card, text="℃", font=("Segoe UI", 18), fg="#6b7280").pack()

        self.status_label = tk.Label(self.card, text="未连接", font=("Segoe UI", 16, "bold"),
                                     fg="#6b7280", bg="#f9fafb")
        self.status_label.pack(pady=8, fill="x")

        info = ttk.Frame(self.card)
        info.pack(pady=10)
        self.tl_label = tk.Label(info, text="温度下限：-- ℃", font=("Segoe UI", 12))
        self.tl_label.pack(anchor="w", pady=2)
        self.th_label = tk.Label(info, text="温度上限：-- ℃", font=("Segoe UI", 12))
        self.th_label.pack(anchor="w", pady=2)
        self.volt_label = tk.Label(info, text="输出电压：-- V", font=("Segoe UI", 12))
        self.volt_label.pack(anchor="w", pady=2)

        self.last_label = tk.Label(self.card, text="", font=("Segoe UI", 9), fg="#9ca3af")
        self.last_label.pack(pady=5)

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports
        if ports:
            self.port_var.set(ports[0])

    def toggle(self):
        if self.running:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        try:
            self.ser = serial.Serial(self.port_var.get(), int(self.baud_var.get()), timeout=0.2)
        except Exception as exc:
            self.status_label.config(text=f"打开失败：{exc}", fg="#b91c1c")
            return
        self.running = True
        self.btn.config(text="断开")
        self.status_label.config(text="已连接，等待数据…", fg="#1d4ed8", bg="#f9fafb")
        threading.Thread(target=self.read_loop, daemon=True).start()

    def disconnect(self):
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
            self.ser = None
        self.btn.config(text="连接")
        self.status_label.config(text="未连接", fg="#6b7280", bg="#f9fafb")
        self.root.configure(background="#f0f2f5")

    def read_loop(self):
        buf = bytearray()
        while self.running:
            try:
                chunk = self.ser.read(64)
            except Exception:
                break
            if chunk:
                buf.extend(chunk)
                while len(buf) >= PAYLOAD_LEN:
                    idx = -1
                    for i in range(len(buf) - 1):
                        if buf[i] == 0xAA and buf[i + 1] == 0x55:
                            idx = i
                            break
                    if idx < 0:
                        buf.clear()
                        break
                    if idx + PAYLOAD_LEN > len(buf):
                        del buf[:idx]
                        break
                    pkt = bytes(buf[idx:idx + PAYLOAD_LEN])
                    del buf[:idx + PAYLOAD_LEN]
                    result = parse_packet(pkt)
                    if result:
                        self.root.after(0, self.update_ui, result)
            time.sleep(0.05)

    def update_ui(self, d):
        self.temp_label.config(text=f"{d['temp']:.1f}")
        self.tl_label.config(text=f"温度下限：{d['tl']} ℃")
        self.th_label.config(text=f"温度上限：{d['th']} ℃")
        self.volt_label.config(text=f"输出电压：{d['volt']:.2f} V")
        self.last_label.config(text=f"最后更新：{time.strftime('%H:%M:%S')}")

        if d["alarm"]:
            self.status_label.config(text="⚠ 报警中", fg="#ffffff", bg="#ef4444")
            self.root.configure(background="#fee2e2")
            if not self.was_alarm and winsound:
                winsound.MessageBeep(winsound.MB_ICONEXCLAMATION)
            self.was_alarm = True
        else:
            self.status_label.config(text="● 正常", fg="#15803d", bg="#dcfce7")
            self.root.configure(background="#f0f2f5")
            self.was_alarm = False

    def on_close(self):
        self.disconnect()
        self.root.destroy()


def main():
    root = tk.Tk()
    app = App(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()


if __name__ == "__main__":
    main()
