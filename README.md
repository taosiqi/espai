# espai

这是一个基于 Waveshare ESP32-S3-RLCD-4.2 的 Arduino IDE 项目，用来在 RLCD 屏幕上显示当前 Mac Codex 账号的 5h/7d 剩余额度。界面是黑白屏上的 Notion 风网页仪表盘，并保留 RTC、温湿度、Wi-Fi 和电池状态小组件。

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

ESP32 不能直接读取 Mac 上的 Codex app-server，所以需要在 Mac 本地跑一个很小的 Bun 服务，把 Codex quota 转成 ESP32 可访问的 JSON。

安装 Bun：

```sh
curl -fsSL https://bun.sh/install | bash
```

安装后新开一个终端，或运行：

```sh
exec /bin/zsh
```

配置服务：

```sh
cd mac-service
cp .env.example .env
```

编辑 `.env`：

```text
ESPAI_HOST=0.0.0.0
ESPAI_PORT=8787
CODEX_BIN=/Applications/Codex.app/Contents/Resources/codex
```

### 启动方式

Bun 服务不一定要有 Terminal 黑窗口。黑窗口只代表服务在前台运行；如果改成 macOS 后台服务，ESP32 仍然可以访问接口，但桌面不会常驻黑窗口。

前台启动，适合调试：

```sh
bun run dev
```

这种方式会占用当前 Terminal 窗口，关掉窗口服务就会停止。

后台启动，适合日常使用：

```sh
bun run build
./install-background-service.command
```

后台服务由 launchd 管理，登录后自动启动，不需要保留 Terminal 窗口。安装脚本本身会短暂打开一个窗口显示结果，安装完成后可以关闭。

日志位置：

```text
~/.codex/espai-mac-service.out.log
~/.codex/espai-mac-service.err.log
```

卸载后台服务：

```sh
./uninstall-background-service.command
```

测试服务：

```sh
curl http://127.0.0.1:8787/api/status
```

服务启动时会打印可供 ESP32 配置页填写的 Mac IP 和完整 URL。每次读取数据时，也会在终端打印 quota 来源和 5h/7d 剩余百分比。

服务会调用本机 Codex app-server 的 `account/rateLimits/read`，优先读取 `rateLimitsByLimitId.codex`。如果 app-server 暂时拿不到数据，会回退解析 `~/.codex/sessions/*.jsonl` 里的 `token_count.rate_limits`。

## 首次配网

首次启动或清空配置后，屏幕会显示 `espai-setup` 和中文配网提示。

1. 手机连接 Wi-Fi：`espai-setup`
2. 浏览器打开：`http://192.168.4.1`
3. 填入 Wi-Fi 和 Mac IP
4. 保存后设备自动重启进入仪表模式

Mac IP 示例：

```text
192.168.1.10
```

ESP32 会自动请求：

```text
http://你的Mac局域网IP:8787/api/status
```

配置带版本号；如果设备里保存的是旧版 URL/token 配置，新固件会自动清空并重新进入配网页。

## 重置 Wi-Fi

长按右键（BOOT/GPIO0）5 秒会清空 ESP32 保存的 Wi-Fi/服务配置，并重启进入 `espai-setup` 配网模式。

## 页面和按键

主界面固定 4 页：

- `用量`：5h 剩余额度大仪表、5h/7d 进度卡。
- `环境`：温度、湿度、公历年月日时分秒、老历日期、Wi-Fi RSSI。
- `电源`：电池估算百分比、电压、ADC 原始值。
- `诊断`：Mac Host、实际请求 URL、HTTP 状态、数据源、刷新年龄。

按键：

- 左键 GP18 短按：上一页。
- 右键 BOOT 短按：下一页。
- 左键 GP18 长按：立即刷新 Codex quota。
- 右键 BOOT 长按 5 秒：清空配置并重启进入配网页。

## 中文显示

当前仍使用 U8g2 直接画 UI，没有切到 LVGL。原因是这块 ST7305 单色 RLCD 本身比较特殊，LVGL 示例也需要额外的屏幕缓冲和中文字体配置；切过去不能自动解决中文缺字，反而会明显增加工程复杂度。

中文字体主要占 Flash，不会整套加载到 RAM。固件现在使用 `u8g2_font_wqy14_t_gb2312`，比之前的 `u8g2_font_wqy12_t_chinese2` 子集更大，但能覆盖常用中文，字号也更清楚。

## 时间校准

设备连上 Wi-Fi 后会通过 NTP 校准北京时间，并写入 PCF85063 RTC；运行中每 6 小时会再次校准。离线时继续使用 RTC 走时，环境页会显示公历年月日时分秒和老历日期。

## 硬件

- MCU: ESP32-S3-WROOM-1-N16R8
- Flash: 16MB
- PSRAM: 8MB
- Display: 4.2 inch RLCD, 300 x 400, ST7305 over SPI
- Sensor: SHTC3
- RTC: PCF85063

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
| 电池 ADC | ADC1 CH3 |
| RTC I2C 地址 | 0x51 |
| SHTC3 I2C 地址 | 0x70 |

如果使用 Waveshare 示例包里的本地库，它们在：

- `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/01_Arduino_Libraries/U8g2`
- `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/01_Arduino_Libraries/SensorLib`

## 资料

- 官方文档: https://docs.waveshare.net/ESP32-S3-RLCD-4.2/
- Arduino 开发说明: https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Development-Environment-Setup-Arduino/
- 示例来源: `/Users/taosiqi/Downloads/ESP32-S3-RLCD-4.2-main/02_Example/Arduino`
