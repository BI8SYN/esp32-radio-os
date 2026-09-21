# Radio OS

[![CI](https://github.com/BI8SYN/esp32-radio-os/actions/workflows/ci.yml/badge.svg)](https://github.com/BI8SYN/esp32-radio-os/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

An internet radio firmware for the LCDwiki 2.8" ESP32-S3 Display board
(SKU: ES3C28P) — 542 pre-verified Chinese public MP3 streams browsable fully
offline, province and genre filtering, on-screen Wi-Fi setup, a real-PCM FFT
spectrum, light/dark themes on a schedule, sleep timer, and signed OTA updates.
Built with ESP-IDF and LVGL. Flash a prebuilt image from
[Releases](../../releases), or build it yourself — the rest of this README is in
Chinese.

---

## 硬件速查

| | |
|---|---|
| 主控 | ESP32-S3R8（N16R8：16MB QSPI Flash + 8MB **OPI** PSRAM） |
| 屏 | 2.8" IPS 240×320，ILI9341V，4 线 SPI |
| 触摸 | FT6336G 电容触摸，I²C `0x38` |
| 音频 | ES8311 编解码 @ I²C `0x18` + SC8002B 功放 + MEMS 麦克风 |
| 存储 | MicroSD，SDIO 4 位 |
| 其它 | WS2812B RGB 灯、TP4054 充电、电池电压 ADC |
| 串口 | 内置 USB-Serial/JTAG（`/dev/cu.usbmodem*`） |

完整引脚表见 [`docs/hardware.md`](docs/hardware.md)。

对应的界面设计文档见 [`docs/design/station-list.md`](docs/design/station-list.md)（含可点击的 HTML 原型）。

## 联网

1. 上电，屏幕显示「联网设置」引导页
2. 点「在本机选择 Wi-Fi」，直接在屏幕上选择 2.4GHz 网络并输入密码；隐藏网络可手动输入名称
3. 连接成功后自动进入播放页

备用方式：手机连接热点 **`ES3C28P-Radio`**，打开 **<http://192.168.4.1>** 配网。

> ⚠️ **ESP32 只支持 2.4GHz**。配网页的列表里只会出现 2.4GHz 网络，所以照着选就不会错。
> 如果你家的网络没出现在列表里，多半是路由器只开了 5GHz 频段
> （SSID 常带 `_5G` 后缀，那种设备永远连不上）。

**换地方 / 换路由器**，三条路都行：

- 什么都不用做——连不上时设备**自动回到配网模式**（重试 6 次约 30 秒后）
- 播放页点左上角「设置」→「选择网络」
- 按住 BOOT 键上电（这个会顺带清掉已存的凭据）

## 功能

- **三个本地入口**：收藏（NVS，上限 50）/ 精选（35 个）/ 台库（542 个发布前全量复检通过的公开 MP3 流）
- 台库覆盖全国级、31 个省级地区与香港；按综合、资讯、音乐、交通、财经、文艺、生活、体育、方言等内容分类
- **地区 + 内容双重筛选**完全在本机完成；目录固化在 Flash，打开页面不再等待 Radio Browser 或任何海外目录 API
- 每页 16 台，支持上一页/下一页；每行显示地区、类型和 MP3 码率，并可直接收藏
- **播放**：HTTP 拉流 → 128KB PSRAM 环形缓冲 → MP3 解码（libhelix）→ I2S → ES8311
- 播放控制使用单独命令队列：连续快速换台时先完整回收旧会话，再启动最后选择的电台
- 网络短暂超时继续等待，断流自动重连 3 次后才显示失败
- 连续收听时关闭 Wi-Fi modem sleep，避免信号良好但实时吞吐周期性塌陷；缓冲耗尽后先重新蓄水再恢复，减少碎片化卡顿
- 串口状态同时报告有效下载速率、重连次数和缓冲耗尽次数，便于区分无线信号、网络链路与直播源问题
- **上一台 / 下一台** 在当前台所属的列表内循环
- 音量滑动条（0–100），使用低/高音量图标；离线和错误状态不改变底部控件的位置
- 设置页：本机选网、联网自动续播、亮色/暗色/自动外观、暗色时段、自定义主题色、24/12 小时时间格式、10–100% 屏幕亮度、系统更新、诊断与关于
- 诊断页提供五点触摸检测，逐点显示目标/实际坐标与偏差，并判断适合固定偏移还是多点补偿；测试只读，不会直接改写触摸参数
- 左侧抽屉：单线流线/17 根镜像能量条切换、15/30/60/90 分钟睡眠定时、设置入口，以及离线内置的“请我喝杯咖啡”微信收款码弹层
- 记住最后电台、来源、音量、亮度、自动续播、时间格式、外观模式、暗色时段、主题色和频谱样式；滑块松手后才写 NVS
- Radio OS 1.1.5 提供暖白亮色与低眩光暗色界面；默认在 22:00–08:00 自动使用暗色，开始/结束时间均可调整，也可固定亮色或暗色
- 主题色使用独立弹层：色相环可自由选色，下方提供科技紫、清新青绿、温暖橙、活力玫红四种差异化预置；点击“应用”后才重绘界面并持久化
- 自定义高亮色会自动约束亮度并选择黑/白前景色，保证按钮和选中态文字对比度；主页仍只保留菜单、时间和 Wi-Fi，不显示不存在的电池状态
- 开机先显示 `Radio OS` 与 `powered by BI8SYN` 品牌画面，首帧完整刷入后背光从全黑用硬件 PWM 渐亮，音频标记、标题、签名和强调线分层进入，再平滑淡入播放页
- 9 频段真实 PCM FFT 驱动单线流线或 17 根能量条；中心唯一一根由 PCM RMS 响度与低中频共同驱动，左右频段镜像后叠加逐级衰减包络和快攻慢释
- 单线流线使用 55 个 Catmull-Rom 平滑采样点并以 25 FPS 更新
- 台名超出可视宽度时自动滚动，不再截断
- 开机联网后自动检查签名 OTA；只在服务器版本严格高于本机时显示首页更新横幅，点击直达更新页
- 系统更新页展示当前/最新版本和更新内容；下载时校验清单签名、镜像 SHA-256 与应用身份，再切换 OTA 分区并支持首启回滚
- 状态齐全：加载中 / 加载失败（可重试，自动换镜像）/ 离线 / 筛选无结果 / 收藏空态 / 缓冲中 / 播放失败
- USB 串口诊断命令可控制页面、筛选、换台和音量，并输出整机自检与运行指标

## 已知限制

- **只解码 MP3**。AAC/AAC+ 和 HLS(m3u8) 的台会被自动过滤掉，不出现在列表里。
- 电台音频源当前只纳入 HTTP MP3。
- 中文字库包含 GB2312 一级字，并额外收录固件文案和全部内置电台名；目录外的生僻字仍可能显示成方框。
- 公开直播地址可能被上游调整；目录生成器和数据源说明见 [`docs/station-catalog.md`](docs/station-catalog.md)。

## 构建与烧录

```bash
tools/idf.sh build
tools/idf.sh -p /dev/cu.usbmodem21201 flash monitor
```

`tools/idf.sh` 会自动探测机器上实际存在的 IDF 虚拟环境再转发给 `idf.py`。ESP-IDF 的
`export.sh` 按当前系统 python3 的版本号去拼 venv 路径，系统 python 升级过就会找不到——
用这个脚本就不用管这件事。已经在 IDF 环境里（比如 CI 容器）直接 `idf.py build` 即可。

需要 **ESP-IDF 5.4 以上**，且 `idf-component-manager` **必须是 2.5 以上**（2.x 范围内，
IDF 5.5 要求 `~=2.2`）。2.4.x 处理 lvgl 8.4 的可选依赖时会以
`Missing required kconfig option after retry` 失败，且与本工程的配置无关——
`sdkconfig.defaults` 为空也一样。撞上了就升：

```bash
pip install -U 'idf-component-manager>=2.5,<3'
```

固件约 2.61MB，app 分区 4MB，仍有约 35% 空间余量。首次发布可通过 USB 完整烧录
bootloader、分区表、OTA 数据和 app；之后由设备主动访问签名更新源，不提供局域网上传入口。

## 代码结构

| 文件 | 职责 |
|---|---|
| `main/board.c` | 板级：I²C 总线、ILI9341 屏、FT6336 触摸、LVGL 挂载、I2S + ES8311、背光、电池 ADC |
| `main/stations.c` | 电台数据：542 台离线目录、精选、地区/内容筛选、收藏（NVS） |
| `main/station_catalog.inc` | 由工具生成的静态电台目录 |
| `main/player.c` | 播放：PSRAM 环形缓冲、HTTP 拉流任务、MP3 解码任务 |
| `main/wifi_mgr.c` | Wi-Fi：凭据存取、本机扫描连接、配网热点 + 备用网页 |
| `main/ui.c` | LVGL 产品界面：播放 / 列表 / 筛选 / 设置 / 本机配网 |
| `main/diagnostics.c` | USB 串口自检、状态读取与无触摸自动验收控制 |
| `main/ota_mgr.c` | 签名清单、语义版本比较、下载进度、镜像校验、双分区切换与首启确认 |
| `main/fonts/` | 自建 CJK 字库（14/18/24px），由 `lv_font_conv` 生成 |

## 仓库结构

```
main/           固件源码：板级、电台目录、播放、Wi-Fi、LVGL 界面、诊断、OTA
main/fonts/     由 tools/gen_fonts.sh 从 Noto Sans SC 生成的 CJK 字库（14/18/24px）
main/assets/    收藏图标与收款码的预烘焙位图
ota/            当前线上签名清单快照
tools/          idf.py 包装、esptool 封装、字库生成、发布打包、资源生成
docs/           硬件参考、构建环境、电台目录、发布流程、设计文档、版本说明
```

## 关键实现说明

**踩过的坑**（详见 [`docs/hardware.md`](docs/hardware.md)）：

- I²S 方向：`dout = GPIO8`、`din = GPIO6`。原理图网络名是编解码器视角，照着填会收发接反。
- 功放 GPIO1 **低电平才开声**（板载 10K 上拉，上电默认静音）。这个由 `es8311_codec_cfg_t.pa_reverted = true` 处理。
- 背光 GPIO45 上电默认灭，要主动拉高。
- 屏没有独立复位脚，`reset_gpio_num = -1`。

**横屏方向**：屏原生 240×320 竖屏，转成 320×240 横屏。
⚠️ **屏和触摸必须分别配置**，不能共用一组参数——两者的变换语义不同（实测踩过）：

| | 变换方式 | 关键差异 |
|---|---|---|
| 屏 | 面板硬件寄存器 `MADCTL = MV｜MX?｜MY?` | **MV=1 时镜像位管的轴互换**：MY 管左右，MX 管上下 |
| 触摸 | `esp_lcd_touch` 的软件变换 | 顺序是**先 mirror 再 swap**；`x_max/y_max` 填**原生竖屏**尺寸 240×320 |

本模组（QD2833）实测的正确值，已写在 `board.h`：

```c
#define BOARD_LCD_SWAP_XY  true   // 屏：基准方向就是对的，两个镜像位都不开
#define BOARD_LCD_MIRROR_X false
#define BOARD_LCD_MIRROR_Y false

#define BOARD_TP_SWAP_XY   true   // 触摸：需要 mirror_x 把 swap 前的 X 轴翻过来
#define BOARD_TP_MIRROR_X  true
#define BOARD_TP_MIRROR_Y  false
```

换屏或换模组要重调时：屏画面上下颠倒改 `BOARD_LCD_MIRROR_X`，左右翻转改 `BOARD_LCD_MIRROR_Y`；
触摸落点上下反了改 `BOARD_TP_MIRROR_X`，左右反了改 `BOARD_TP_MIRROR_Y`。

**字库**：`lv_font_conv` 生成 LVGL 8 格式，所以 LVGL 锁在 8.4（LVGL 9 的字体格式不兼容）。
重新生成见 `tools/gen_fonts.sh`。Montserrat 只用它自带的图标字形（喇叭 / Wi-Fi / 播放键），
中文和图标不能放在同一个 label 里。

**字库来源**：`main/fonts/*.c` 由 `tools/gen_fonts.sh` 从 Noto Sans SC（SIL OFL 1.1）生成——
字体源按 commit 钉死、校验摘要、定重到 `wght=400`（Regular）后再转换，整条链路可复现。
字重是在真机上比过的——500 偏粗，400 合适；想再调改 `tools/gen_fonts.sh` 里的
`FONT_WEIGHT` 即可，可变字体 100–900 连续可选。换别的字体直接
`tools/gen_fonts.sh 路径/字体.ttf`，脚本对任意 TTF/OTF 都通用。逐项出处见
[`NOTICE.md`](NOTICE.md)。

**内存**：产品界面、联网和播放稳定后，内部 RAM 会随 HTTP 接收缓冲在约 40–61KB 间变化，PSRAM 剩约 5.45MB；
进入本机选网再离开后会释放扫描行，余量可恢复。连续运行 5 分钟未见持续下降或重启。
环形缓冲和解码工作区均使用 PSRAM。自检同时检查当前总余量、历史最低余量和最大连续块，避免把拉流阶段的正常瞬时占用误判为泄漏。

## 串口产品诊断

连接 USB 串口后可直接输入命令：

```text
selftest                         # 屏幕反显、目录、音频、RAM/PSRAM 总检
status                           # Wi-Fi、播放、音量、频谱、缓冲、当前台
catalog 0 10                    # 查看目录片段
regions / categories            # 查看地区和内容分类及数量
filter <地区序号> <分类序号>      # 设置台库筛选
play <台库序号> / stop / next / prev
volume <0-100>
screen <player|stations|filter|settings|appearance|theme-color|wifi|wifi-pass|about|ota|diagnostics>
drawer <open|close>
display <on|off>                # 息屏/唤醒；不影响音频和睡眠定时
sleep <off|seconds>             # 产测用短计时，验证到期停播并息屏
theme <light|dark|auto>         # 切换外观模式
accent <0-3>                    # 紫罗兰 / 青绿 / 暖橙 / 玫红
accent hue <0-359>              # 自定义色相
schedule <开始小时> <结束小时>   # 自动暗色时段，例如 22 8
```

`selftest` 同时验证 `BOARD_LCD_INVERT_COLOR=true`，用于阻止 IPS 屏白黑方向回归。

## 后续可扩展

- AAC 解码、HLS 支持
- SD 卡导入电台、Web 配置页改电台列表
- 麦克风通路（硬件有，本版没用）
- WS2812B 灯效

## 四个最容易踩的坑

1. **I²S 方向**：`dout = GPIO8`、`din = GPIO6`。原理图上的网络名 `I2S_DO`/`I2S_DI`
   是**编解码器视角**，照着填会把收发接反。详见 [hardware.md](docs/hardware.md#1-i²s-数据方向gpio6-vs-gpio8-️-最重要)。
2. **功放默认静音**：GPIO1 被 10K 上拉到 3V3，要**拉低**才出声。
3. **背光默认灭**：GPIO45 要主动拉高。
4. **IPS 版屏要开反显**：`esp_lcd_panel_invert_color(panel, true)`。不开的话白底黑字
   会显示成黑底白字，而且很容易被当成"深色主题"忽略过去。详见
   [hardware.md](docs/hardware.md#9-ips-版-ili9341-必须开反显)。

## 系统更新

发布流程、版本号与 tag 约定、发布产物去向，以及签名私钥的存放约束，全部见
[`docs/release-process.md`](docs/release-process.md)。预编译镜像见 [Releases](../../releases)。

设备联网后会自动检查更新，但不会自动安装。仅当服务器版本严格高于本机时，首页才显示更新横幅；
点击横幅或进入“设置 → 系统更新”可以查看最新版本和更新内容，再由用户启动安装。版本比较支持
`v` 前缀、不同长度的数字段、预发布标识和构建元数据，因此 `1.2.0-test` 不会被服务器上的
`1.1.0` 误判为可更新。

生产构建通过 CMake 缓存变量配置公开的清单地址，例如：

```bash
tools/idf.sh -D RADIO_OTA_URL=https://updates.example.com/radio-os/manifest.json build
```

清单包含 `version`、`url`、`sha256`、`notes` 和 `signature`。固件使用内置 P-256 公钥验证
ECDSA-SHA256 签名，再流式校验下载镜像的 SHA-256、工程名和版本；只有所有校验通过才切换
备用 OTA 分区。旧的已签名清单也不能触发降级。当前发布清单和镜像位于
`http://8.138.130.141/radio-os/`（与 `CMakeLists.txt` 的 `RADIO_OTA_URL` 默认值一致），
设备因所在网络的 HTTPS 代理兼容问题使用签名 HTTP 直连；传输即使被篡改也会在签名或摘要
校验阶段被拒绝。私钥只保存在发布服务器，不进入仓库和固件。

1.1.2 将收藏图标改为 SVG 贝塞尔轮廓预生成的 24×24 A8 抗锯齿蒙版，空心与实心共用同一轮廓。
运行时不需要 SVG 解析器，也不再触发 LVGL 8 凹多边形限制。中心能量条使用 PCM RMS 响度与低中频混合能量，
其余频段以 18% 权重提供真实起伏，并叠加从中心向外的固定衰减包络；低电平音频下仍有稳定的视觉锚点。
`selftest` 同时探测 LVGL 界面锁，若渲染任务阻塞会明确报告 `ui=blocked`。

1.1.3 将 Wi-Fi 密码页改为顶栏、输入区、键盘三段式 flex 布局，显式清除 LVGL 主题默认的行间距。
键盘占满剩余 140 px，与密码框无空白衔接，并通过串口输出实际布局坐标便于回归验收。

## 请我喝杯咖啡

如果这个项目对你有帮助，可以通过微信支持后续开发。感谢每一份鼓励。

<img src="docs/assets/wechat-donation.jpg" width="220" alt="微信收款码">

## 许可

MIT，见 [`LICENSE`](LICENSE)。第三方组件、字体、电台数据与素材的逐项出处与许可见
[`NOTICE.md`](NOTICE.md)。

作者 BI8SYN。
