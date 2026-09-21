#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"   // ES8311 定义 + i2c/i2s/gpio 接口工厂函数
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG = "board";

_Static_assert(BOARD_LCD_INVERT_COLOR, "ES3C28P IPS panel requires color inversion (INVON)");

static i2c_master_bus_handle_t s_i2c_bus;
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_touch_handle_t s_tp;
static esp_codec_dev_handle_t s_codec;
static i2s_chan_handle_t s_i2s_tx;
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_adc_cali;
static bool s_backlight_ready;
static int s_backlight_percent = 85;

// ---------------------------------------------------------------- 显示 / 触摸

static esp_err_t i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,  // 板载已有 4.7K，内部上拉只作保险
    };
    return i2c_new_master_bus(&cfg, &s_i2c_bus);
}

void board_backlight(bool on)
{
    board_backlight_level(on ? s_backlight_percent : 0);
}

void board_backlight_level(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent > 0) s_backlight_percent = percent;
    if (!s_backlight_ready) {
        gpio_set_level(BOARD_LCD_BL, percent > 0);
        return;
    }
    uint32_t duty = (1023U * (uint32_t)percent) / 100U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

esp_err_t board_backlight_fade_to(int percent, int duration_ms)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (duration_ms < 0) duration_ms = 0;
    if (percent > 0) s_backlight_percent = percent;
    if (!s_backlight_ready) {
        board_backlight_level(percent);
        return ESP_OK;
    }
    uint32_t duty = (1023U * (uint32_t)percent) / 100U;
    ESP_RETURN_ON_ERROR(ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                                                duty, (uint32_t)duration_ms),
                        TAG, "背光渐变参数设置失败");
    return ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_NO_WAIT);
}

esp_err_t board_display_power(bool on)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (!on) {
        board_backlight(false);
        return esp_lcd_panel_disp_on_off(s_panel, false);
    }
    esp_err_t err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err == ESP_OK) board_backlight(true);
    return err;
}

esp_err_t board_display_init(void)
{
    ESP_RETURN_ON_ERROR(i2c_bus_init(), TAG, "I2C 总线初始化失败");

    // 背光先拉低，等屏初始化完再点亮，避免开机闪白屏
    gpio_config_t bl_cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << BOARD_LCD_BL,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bl_cfg), TAG, "背光 GPIO 配置失败");
    board_backlight(false);

    ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "背光 PWM 时钟初始化失败");
    ledc_channel_config_t bl_channel = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "背光 PWM 初始化失败");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "背光渐变服务初始化失败");
    s_backlight_ready = true;

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_SCREEN_W * 40 * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI 总线初始化失败");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PIXEL_CLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                                 &io_cfg, &io),
                        TAG, "LCD panel IO 创建失败");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOARD_LCD_RST,   // -1：屏复位与 CHIP_PU 共用
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, &s_panel), TAG, "ILI9341 创建失败");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "屏复位失败");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "屏初始化失败");
    // ⚠️ IPS 版 ILI9341（本模组 QD2833）出厂需要 INVON，否则整屏黑白反相。
    // TN 版才是 false。症状：白底黑字的界面显示成黑底白字。
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, BOARD_LCD_INVERT_COLOR),
                        TAG, "反色设置失败");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "开显示失败");

    // 触摸：FT6336G 兼容 FT5x06 驱动
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_cfg.scl_speed_hz = BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_bus, &tp_io_cfg, &tp_io),
                        TAG, "触摸 panel IO 创建失败");

    // x_max/y_max 用屏的原生（竖屏）尺寸：驱动是先 mirror 再 swap。
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_NATIVE_W,
        .y_max = BOARD_LCD_NATIVE_H,
        .rst_gpio_num = BOARD_TP_RST,
        .int_gpio_num = BOARD_TP_INT,
        .levels = {
            .reset = 0,      // 复位低有效
            .interrupt = 0,  // 触摸时输出低
        },
        .flags = {
            .swap_xy = BOARD_TP_SWAP_XY,
            .mirror_x = BOARD_TP_MIRROR_X,
            .mirror_y = BOARD_TP_MIRROR_Y,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &s_tp), TAG, "FT6336 初始化失败");

    // LVGL
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 8192;
    port_cfg.task_affinity = -1;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "LVGL port 初始化失败");

    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = s_panel,
        .buffer_size = BOARD_SCREEN_W * 40,
        .double_buffer = true,
        .hres = BOARD_SCREEN_W,
        .vres = BOARD_SCREEN_H,
        .monochrome = false,
        .rotation = {
            .swap_xy = BOARD_LCD_SWAP_XY,
            .mirror_x = BOARD_LCD_MIRROR_X,
            .mirror_y = BOARD_LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "LVGL 显示注册失败");

    lvgl_port_touch_cfg_t touch_cfg = {
        .disp = disp,
        .handle = s_tp,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "LVGL 触摸注册失败");

    ESP_LOGI(TAG, "显示与触摸就绪：%dx%d，IPS INVON=%s", BOARD_SCREEN_W, BOARD_SCREEN_H,
             BOARD_LCD_INVERT_COLOR ? "on" : "off");
    return ESP_OK;
}

// ---------------------------------------------------------------------- 音频

esp_err_t board_audio_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;   // 停播时补零，避免残留噪声
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 240;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, NULL), TAG, "I2S 通道创建失败");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws   = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,   // GPIO8 —— 别照原理图网络名填，那是编解码器视角
            .din  = BOARD_I2S_DIN,    // GPIO6
            .invert_flags = { false, false, false },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "I2S std 模式失败");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "I2S data 接口创建失败");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,   // 0x30 = 7 位地址 0x18 左移一位
        .bus_handle = s_i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "ES8311 I2C 控制接口创建失败");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BOARD_PA_EN,
        .pa_reverted = true,     // GPIO1 低=开声
        .master_mode = false,    // ESP32 是 I2S 主机
        .use_mclk = true,        // MCLK 走 GPIO4
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "ES8311 创建失败");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "codec dev 创建失败");

    ESP_LOGI(TAG, "音频就绪（ES8311 @ I2C 0x18，功放使能 GPIO%d 低有效）", BOARD_PA_EN);
    return ESP_OK;
}

esp_codec_dev_handle_t board_codec(void)
{
    return s_codec;
}

// ---------------------------------------------------------------------- 电池

float board_battery_voltage(void)
{
    if (!s_adc) {
        adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
        if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
            return -1.0f;
        }
        adc_oneshot_chan_cfg_t ch_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        if (adc_oneshot_config_channel(s_adc, BOARD_BAT_ADC_CH, &ch_cfg) != ESP_OK) {
            return -1.0f;
        }
        adc_cali_curve_fitting_config_t cali_cfg = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali);
    }

    int raw = 0;
    if (adc_oneshot_read(s_adc, BOARD_BAT_ADC_CH, &raw) != ESP_OK) {
        return -1.0f;
    }
    int mv = 0;
    if (s_adc_cali && adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) == ESP_OK) {
        return mv * 2.0f / 1000.0f;   // R14/R15 = 200K/200K 分压
    }
    return -1.0f;
}
