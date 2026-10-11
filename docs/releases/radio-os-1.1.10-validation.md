# Radio OS 1.1.10 发布验证

日期：2026-10-11（Asia/Shanghai）。

- 以 `main` 的 1.1.9 为基础，仅替换六路香港 MP3 入口域名及同步目录生成器；不包含 CYD2432 移植。
- 主机应用策略与旧设置兼容性测试通过；目录校验确认其他 537 台不变、总数 543、生成器六路地址一致。
- `tools/package_radio_release.sh 1.1.10` 完整构建通过；OTA 发布说明为 138 字节。
- 固件内置公钥与签名服务器派生公钥逐字节一致；私钥始终留在服务器。
- 烧录前串口确认设备从 `ota_0=0x20000` 启动；只写应用分区，Flash 摘要校验通过，保留 NVS。
- 原 MP3 域名在板上的 DNS 结果不可用；CDN 别名在开发版两轮短时六台播放验证成功。
- 开发版重复切台出现过一次失败，尚未修复；该限制已写入正式发布说明，不宣称长期稳定性验收通过。

本地正式版本六路全部通过选台、解码及连续下载检查（22050 Hz、32 kbps、重连 0、缓冲耗尽 0），`SELFTEST PASS`。音量 83、收藏 0 保留。

## 正式发布复检

- GitHub CI 与 Release 工作流均通过；tag `v1.1.10` 指向 `1e557ce`。
- 正式 GitHub OTA 镜像为 2740464 字节，SHA-256：`b44134a462509a46bf6491f893708835bbb43b38c7f8aa58bb2018d3d8d42843`。
- 从签名服务器取回的暂存清单，版本、URL、应用身份、镜像 SHA-256 和 ECDSA 签名通过本地独立检查。
- 私钥仅在签名服务器使用，没有进入仓库或构建产物。


- 正式 GitHub 镜像已在 ES3C28P 上从 `0x20000` 启动，`version=1.1.10 catalog=543`，Flash 摘要校验通过。
- 正式镜像国内台对照：`player=playing rate=44100 bitrate=64 net_kbps=93 reconnects=0 underruns=0`，`SELFTEST PASS`。
- 线上清单已原子替换，旧版回滚副本为 `manifest.json.rollback-1.1.9-20261011T071230Z`。
- 公网重新下载镜像与 GitHub Release 资产逐字节一致，清单亦与暂存签名清单一致；版本、应用身份、SHA-256、ECDSA 签名复核通过。
- `ota/manifest.json` 归档本次已签名清单；设备保留正式 1.1.10，Wi-Fi、音量 83、收藏 0 保留。
- 正式 GitHub 镜像香港第 1 台专项复检通过：`player=playing rate=22050 bitrate=32 net_kbps=58 reconnects=0 underruns=0`，自检通过，设备最后停留香港第 1 台。
