# 构建环境

## 工具链

本机已装 ESP-IDF 在 `~/esp/esp-idf`：

```bash
source ~/esp/esp-idf/export.sh   # 每开一个新 shell 都要跑
idf.py --version
```

厂商示例基于 **ESP-IDF 5.4.1 + LVGL 8.4.0**。IDF 5.4/5.5 都能用。

## 串口

这块板子用的是 **ESP32-S3 内置的 USB-Serial/JTAG**，直接一根 Type-C 到电脑，
不需要 CH340 之类的转换芯片。macOS 上设备名是：

```
/dev/cu.usbmodem21201        # 数字会随插的 USB 口变化
```

`tools/esp.sh` 会自动挑第一个 `/dev/cu.usbmodem*`；要指定就设环境变量：

```bash
ESP_PORT=/dev/cu.usbmodemXXXXX tools/esp.sh flash_id
```

原生 USB 不受 UART 波特率限制，脚本默认 **921600**（16MB 整片读约 3 分半）。

> 板上还有一个 4P 1.25mm 的串口座子（TX=IO43, RX=IO44），需要外接 USB-TTL 模块，
> 一般用不上。用它的时候 `tools/esp.sh` 会退回去找 `/dev/cu.usbserial*`。

## 进下载模式

正常情况下 esptool 能自动复位进下载模式。如果失败，手动：

- 按住 BOOT，插电 / 按一下 RESET，松开 BOOT

## sdkconfig 关键项

这块板子是 **N16R8**，以下几项必须对，否则要么起不来要么白白浪费 8MB PSRAM：

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y          # eFuse 已烧死为 quad，别选 OPI

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y                  # ⚠️ 片内是 OPI(octal) PSRAM，选错 PSRAM 认不到
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y        # 大固件时有用
CONFIG_SPIRAM_RODATA=y

CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y      # 日志走内置 USB，不走 UART0
```

用 octal PSRAM 时 GPIO33~37 被占死，不要在代码里碰它们。

验证 PSRAM 真的认到了：启动日志里应有

```
I (xxx) esp_psram: Found 8MB PSRAM device
I (xxx) esp_psram: Speed: 80MHz
```

## 分区表

原厂固件只用了 4MB。自己的工程建议用满 16MB，
留双 OTA + 一个大的存储分区，比如：

```csv
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
otadata,  data, ota,     0xf000,   0x2000
phy_init, data, phy,     0x11000,  0x1000
ota_0,    app,  ota_0,   0x20000,  0x300000
ota_1,    app,  ota_1,   0x320000, 0x300000
storage,  data, spiffs,  0x620000, 0x9E0000
```

## 屏 / 触摸 / 音频组件

ESP-IDF 组件管理器上有现成的：

```bash
idf.py add-dependency "espressif/esp_lcd_ili9341"
idf.py add-dependency "espressif/esp_lcd_touch_ft5x06"    # FT6336G 兼容 FT5x06 驱动
idf.py add-dependency "espressif/es8311"
idf.py add-dependency "espressif/led_strip"               # WS2812B
idf.py add-dependency "lvgl/lvgl"
```

接线参数照 [`hardware.md`](hardware.md) 填。几个容易错的：

- 屏没有独立复位脚，`reset_gpio_num = -1`
- I²S 的 `dout = GPIO_NUM_8`、`din = GPIO_NUM_6`（**别照原理图网络名填**）
- 开声前要把 GPIO1 拉低，否则功放是静音的
- 背光上电默认灭，要主动把 GPIO45 拉高
