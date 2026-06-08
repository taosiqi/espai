# espai

这是一个基于 Waveshare ESP32-S3-RLCD-4.2 的 Arduino IDE 项目，用来在 RLCD 屏幕上显示当前 Mac Codex 账号的 5h/7d 剩余额度。界面是黑白屏上的 Notion 风网页仪表盘，并保留 RTC、温湿度、蓝牙连接和电池状态小组件。

## Arduino IDE 使用

用 Arduino IDE 打开这个文件：

```text
/Users/taosiqi/Desktop/project/espai/espai.ino
```

建议板卡参数：

- Board: ESP32S3 Dev Module
- USB CDC On Boot: Enabled
- Flash Size: 16MB
- PSRAM: OPI PSRAM
- Upload Speed: 921600
- Serial Monitor: 115200

官方文档要求 ESP32-S3-RLCD-4.2 使用 Arduino-ESP32 v3.3.0 以上版本。库文件需要安装：

- U8g2
- SensorLib
- ArduinoJson

## Mac 服务

Mac 本地使用原生 `EspaiBleBridge.app` 读取 Codex session log 里的 quota 快照，再通过 BLE 写给 ESP32-S3。板子默认不连公司 Wi-Fi，而是广播 BLE 设备 `espai-s3`。

### 启动方式

安装后台 App：

```sh
cd mac-service
./install-swift-ble-app.command
```

第一次运行时 macOS 可能会请求蓝牙权限，需要允许 `EspaiBleBridge` 使用蓝牙。安装完成后可以关闭脚本窗口；后台 App 会作为登录项自动启动，不需要常驻 Terminal 黑窗口。

```text
~/.codex/espai-ble-bridge-app.log
```

卸载后台 App：

```sh
cd mac-service
./uninstall-swift-ble-app.command
```

Swift App 会解析 `~/.codex/sessions/*.jsonl` 里的 `token_count.rate_limits`，不需要 Bun、HTTP 服务或 Python bridge。

## 蓝牙传输

ESP32-S3-RLCD-4.2 使用的是 ESP32-S3，默认方案是 BLE，不使用经典蓝牙串口 SPP。Mac 作为 BLE Central，ESP32-S3 作为 BLE Peripheral。

后台无窗口方式：

```sh
cd mac-service
./install-swift-ble-app.command
```

Swift BLE App 会扫描 `espai-s3`，连接后每 60 秒推送一次 quota JSON。

可用环境变量：

```text
ESPAI_BLE_POLL_SECONDS=60
```

BLE UUID：

```text
Service: d34d3b80-2e0b-4b7a-9d68-28db61b3d1a0
Quota RX: d34d3b81-2e0b-4b7a-9d68-28db61b3d1a0
```

写入协议是简单分块：

```text
BEGIN:<json字节数>
<json chunk 1>
<json chunk 2>
END
```

## 重置

长按右键（BOOT/GPIO0）5 秒会清空 ESP32 保存的旧配置并重启。当前默认是蓝牙模式，重启后会继续广播 `espai-s3`。

## 页面和按键

主界面固定 4 页：

- `用量`：5h 剩余额度大仪表、5h/7d 进度卡。
- `环境`：温度、湿度、公历年月日时分秒、老历日期。
- `电源`：电池估算百分比、电压、ADC 原始值。
- `番茄钟`：25 分钟专注计时，并提供 3 秒录音回放测试。
- `诊断`：Mac Host、实际请求 URL、HTTP 状态、数据源、刷新年龄。

按键：

- 左键 GP18 短按：上一页。
- 右键 BOOT 短按：下一页。
- 左键 GP18 长按：立即重绘当前状态。
- 右键 BOOT 长按 5 秒：清空配置并重启。

在 `番茄钟` 页，左键有专门操作：

- 左键 GP18 短按：开始/暂停番茄钟。
- 左键 GP18 长按：录音 3 秒并立即回放，用来测试麦克风和喇叭。

## 中文显示

当前仍使用 U8g2 直接画 UI，没有切到 LVGL。原因是这块 ST7305 单色 RLCD 本身比较特殊，LVGL 示例也需要额外的屏幕缓冲和中文字体配置；切过去不能自动解决中文缺字，反而会明显增加工程复杂度。

中文字体主要占 Flash，不会整套加载到 RAM。固件现在使用 `u8g2_font_wqy14_t_gb2312`，比之前的 `u8g2_font_wqy12_t_chinese2` 子集更大，但能覆盖常用中文，字号也更清楚。

## 时间校准

BLE bridge 每次推送 quota 时会顺带发送 Mac 当前时间，ESP32-S3 收到后写入 PCF85063 RTC。没有收到蓝牙数据时，设备继续使用 RTC 走时。

## 硬件

- MCU: ESP32-S3-WROOM-1-N16R8
- Flash: 16MB
- PSRAM: 8MB
- Display: 4.2 inch RLCD, 300 x 400, ST7305 over SPI
- Sensor: SHTC3
- RTC: PCF85063
- Audio codec: ES8311 speaker output + ES7210 microphone input

## 引脚

| 功能 | GPIO |
| --- | --- |
| RLCD SCK | 11 |
| RLCD MOSI | 12 |
| RLCD DC | 5 |
| RLCD CS | 40 |
| RLCD RST | 41 |
| I2C SCL | 14 |
| I2C SDA | 13 |
| BOOT 右键/重置键 | 0 |
| GP18 左键/刷新键 | 18 |
| Audio MCLK | 16 |
| Audio BCLK | 9 |
| Audio WS | 45 |
| Audio DIN | 10 |
| Audio DOUT | 8 |
| Audio PA | 46 |
| 电池 ADC | ADC1 CH3 |
| RTC I2C 地址 | 0x51 |
| SHTC3 I2C 地址 | 0x70 |

## 录音播放

项目复用了 Waveshare `07_Audio_Test` 示例里的 codec 驱动。板载/外接喇叭通过 ES8311 播放，麦克风通过 ES7210 录音；番茄钟页长按左键会录制约 3 秒 PCM 到 PSRAM，然后直接回放。番茄钟结束时也会播放一段短提示音。

如果使用 Waveshare 示例包里的本地库，它们在：

- `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/01_Arduino_Libraries/U8g2`
- `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/01_Arduino_Libraries/SensorLib`

## 资料

- 官方文档: https://docs.waveshare.net/ESP32-S3-RLCD-4.2/
- Arduino 开发说明: https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino/
- 示例来源: `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/02_Example/Arduino`
