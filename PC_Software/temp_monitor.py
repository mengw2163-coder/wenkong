#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
智能温度监测报警器 —— 电脑接收端上位机（无线串口模块版）

通过串口读取无线串口模块转发的文本数据，实时显示温度、阈值、电压与报警状态。
报警时界面变红并发出提示音。

依赖：pyserial
     pip install pyserial
运行：python temp_monitor.py

串口协议（文本行，115200）：
  T:25.3,A:0,TL:20,TH:60,V:1.65
  T   温度（℃，1 位小数）
  A   报警状态（0 正常 / 1 报警）
  TL  温度下限
  TH  温度上限
  V   输出电压（V，2 位小数）
"""

import threading
import time
import tkinter as tk
from tkinter import ttk

import serial
import serial.tools.list_ports

try:
    import winsound
except ImportError:
    winsound = None


def parse_line(line):
    """解析一行文本数据，失败返回 None"""
    line = line.strip()
    if not line:
        return None
    data = {}
    for item in line.split(','):
        if ':' not in item:
            continue
        key, val = item.split(':', 1)
        data[key.strip()] = val.strip()
    if 'T' not in data:
        return None
    try:
        return {
            "temp": float(data.get("T", "0")),
            "alarm": int(float(data.get("A", "0"))),
            "tl": int(float(data.get("TL", "0"))),
            "th": int(float(data.get("TH", "0"))),
            "volt": float(data.get("V", "0")),
        }
    except ValueError:
        return None


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
        buf = ""
        while self.running:
            try:
                chunk = self.ser.read(64)
            except Exception:
                break
            if chunk:
                buf += chunk.decode("utf-8", errors="ignore")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    result = parse_line(line)
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
