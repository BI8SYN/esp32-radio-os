// 板级支持：LCDwiki 2.8" ESP32-S3 Display (ES3C28P)
// 引脚依据 docs/hardware.md（已交叉验证厂商 wiki / 原理图 / IO 分配表 / 数据手册）。
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- 屏 ILI9341V，4 线 SPI ----
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_CS            10
#define BOARD_LCD_DC            46
#define BOARD_LCD_SCK           12
#define BOARD_LCD_MOSI          11
#define BOARD_LCD_MISO          13
#define BOARD_LCD_RST           (-1)   // 屏复位接在 CHIP_PU 上，没有独立 GPIO
#define BOARD_LCD_BL            45     // 高=亮；上电默认灭
#define BOARD_LCD_PIXEL_CLK_HZ  (40 * 1000 * 1000)
// QD2833 IPS 面板必须 INVON。改为 false 会把所有 UI 颜色整体颠倒。
#define BOARD_LCD_INVERT_COLOR  true

// 屏原生是 240×320 竖屏；本固件按 320×240 横屏使用。
#define BOARD_LCD_NATIVE_W      240
#define BOARD_LCD_NATIVE_H      320
#define BOARD_SCREEN_W          320
#define BOARD_SCREEN_H          240

// 横屏方向。⚠️ 屏和触摸**不能共用**同一组参数——两者的变换语义根本不同：
//
//   屏：走面板硬件寄存器 MADCTL = MV | MX? | MY?（驱动默认初始化不设 MADCTL，
//       所以值就这三位决定）。MV=1 时两个镜像位的作用相对竖屏是换过来的——
//       MY（行地址序）管**左右**，MX（列地址序）管**上下**。
//
//   触摸：走 esp_lcd_touch 里的软件变换，顺序是 **先 mirror 再 swap**，
//       且 x_max/y_max 用的是**原生竖屏**尺寸（240×320），不是旋转后的。
//
// 2026-09-02 实机逐个试出来的（本模组 QD2833）：
//   屏   MX=1,MY=0 → 上下颠倒；MX=0,MY=1 → 左右翻转；MX=0,MY=0 → 正确 ✓
//   触摸 与屏同参时上下颠倒，需要 mirror_x=true 把 swap 前的 X 轴翻过来 ✓
#define BOARD_LCD_SWAP_XY       true
#define BOARD_LCD_MIRROR_X      false
#define BOARD_LCD_MIRROR_Y      false

#define BOARD_TP_SWAP_XY        true
#define BOARD_TP_MIRROR_X       true
#define BOARD_TP_MIRROR_Y       false

// ---- I²C 总线（触摸 0x38 + ES8311 0x18 共用）----
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_SCL           15
#define BOARD_I2C_SDA           16
#define BOARD_I2C_FREQ_HZ       (400 * 1000)

// ---- 电容触摸 FT6336G ----
#define BOARD_TP_RST            18
#define BOARD_TP_INT            17

// ---- 音频 ES8311 + SC8002B 功放 ----
#define BOARD_PA_EN             1      // 低=开声，高=静音；板载 10K 上拉，上电默认静音
#define BOARD_I2S_MCLK          4
#define BOARD_I2S_BCLK          5
#define BOARD_I2S_WS            7
#define BOARD_I2S_DOUT          8      // → ES8311 DSDIN（喇叭）
#define BOARD_I2S_DIN           6      // ← ES8311 ASDOUT（麦克风）

// ---- 其它 ----
#define BOARD_BAT_ADC_CH        ADC_CHANNEL_8   // GPIO9，R14/R15 200K/200K 分压 → ×2
#define BOARD_BOOT_BTN          0

// 初始化 I²C、屏、背光、触摸，并把它们挂到 LVGL 上。
esp_err_t board_display_init(void);

// 初始化 I2S + ES8311。喇叭没插也能成功返回。
esp_err_t board_audio_init(void);

// 音频编解码句柄；board_audio_init 之前返回 NULL。
esp_codec_dev_handle_t board_codec(void);

// 背光开关（GPIO45）。
void board_backlight(bool on);

// 背光亮度 10–100%。使用 LEDC 硬件 PWM，0 等同关闭。
void board_backlight_level(int percent);

// 使用 LEDC 硬件渐变到目标亮度；启动首帧使用，过程不阻塞 UI。
esp_err_t board_backlight_fade_to(int percent, int duration_ms);

// 显示面板电源状态。关闭时先灭背光再发送 DISPOFF；开启时反向恢复。
// 触摸控制器和音频链路不受影响，可用于息屏播放。
esp_err_t board_display_power(bool on);

// 电池电压（V）。读不到返回 -1。
float board_battery_voltage(void);

#ifdef __cplusplus
}
#endif
