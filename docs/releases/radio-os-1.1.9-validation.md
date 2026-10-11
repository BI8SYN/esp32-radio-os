# Radio OS 1.1.9 发布验证

日期：2026-10-11（Asia/Shanghai）。

- 发布基于 `main` 的 1.1.8；不包含 `port/cyd-esp32` 板级移植。
- 主机应用策略与旧设置兼容性测试通过。
- 完整构建、发布包 SHA-256 校验、版本号与 OTA 说明长度检查通过；说明 133 字节。
- 本地发布镜像真机从 `0x420000` 启动：`version=1.1.9 catalog=543`。
- 国内台对照：`player=playing rate=44100 bitrate=64 net_kbps=112 reconnects=0 underruns=0`。
- `SELFTEST PASS invert=INVON catalog=543 codec=ok semver=ok ui=ok`。
- 签名服务器派生公钥与固件内置公钥一致；私钥始终留在服务器。
- 6 路香港 MP3 源电脑经 Surge 可拉流并短时解码，开发板直连则 TCP 超时；该限制已写入发布说明，不宣称设备网络可收听。

本地最终保留升级前的原始 1.1.8 镜像（542 台）；OTA 只提供更新，不自动安装。
## 正式发布复检

- GitHub CI 与 Release 工作流均通过，tag 为 `v1.1.9`。
- GitHub 正式 OTA 镜像大小 2740464 字节，SHA-256：
  `1eadabdf49734656b946e8a0d0baf7c2bfa106b07f3846d620e3b9737e7a6f57`。
- 正式镜像已在 ES3C28P 上从 `0x420000` 启动，确认 `version=1.1.9 catalog=543`。
- 正式镜像国内台对照：`player=playing rate=44100 bitrate=64 net_kbps=72 reconnects=0 underruns=0`；`SELFTEST PASS`。
- 公网重新下载的镜像与 GitHub Release 资产逐字节一致；版本、应用身份、SHA-256 和 ECDSA 签名均通过本地独立检查。
- 线上清单已原子替换；旧版回滚副本：`manifest.json.rollback-1.1.8-20261011T022231Z`。
- `ota/manifest.json` 保存本次已签名清单快照。

- 最终设备已恢复此次更新前的原始 1.1.8 应用备份，Flash 写入摘要校验通过；`version=1.1.8 catalog=542 player=playing`，自检通过。
- 原版设备已验证签名清单并报告 `OTA state=available running=1.1.8 available=1.1.9`；未执行 OTA 安装。
