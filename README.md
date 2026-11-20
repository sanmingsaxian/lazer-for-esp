# Lazer Cardputer — ESP-IDF 原生重构版

将 Lazer Cardputer v1.3.0（Arduino/PlatformIO 固件）完整重构为**纯原生 ESP-IDF 工程**：不再依赖 Arduino core、M5GFX、ESP8266Audio、ArduinoJson。目标硬件不变：**M5Stack Cardputer v1.1 / Stamp-S3A**（ESP32-S3FN8，8MB Flash，240×135 ST7789V2）。

版本标识：`2.0.0-idf`。功能契约与 v1.3.0 对齐：本地 MP3 播放、LRC 歌词、嵌入式 WebUI、SD 文件管理、Web OTA、AP+STA Wi-Fi、网易云扫码/手机号登录与在线播放。

## 技术栈映射

| 原 Arduino 依赖 | 本重构实现 |
| --- | --- |
| M5GFX 显示/Canvas/中文字体 | esp_lcd ST7789 驱动 + LVGL 9.2（内置 MiSans 子集 C 字体 16px/10px） |
| M5Cardputer 键盘 | 自写 74HC138 矩阵扫描驱动（两级修饰键处理，与原版键值表一致） |
| ESP8266Audio / libmad | 直接移植 ESP8266Audio 同源 libmad（GPL，与 AGPL 兼容），自写帧同步/增量合成封装 |
| AudioOutputI2S | i2s_std 驱动 NS4168（BCLK41/LRCK43/DOUT42），DMA 16 段 |
| AudioFileSourceBuffer | 24KiB 环形缓冲 + 独立喂料任务（Core 0） |
| NeteaseAudioSource | esp_http_client Range 流式读取，重定向≤4、重连≤2、定位强制 206 |
| WebServer | esp_http_server，27 个 `/api/*` 路由逐条对齐，Basic Auth |
| HTTPClient + ArduinoJson | esp_http_client + cJSON（解析层独立成可主机测试的纯函数） |
| Preferences | nvs_flash，命名空间 `lazer` / `lazer-ncm`，键名与 Arduino 版字节兼容 |
| Update（OTA） | esp_ota_ops，双 OTA 分区各 3264KiB，镜像头与文件名校验 |
| SD / FS | sdspi_host + FatFS VFS（SPI2，25MHz），音频/文件操作互斥 |
| WiFi | esp_wifi AP+STA，8.5dBm 发射功率、最多 2 客户端（v1.1.1 功耗策略保留） |
| lgfx 二维码 | Nayuki qrcodegen（MIT），LVGL canvas 绘制 |

音频任务布局与原版一致：解码任务 Core 1、优先级 4、栈 20KB；看门狗与欠压保护保持开启。诊断日志保留：`BOOT_RESET_REASON` / `RESET_REASON` / `MP3_STARTED` / `NETEASE_STARTED` / `AUDIO_ALIVE`（10s 周期，含堆/栈水位）/ `MP3_MEMORY_ERROR`。

## 工程结构

```
lazer-idf/
├── CMakeLists.txt / partitions.csv / sdkconfig.defaults
├── main/                  app_main + LCD 诊断（Kconfig 选择）
├── components/
│   ├── board/             引脚、背光 PWM、电池 ADC、复位原因
│   ├── display/           esp_lcd + LVGL 显示胶水（MV|MX，gap 40/53）
│   ├── keyboard/          74HC138 矩阵扫描
│   ├── storage/           SD 挂载与容量
│   ├── library/           曲库扫描（80 首/两层）、元数据
│   ├── common/            平台无关纯逻辑（字符串/路径/MP3 帧头/ID3/LRC/队列）
│   ├── mp3dec/            libmad + 解码封装
│   ├── audio_out/         I2S 输出
│   ├── netease_audio/     HTTP 流源 + 24KiB 环形缓冲
│   ├── audio_engine/      播放状态机与解码任务
│   ├── lyrics/            双缓冲歌词存储
│   ├── settings/          NVS 设置与收藏
│   ├── netease/           网易云 API（cJSON 解析层可主机测试）
│   ├── wifi_mgr/          AP+STA 管理
│   ├── web/               esp_http_server + 内嵌 WebUI + multipart + OTA
│   ├── app/               键盘状态机（与原版 App.cpp 对齐）
│   ├── ui/                LVGL 界面
│   ├── fonts/             MiSans 子集 LVGL 字体
│   ├── lvgl/              LVGL 9.2.2（vendored）
│   └── qrcode/            qrcodegen（vendored）
├── test/host/             主机端单元测试（g++，无需硬件）
└── docs/                  重构设计文档与上游第三方声明
```

## 编译与烧录

需要 ESP-IDF **v5.5.x**（本工程按 v5.5.6 验证环境搭建）。

```bash
source $IDF_PATH/export.sh
cd lazer-idf
idf.py build
idf.py flash monitor
```

**注意**：若工程目录位于不支持符号链接/随机写入的挂载（如某些云盘 FUSE），请把构建目录放到本地磁盘：

```bash
idf.py -B /path/to/local/build build
```

分区表为 8MB Flash 双 OTA（`partitions.csv`）。首次烧录或从错误分区布局迁移时先 `idf.py erase-flash`。

### LCD 诊断固件

诊断固件是独立子工程 `lcd-test/`（复用共享组件，CMake 内排除播放器组件）：

```bash
cd lcd-test
idf.py -B /path/to/build-lcd-test build
idf.py -B /path/to/build-lcd-test flash monitor
```

诊断固件只点亮屏幕：八条色带、`LCD OK`、`Stamp-S3A / 240x135`、闪烁绿点；串口输出 `LCD_TEST_READY` / `LCD_TEST_ALIVE`。

### Web OTA

与原版一致：WebUI「固件」页上传**应用镜像**（构建产物 `lazer_cardputer.bin` 或 `firmware/` 中的 app 镜像），拒绝 merged/bootloader/partition/otadata。

**从 Arduino 版迁移**：本工程的 app 分区偏移为 `0x20000`（分区表见 `partitions.csv`），与原版镜像布局可能不同，首次迁移请用 USB 整片烧录并先 `idf.py erase-flash`（会清除内部 Flash 设置，不影响 SD 卡）。NVS 键名与 Arduino 版兼容，重新配置后收藏等数据按新键写入。本工程后续版本之间可直接 Web OTA。

## 使用

WebUI、热点、键盘操作与原版 v1.3.0 相同：

- 热点 `Lazer-Cardputer` / `lazer1234`，管理页 `http://192.168.4.1/`，账号 `admin` / `lazer1234`。**首次使用请立即修改默认密码。**
- 键盘：`Space` 播放/暂停、`M` 全屏播放器、`L` 歌词/封面、`Q` 队列、`F` 收藏、`N/P` 切歌、`A/D` ±5s、`W/S` 选择/音量、`R` 播放模式、`Tab/1-4` 切页、`H` 帮助、`Esc` 返回。
- 网易云：设备端「设置 → 网易云登录」扫码或手机号登录；在线搜索在 WebUI「网易云」页。预置公共 API `https://www.xlvip.ltd` 为第三方服务，**不建议登录重要账号，长期请自建**（Docker/NAS 部署见原版 README，接口兼容）。
- HTTPS 仍为不校验证书模式（与原版一致），局域网 HTTP 明文传输；请勿暴露公网。

## 与原版的行为差异（已知且已确认）

1. **界面用 LVGL 重建**：布局、配色、信息层级与原版一致，但动效为 LVGL 缓动/overshoot 近似，非逐像素复制；播放/暂停等图标用字体符号（>、||、♥、≡）替代原矢量绘制。
2. 字体为 MiSans 子集（GB2312 常用 3755 字 + 符号），生僻字可能缺字形。
3. 在线缓冲由 AudioFileSourceBuffer 换为自写环形缓冲 + 喂料任务，缓冲容量同为 24KiB。
4. ID3 UTF-16 代理对解码修正为完整 4 字节 UTF-8（原版按 BMP 截断）。
5. 电池电量按 GPIO10 ADC（1:1 分压）估算，换算曲线与 M5Unified 略有差异，需实机校准。

## 验证状态

- **主机单元测试**：`test/host/run.sh`，8 个套件 278 项断言全部通过（字符串/路径工具、MP3 帧头与时长估算、ID3v1/v2 与完整元数据管线、LRC 解析与 7 行视图、播放队列、网易云响应解析契约）。纯逻辑模块按测试先行（红→绿→重构）开发。
- **编译验证（已完成）**：ESP-IDF v5.5.6 + GCC 14.2.0，主固件与 LCD 诊断固件均编译链接通过。主应用镜像 2,270,064 字节，占 3264KiB app 分区的 69.5%；诊断镜像 915,360 字节。构建产物在构建目录：主固件 `lazer_cardputer.bin`、诊断固件 `lazer_lcd_test.bin`（Web OTA 用 app 镜像；USB 整片烧录可自行用 esptool merge_bin 合并，app 偏移 0x20000）。
- **实机问题修复记录**：首版固件在原版 Cardputer（Stamp-S3）上出现"背光亮、无图像"。根因：IDF 的 ST7789 驱动 init() 不发送 DISPON，需显式调用 `esp_lcd_panel_disp_on_off(panel, true)`，已修复。诊断固件增加背光心跳（启动前三闪证明固件在运行；初始化失败快闪 SOS）与逐步串口日志；控制台已改到 USB Serial/JTAG，插 USB-C 即可用 115200 看日志。原版 Cardputer 与 v1.1 的显示/键盘/SD/功放引脚一致，固件通用。
- **未验证项（需实机）**：屏幕方向/色彩（gap 40/53 按 M5GFX 旋转 0 推导）、键盘扫描时序、I2S 音质与音量曲线、电池 ADC 换算、Wi-Fi 兼容性、网易云真实账号登录与在线播放、OTA 升级、长时间播放稳定性。

## 许可证

本工程 AGPL-3.0-or-later（见 LICENSE），与原 Lazer 一致。第三方组件：

- libmad：GPL-2.0+（components/mp3dec/libmad/COPYING），与 AGPL 兼容。
- LVGL 9.2.2：MIT（components/lvgl/LICENCE.txt）。
- qrcodegen：MIT（Nayuki，文件头声明）。
- cJSON：MIT（components/netease/cJSON-LICENSE）。
- MiSans 字体：小米 MiSans 字体知识产权许可协议（允许免费商用与嵌入，见 https://hyperos.mi.com/font/ ）。
- 上游 Lazer 第三方声明见 docs/UPSTREAM-THIRD-PARTY-NOTICES.md。

---by ChenhaoZhang
2262881823@qq.com
