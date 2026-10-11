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
正式 GitHub 产物、线上签名及回滚清单的最终核验记录在发布后补充。
