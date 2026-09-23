# 实机 UI 回归

需要真实 ESP32-S3、Python `pyserial`，以及 `RADIO_UI_TEST=ON` 的开发固件：

```sh
tools/idf.sh -D RADIO_UI_TEST=ON build
```

截图还要求 `CONFIG_LV_USE_SNAPSHOT=y`（当前非 minimal LVGL 默认启用）。**先确认芯片、读取真实分区表和当前启动分区、保存回退镜像，再只烧应用分区**；不能把某台设备的偏移写成通用烧录命令。

```sh
python tests/device/serial_ui.py --port /dev/cu.usbmodemXXXXX --output backup/ui-regression
```

脚本会开关主题、播放电台、短时设置睡眠、添加再移除当前台收藏、拖动音量，并截图。会尝试恢复音量和外观；最后选择的电台可能改变。使用测试设备并保留原设置备份；中断/断言失败后应检查和恢复测试状态。脚本不提交 Wi-Fi 凭据、不安装 OTA。

开发探针提供 `uitest state`、`uitest frame`、`uitest touch x y [end_x end_y]`。触摸注入走 LVGL 输入驱动，而非直接调用按钮回调。截图在开发板上重新渲染当前 LVGL 页面，经串口返回 RGB565 数据；它可以验证画面内容和布局，**不能验证实体面板颜色、真实手指触摸坐标或扬声器听感**。

输出包含页面 PNG、断言结果及已遮蔽 SSID 的状态日志。Wi-Fi 扫描截图可能出现附近网络名称，保存在 `backup/`，不要提交到公开仓库。

完成回归后重新构建普通固件并烧录、自检：

```sh
tools/idf.sh -D RADIO_UI_TEST=OFF build
```

普通构建不链接 `ui_probe.c`，不增加诊断任务栈，不暴露注入/截图命令。发布打包会强制关闭此选项，防止本地 CMake 缓存误带测试入口。

随机换台专项（同样要求 `RADIO_UI_TEST=ON`）：

```sh
python tests/device/shuffle_ui.py --port /dev/cu.usbmodem21201 --output backup/shuffle-check
```

覆盖开关不打断播放、随机下一台/历史上一台、主题重建、12 小时制与睡眠倒计时、
相邻 Wi-Fi 按钮、息屏唤醒隔离。专项结束保留随机开启，便于重启后检查 `uitest state`
中的 `shuffle=1`；测试会改动主题、时间格式和电台，交付前应恢复测试前 NVS。
