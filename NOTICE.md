# 第三方组件与素材出处

本仓库以 MIT 分发（见 `LICENSE`）。以下逐项列明第三方代码与素材的来源与许可。

## 托管组件

`main/idf_component.yml` 声明的依赖**由 ESP Component Registry 在构建时拉取，
不随本仓库分发**：

| 组件 | 版本约束 | 许可 |
|---|---|---|
| `espressif/esp_lcd_ili9341` | `^2.1.0` | Apache-2.0 |
| `espressif/esp_lcd_touch_ft5x06` | `^1.1.1` | Apache-2.0 |
| `lvgl/lvgl` | `~8.4.0` | MIT |
| `espressif/esp_lvgl_port` | `^2.9.0` | Apache-2.0 |
| `espressif/esp_codec_dev` | `^1.6.2` | Apache-2.0 |
| `chmorgan/esp-libhelix-mp3` | `^1.0.3` | 见下 |

### MP3 解码器的许可要说清楚

`chmorgan/esp-libhelix-mp3` 仓库自身声明 Apache-2.0，但它封装的是 **RealNetworks
Helix 项目**的 MP3 定点解码器，那批源文件的文件头带 RPSL / RCSL 声明。本仓库不分发
该组件的任何代码（构建时从 Registry 拉取），但**如果你要把这个固件用在对解码器授权
有严格要求的场合，请自行核实或替换**。

## 字体

中文字库以 LVGL 字体格式的 C 源码编进固件：

| 文件 | 来源 | 可否重新生成 |
|---|---|---|
| `main/fonts/font_cjk_14.c`<br>`main/fonts/font_cjk_18.c`<br>`main/fonts/font_cjk_24.c` | [Noto Sans SC](https://github.com/notofonts/noto-cjk)，**SIL Open Font License 1.1** | ✅ `tools/gen_fonts.sh` |

字体源按 commit 钉死在 `google/fonts` 的
`2894aab31764f10f29c421bdfd2340d3b382d384`，路径 `ofl/notosanssc/NotoSansSC[wght].ttf`；
脚本会校验下载物的 SHA-256，再用 fontTools 把可变字体定重到 `wght=400`（Regular），
最后用 `lv_font_conv` 1.5.3 转成 LVGL 8 格式。字符集为 GB2312 一级汉字加固件实际文案
与全部内置电台名，共 3807 个字符。整条链路可完整复现。

图标字形（喇叭 / Wi-Fi / 播放键）用的是 LVGL 自带的 Montserrat 子集，随 LVGL 以
SIL Open Font License 1.1 分发。

## 电台数据

`main/station_catalog.inc` 是 542 个**公开** MP3 直播地址的静态目录：

- 候选频道 ID 来自 [`gaotianliuyun/gao`](https://github.com/gaotianliuyun/gao) 公开的 `radio.txt`
- 台名、地区与内容分类取自蜻蜓开放平台及其公开网页接口
- 香港电台 Radio 1–5 取自 RTHK 官方公开直播入口

采集与验证口径见 `docs/station-catalog.md`。目录**只收录公开可访问的直播地址，不含
任何私有接口、鉴权绕过或破解**。公开地址可能随电台或平台调整而失效，因此「已验证」
指生成快照时实际拉流通过，不表示永久可用。**音频内容的版权属各电台所有**，本仓库
不分发任何音频内容。

## 素材

| 文件 | 说明 |
|---|---|
| `main/assets/heart.svg` 及由它烘出的 `heart_icon.c` | 24×24 收藏图标轮廓。**其路径结构与 [Material Icons](https://github.com/google/material-design-icons) 的 `favorite` 高度同构，原始来源已无法确认**；若源自它，则该图标遵 Apache-2.0。烘焙脚本见 `tools/generate_heart_icon.py` |
| `main/assets/donation_qr.c` | 仓库所有者本人的微信收款码，由 `tools/generate_donation_qr.py` 从 `docs/assets/wechat-donation.jpg` 转成 RGB565 |

## 硬件文档

`docs/hardware.md` 的引脚与外设信息，整理自厂商 wiki、原理图、IO 资源分配表与各芯片
数据手册的交叉验证，并以实机读数核对。**厂商原始文档（PDF / xlsx / zip）本身不随本
仓库分发**，需要时从
<https://www.lcdwiki.com/zh/2.8inch_ESP32-S3_Display> 获取。
