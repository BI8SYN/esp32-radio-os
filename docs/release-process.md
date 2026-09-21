# 发布流程

## 版本号

`CMakeLists.txt` 的 `PROJECT_VER` 是**唯一权威版本来源**——它被嵌进 bin，设备的设置页、
侧边栏和启动日志显示的都是它，OTA 的版本比较也用它。

tag 名为 `v<版本号>`（比较时去掉 `v` 前缀）。tag 与 `PROJECT_VER` 必须一致，否则
Release 的名字会和固件自报的版本对不上号，而这种错配装到设备上才会发现。

## 发布前必须做的

1. `tools/package_radio_release.sh <版本号>` —— 脚本会校验 `PROJECT_VER` 与参数一致、
   检查 `docs/releases/radio-os-<版本号>-ota-notes.txt` 存在且不超过固件可接收的
   191 字节，然后完整构建并在 `dist/radio-os-<版本号>/` 生成：
   - `radio-os-<版本号>-ota.bin` —— OTA 应用固件
   - `radio-os-<版本号>-0x0.bin` —— 从 `0x0` 烧录的整合固件
   - `SHA256SUMS`
2. 版本说明 `docs/releases/radio-os-<版本号>.md` 必须存在。设备上看不到它，但仓库里
   必须留下这一版改了什么——出问题时要靠它定位是哪一版引入的。
3. 完整构建通过、清单签名与摘要校验通过、真机自检（串口 `selftest`）通过。

`0x0` 整合固件用于全新烧录，**会清除 NVS 里的 Wi-Fi、收藏等本机设置**；这一点要写进
Release 说明。保留设置的升级走 OTA 或只烧 app 分区。

## 产物去向

二进制**不入库**。`dist/` 在 `.gitignore` 里，产物由 CI 上传到 GitHub Releases。
仓库里提交的是版本说明、OTA 发布说明和 `ota/manifest.json`（已签名清单快照）。

## tag 与历史

发布提交合入 `main` 后，创建带说明的 `v<版本号>` tag，并同时推送 `main` 和 tag。
**禁止覆盖已有 tag、强推或重写发布历史。**

## OTA 服务器

设备联网后自动检查更新，但不会自动安装；仅当服务器版本严格高于本机时首页才显示更新
横幅。默认清单地址由 CMake 缓存变量配置：

```bash
tools/idf.sh -D RADIO_OTA_URL=https://updates.example.com/radio-os/manifest.json build
```

清单包含 `version`、`url`、`sha256`、`notes` 和 `signature`。固件用内置 P-256 公钥验证
ECDSA-SHA256 签名，再流式校验下载镜像的 SHA-256、工程名与版本，全部通过才切换备用 OTA
分区并支持首启回滚。已签名的旧清单也不能触发降级。

线上清单**必须原子替换，并保留上一版回滚副本**。

**签名私钥只存放在发布服务器**，严禁进入本仓库、构建产物、日志或终端输出。因此 OTA
发布不在 CI 里做（那需要把服务器凭据放进公开仓库的 secrets），在本机执行。

## 授权边界

只有收到设备所有者明确的「发布」命令，才能签名发布包、创建版本 tag 或更新线上 OTA
清单。开发构建的烧录与实机验证不受此限（见 `AGENTS.md`）。
