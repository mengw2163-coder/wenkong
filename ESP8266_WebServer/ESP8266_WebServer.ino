/*
 * 智能温度监测报警器 —— 远程接收端（ESP8266）
 *
 * 功能：开 WiFi AP，提供网页显示温度 / 报警 / 阈值 / 电压
 * 硬件：ESP8266（ESP-01S / NodeMCU 等）
 * 接线：ESP8266 RX(GPIO3) ← STM32 PA9(TX)
 *       ESP8266 TX(GPIO1) → STM32 PA10(RX)（可选）
 *       3.3V / GND
 * 使用：烧录后，手机/电脑连接 WiFi "TempMonitor"（密码 12345678），
 *       浏览器打开 http://192.168.4.1
 *
 * STM32 → ESP8266 数据格式（串口 115200）：
 *   T:25.3,A:0,TL:20,TH:60,V:1.65\r\n
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char *AP_SSID = "TempMonitor";
const char *AP_PASS = "12345678";

ESP8266WebServer server(80);

/* 上报数据 */
struct ReportData
{
    float    temp = 0.0f;
    int      alarm = 0;
    int      tempLow = 20;
    int      tempHigh = 60;
    float    volt = 0.0f;
    uint32_t lastUpdate = 0;
} g_data;

/*==================== 网页 ====================*/
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>智能温度监测</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, "PingFang SC", "Microsoft YaHei", sans-serif;
    background: #f0f2f5; color: #1f2937; min-height: 100vh;
    display: flex; flex-direction: column; align-items: center; padding: 24px 16px;
    transition: background .3s;
  }
  body.alarm { background: #fee2e2; }
  h1 { font-size: 20px; font-weight: 600; margin-bottom: 16px; }
  .card {
    background: #fff; border-radius: 16px; padding: 24px;
    width: 100%; max-width: 360px; box-shadow: 0 2px 12px rgba(0,0,0,.08);
  }
  .temp { font-size: 56px; font-weight: 700; text-align: center; line-height: 1.1; }
  .temp small { font-size: 20px; font-weight: 500; color: #6b7280; }
  .status {
    text-align: center; margin: 12px 0; padding: 8px;
    border-radius: 999px; font-size: 15px; font-weight: 600;
    background: #dcfce7; color: #15803d; transition: all .3s;
  }
  body.alarm .status { background: #ef4444; color: #fff; animation: blink 1s infinite; }
  @keyframes blink { 50% { opacity: .35; } }
  .row { display: flex; justify-content: space-between; padding: 10px 4px;
         border-bottom: 1px solid #f3f4f6; font-size: 14px; }
  .row:last-child { border-bottom: none; }
  .row .label { color: #6b7280; }
  .row .value { font-weight: 600; }
  .tips { margin-top: 14px; font-size: 12px; color: #9ca3af; text-align: center; }
</style>
</head>
<body>
  <h1>智能温度监测报警器</h1>
  <div class="card">
    <div class="temp"><span id="temp">--.-</span><small> ℃</small></div>
    <div class="status" id="status">连接中…</div>
    <div class="row"><span class="label">温度下限</span><span class="value" id="tl">-- ℃</span></div>
    <div class="row"><span class="label">温度上限</span><span class="value" id="th">-- ℃</span></div>
    <div class="row"><span class="label">输出电压</span><span class="value" id="volt">-- V</span></div>
  </div>
  <div class="tips">每 1 秒自动刷新 · ESP8266 接收端</div>
<script>
function setText(id, text) { document.getElementById(id).innerText = text; }
function update() {
  fetch('/data')
    .then(function (r) { return r.json(); })
    .then(function (d) {
      setText('temp', d.temp.toFixed(1));
      setText('tl', d.tl + ' ℃');
      setText('th', d.th + ' ℃');
      setText('volt', d.volt.toFixed(2) + ' V');
      if (d.alarm) {
        setText('status', '⚠ 报警中');
        document.body.classList.add('alarm');
      } else {
        setText('status', '● 正常');
        document.body.classList.remove('alarm');
      }
    })
    .catch(function () {
      setText('status', '连接中断');
      document.body.classList.remove('alarm');
    });
}
setInterval(update, 1000);
update();
</script>
</body>
</html>
)rawliteral";

/*==================== 解析 STM32 上报数据 ====================*/
static void parseLine(const String &line)
{
    if (line.length() < 2 || line[0] != 'T')
    {
        return;
    }

    int idx = 0;
    while (idx < line.length())
    {
        int colon = line.indexOf(':', idx);
        if (colon < 0)
        {
            break;
        }
        String key = line.substring(idx, colon);
        int next = line.indexOf(',', colon);
        if (next < 0)
        {
            next = line.length();
        }
        String val = line.substring(colon + 1, next);

        if (key == "T")       g_data.temp = val.toFloat();
        else if (key == "A")  g_data.alarm = val.toInt();
        else if (key == "TL") g_data.tempLow = val.toInt();
        else if (key == "TH") g_data.tempHigh = val.toInt();
        else if (key == "V")  g_data.volt = val.toFloat();

        idx = next + 1;
    }
    g_data.lastUpdate = millis();
}

static void readSerial(void)
{
    static String line;

    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            if (line.length() > 0)
            {
                parseLine(line);
                line = "";
            }
        }
        else
        {
            line += c;
        }
    }
}

/*==================== HTTP 处理 ====================*/
static void handleRoot()
{
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void handleData()
{
    String json = "{";
    json += "\"temp\":"  + String(g_data.temp, 1) + ",";
    json += "\"alarm\":" + String(g_data.alarm) + ",";
    json += "\"tl\":"    + String(g_data.tempLow) + ",";
    json += "\"th\":"    + String(g_data.tempHigh) + ",";
    json += "\"volt\":"  + String(g_data.volt, 2);
    json += "}";
    server.send(200, "application/json", json);
}

static void handleNotFound()
{
    server.send(404, "text/plain", "Not Found");
}

/*==================== 初始化 ====================*/
void setup()
{
    Serial.begin(115200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.onNotFound(handleNotFound);
    server.begin();
}

void loop()
{
    server.handleClient();
    readSerial();
}
