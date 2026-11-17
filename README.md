# Lazer Cardputer v1.1 Edition

将 [chuxuehaocai/Lazer](https://github.com/chuxuehaocai/Lazer) 的 Apple Music 风格界面、AMLL 式歌词阅读节奏和弹簧动效，原生移植到使用 **Stamp-S3A 模组的 M5Stack Cardputer v1.1**。

这不是 Flutter 模拟器或界面截图，而是一个独立的 ESP32-S3 / C++ 固件。原项目的桌面与移动端 Dart 代码无法直接在 ESP32-S3 上运行，因此本移植版重写了显示、输入、曲库、音频和持久化层。

![紧凑界面设计预览](assets/界面预览.png)

！！！！！请仔细核对自己的设备引脚是否为code中指向的正确引脚，请仔细核对自己的芯片型号和版型，目前仅支持st7789其他屏幕等待update！！！！！

！！！！请在AudioEngine.cpp里定义自己的i2s引脚！！！！

## 第一版功能

- 四套浏览页面：主页、音乐库、我喜欢、设置。
- 底部标签栏、迷你播放器、全屏播放页和播放队列。
- SD 卡递归扫描 MP3，最多 80 首，目录深度最多两层。
- 读取常见 ID3v1 / ID3v2 标题、歌手、专辑信息；没有标签时使用文件名。
- MP3 播放、暂停、上一首、下一首、约 ±5 秒定位。
- 顺序循环、单曲循环、随机播放。
- 读取与音频同名的 `.lrc` 文件；当前行渐进高亮，前后行弱化，平滑跟随。
- 封面色彩驱动的动态网格背景、封面与歌词切换、可中断弹簧转场。
- 收藏、音量、亮度、外观、歌词、减少动态效果和播放模式保存到 Flash。
- 深色／浅色外观、亮度调节、减少动态效果。

## 硬件范围

- ESP32-S3FN8，8 MB Flash，240×135 ST7789V2 屏幕，其他屏幕等待适配。
- GPIO38 屏幕背光，启动早期强制拉高，避免初始化探测失败时表现为完全黑屏。
- v1.0.2 根据实机照片修正画面方向：启动画面、主 UI 和 LCD 诊断统一使用 `setRotation(0)`，以 240×135 正横屏显示，键盘位于屏幕下方。
- GPIO 矩阵键盘（74HC138）。
- NS4168 I²S 音频功放：BCLK=GPIO41、LRCK=GPIO43、DOUT=GPIO42。
- microSD：SCK=GPIO40、MISO=GPIO39、MOSI=GPIO14、CS=GPIO12。

**不要直接烧录到 Cardputer ADV。** ADV 使用 TCA8418 键盘和 ES8311 音频编解码器，需要单独适配。预编译版本为cardputerv1.1编译的固件。

## 准备 SD 卡

建议使用 FAT32。MP3 可以放在根目录或最多两层子目录中：

```text
/
├── Artist - Song.mp3
├── Artist - Song.lrc
└── Music/
    ├── Another song.mp3
    └── Another song.lrc
```

- 文件名形如 `歌手 - 歌名.mp3` 时，即使没有 ID3 标签也能拆分显示。
- 歌词文件名除扩展名外必须与音频完全一致，扩展名使用小写 `.lrc`。
- 128–192 kbps 的普通 MP3 更适合本机内存和解码能力。
- 时长与定位依据 MP3 码率估算；VBR 文件的进度可能有偏差。
- 当前版本使用程序化封面，不解码内嵌 APIC 图片。

## 键盘操作

| 按键 | 功能 |
| --- | --- |
| `Space` | 播放／暂停 |
| `Enter` | 播放所选歌曲 |
| `M` | 打开／关闭全屏播放器 |
| `L` | 歌词／大封面切换 |
| `Q` | 打开／关闭播放队列 |
| `F` | 收藏／取消收藏 |
| `N` / `P` | 下一首／上一首；播放超过 5 秒时，上一首会回到开头 |
| `A` / `D`，或左右方向键 | 播放页快退／快进约 5 秒 |
| `W` / `S`，或上下方向键 | 列表选择；播放页调音量 |
| `+` / `-` | 音量增减 |
| `R` | 顺序／单曲／随机模式 |
| `Tab` / `1`–`4` | 切换四个浏览页面 |
| `Esc` | 关闭面板或返回浏览页 |
| `H` | 打开键盘帮助 |

方向键使用 Cardputer 的 `Fn` 组合：`Fn+;` 上、`Fn+,` 左、`Fn+.` 下、`Fn+/` 右；`Esc` 对应键盘左上角按键。设置页中，`Enter` 切换所选项目，亮度也可用左右方向键调整。

## 使用 PlatformIO 编译

依赖版本已锁定在 `platformio.ini`：

- PlatformIO `espressif32@6.10.0`。
- `M5Cardputer@1.1.1`。
- `ESP8266Audio@1.9.7`。

```bash
pio run
pio run --target upload
pio device monitor
```

项目自带 Cardputer 板型 JSON 和 `variants/m5stack_cardputer/pins_arduino.h`，因此使用 Arduino-ESP32 2.0.17 时也不需要较新的开发板索引或手工补引脚文件。烧录时选择 `m5stack-cardputer` 环境。

如需自行编译不依赖曲库、音频和播放器界面的极简 LCD 诊断程序：

```bash
pio run -e cardputer-lcd-test
pio run -e cardputer-lcd-test --target upload
```

如果无法进入下载模式：将顶部电源开关拨到 OFF，按住机身侧面的 G0，连接 USB-C，然后松开 G0。

## 直接烧录预编译固件

纯源码包不含预编译固件；完整源码与固件包的 `firmware/` 中提供整片合并镜像。自行编译也可直接使用上面的 `pio run --target upload`。由于旧版使用了错误的 4 MB 配置，第一次刷入 1.0.2 时建议先完整擦除内部 Flash（不会擦除 SD 卡，但会清除固件内保存的设置）。把 `PORT` 换成实际串口：

```bash
esptool.py --chip esp32s3 --port PORT erase_flash
esptool.py --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 \
  firmware/Lazer-Cardputer-v1.0.2-merged.bin
```

合并镜像从 `0x0` 开始烧录，完整包含 `0x0` bootloader、`0x8000` 分区表、`0xe000` OTA 启动选择数据和 `0x10000` 应用，不要再按 `0x10000` 偏移烧写这个文件。目录中同时保留独立的 app、bootloader、partitions 和 otadata 镜像供开发调试；普通烧录只需 merged 文件。

如果 1.0.2 仍不亮屏，请执行 `pio device monitor` 打开 115200 串口并重新上电。启动日志会以 `Lazer 1.0.2 - Cardputer v1.1 / Stamp-S3A` 开头，随后输出检测到的芯片、Flash 容量和 LCD 尺寸；这些日志可用于继续区分启动失败与屏幕驱动问题。

### 黑屏时烧录极简 LCD 诊断固件

诊断程序只初始化 Stamp-S3A、背光和 LCD，不扫描 SD 卡，不启动音频、曲库、设置存储或播放器界面。它会替换主固件；诊断结束后需重新烧录上面的主 merged 镜像。

```bash
esptool.py --chip esp32s3 --port PORT erase_flash
esptool.py --chip esp32s3 --port PORT --baud 460800 write_flash 0x0 \
  firmware/Lazer-Cardputer-v1.0.2-lcd-test-merged.bin
```

正常启动后应看到：顶部八条色带、绿色 `LCD OK`、`Stamp-S3A / 240x135`、`Lazer display test`，底部绿点每 500 ms 明暗切换。115200 串口应输出一次 `LCD_TEST_READY`，并持续输出 `LCD_TEST_ALIVE`。

- **诊断能亮、主程序不亮**：说明显示供电和基础驱动链路可工作，下一步排查主程序启动流程。
- **诊断也不亮但有串口日志**：重点检查背光供电、屏幕 FPC 排线、屏幕模组和底层显示初始化。
- **不亮且没有串口日志**：先保留下载模式和 115200 启动日志，排查供电、启动或烧录问题，不要先改播放器界面。

## 构建与实机验证状态

- v1.0.2 已使用项目指定的 PlatformIO、Arduino-ESP32 2.0.17 和 ESP32-S3 工具链完成主程序与 LCD 诊断程序的构建和链接。
- v1.0.2 主程序静态 RAM 为 27,000 字节（8.2%），应用为 1,420,005 字节，占 3,264 KiB 应用分区的 42.5%；LCD 诊断程序静态 RAM 为 22,500 字节（6.9%），应用为 497,317 字节（14.9%）。
- 两个合并镜像均已核对四个组成文件的偏移和内容；bootloader 与应用均标记为 DIO、80 MHz、8 MB，应用校验和及验证哈希有效。
- v1.0.2 根据实机照片修正了旋转方向，但修正后的实机显示仍需你的设备确认；扬声器实际音质与音量、不同 SD 卡的兼容性、按键手感、长时间播放稳定性以及真实屏幕观感也需上机验证。
- `assets/界面预览.png` 是设计预览，不是实体设备截图。

## 与原项目的差异

- 在线曲库、账号、评论、NAS、播客、Lua 插件、桌面歌词和系统媒体控制不在第一版范围内。
- LRC 使用行级渐进高亮；不实现桌面版的 YRC 逐字弹簧、距离模糊与字形发光。
- 受无 PSRAM 和 240×135 屏幕限制，网格背景与转场经过重新设计，并非 Flutter 渲染结果的逐像素复制。
- 程序不会访问网络，所有播放与收藏都在本机完成。

## 许可证与致谢

本移植版采用 `AGPL-3.0-or-later`，与原 Lazer 项目一致。原项目第三方声明见 `UPSTREAM-THIRD-PARTY-NOTICES.md`。

- 原项目：chuxuehaocai/Lazer。
- 硬件抽象与显示：M5Stack M5Cardputer、M5Unified、M5GFX。
- MP3 解码：earlephilhower/ESP8266Audio。
- Cardputer v1.1 的 I²S 引脚与 MP3 任务调度参考 VolosR/M5Mp3 的公开实现；本项目的界面、曲库、歌词和播放状态机为重新实现。
