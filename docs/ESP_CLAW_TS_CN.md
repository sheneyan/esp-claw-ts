# ESP-Claw TS 中文指南

[English](./ESP_CLAW_TS.md)

## 能力边界

ESP-Claw TS 是 [ESP-Claw](https://github.com/espressif/esp-claw) 的非官方社区
分支，通过仓库中固定版本的
[MicroLink](https://github.com/Csontikka/microlink) 子模块增加可选的 Tailscale
兼容网络连接。

当前经过验证的用途有明确边界：ESP32 加入 tailnet 后，获得授权的远端节点可以
访问 ESP-Claw 网页和 WebSocket 聊天；ESP32 自己发起的出站流量也可以选择通过
exit node 转发。首版不作为通用 subnet router 提供支持或宣传。

本项目与乐鑫、Tailscale 均无隶属或背书关系。这里的兼容性也不代表已经实现
Tailscale 官方客户端的全部功能。

## 已验证配置

目前完成实机验证的配置是：

- 带 16 MB Flash、8 MB 八线 PSRAM 的通用 ESP32-S3 开发板（`N16R8`）
- 2.4 GHz Wi-Fi STA 上联网
- ESP-IDF 5.5.4
- 板型 `esp32_s3_n16r8_ts_claw`
- 仓库 gitlink 固定的 MicroLink 版本
- 使用 Auth Key 接入 Tailscale 控制服务
- 局域网网页访问，以及通过 tailnet 访问网页和 WebSocket 聊天

当前板型按 16 MB QIO Flash 和 8 MB 八线 PSRAM 配置。只标有“ESP32-S3”并不
足够，烧录前仍需确认模块的 Flash 和 PSRAM 规格。

## 前置条件

编译前准备：

- 支持 submodule 的 Git
- ESP-IDF 5.5.4 及其工具链
- ESP Board Manager（由项目的 managed components 提供 `idf.py bmgr`）
- 可以传输数据的 USB 线
- 可用的 Wi-Fi，以及你有权添加设备的 tailnet

下面使用常见的 ESP-IDF 安装路径；如果你的 ESP-IDF 位于其他目录，请调整
`source` 命令。

## 克隆并初始化 MicroLink

递归克隆会取回仓库指定的 MicroLink 版本：

```bash
git clone --recurse-submodules https://github.com/sheneyan/esp-claw-ts.git
cd esp-claw-ts
```

如果已经使用普通方式克隆，可以补充执行：

```bash
git submodule update --init --recursive
```

确认子模块状态：

```bash
git submodule status --recursive
```

MicroLink 所在行应以提交 ID 开头，而不是 `-`。

## 编译与烧录

在仓库根目录执行：

```bash
cd application/edge_agent
source "$HOME/esp/esp-idf/export.sh"
idf.py bmgr -c ./boards -b esp32_s3_n16r8_ts_claw
idf.py build
idf.py flash monitor
```

如果电脑同时连接多个串口设备，需要显式指定 ESP32 端口：

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Linux 上常见的端口名称是 `/dev/ttyACM0`。使用 `Ctrl-]` 退出串口监视器。

> [!WARNING]
> 完整项目烧录会写入 storage 分区，可能覆盖原先存放在 `/fatfs` 下的可变文件。
> 重新烧录前请自行备份。Wi-Fi 和 ESP-Claw TS 设置存放在独立的 NVS 中，但这
> 不等于备份保证；`erase-flash` 等命令同样会清除 NVS。

## 首次启动与 Wi-Fi 配网

没有有效 STA 凭据时，开发板会开启名称类似 `esp-claw-84AEE5` 的配网热点。
手机或电脑连接热点后打开：

```text
http://192.168.237.1/
```

进入 Wi-Fi 设置，手工填写 2.4 GHz Wi-Fi 的 SSID 和密码，保存后重启设备。
STA 成功联网后，配网热点通常会自动关闭。

如果扫描 SSID 时提示 `esp_wifi_scan_start failed`，可以直接手工填写 SSID；
这个错误本身不代表 STA 一定无法连接。

当前板型可使用 BOOT 按钮恢复：

- 按住 3 至 9 秒后松开：重新打开配网热点。
- 按住至少 10 秒后松开：恢复出厂设置并重启。

恢复出厂设置会删除应用、Wi-Fi 和 ESP-Claw TS 的已保存配置。

## 加入 Tailnet

在准备使用的控制服务中创建 Auth Key。如果部署条件允许，优先使用权限范围较小、
短期或一次性的密钥，并始终把它当作机密信息处理。

在设备网页中打开 **Tailscale**，填写：

- **启用 Tailscale：** 打开
- **Auth Key：** 新建的密钥；该字段只写入、不回显
- **设备主机名：** 例如 `esp-claw`
- **登录服务器：** 使用默认服务时留空；需要兼容的自建控制服务时再填写
- **Exit Node：** 初次配置保持“不使用”，先走普通 Wi-Fi 出口
- **最大节点数：** 建议保持默认值 `16`；允许范围是 1 到 64，数值越大占用内存
  越多

保存设置后重启。保存成功只代表配置通过校验，不代表隧道已经连接。重启后回到
Tailscale 页面，确认：

- 连接状态为“已连接”
- 显示 `100.64.0.0/10` 范围内的 Tailscale IP
- 能看到连接路径和节点数量
- “最近错误”为空

设备还可能需要在 tailnet 管理后台中批准。Tailnet ACL 或 grants 也必须允许来源
节点访问这个设备。

## 验证局域网与 Tailnet 访问

先使用网页顶部或串口日志显示的局域网 IP 验证 ESP-Claw 服务：

```bash
curl --noproxy '*' -fsS http://设备局域网IP/api/webim/status
```

再从另一个已授权的 tailnet 节点验证设备和连接路径：

```bash
tailscale status
tailscale ping esp-claw
curl --noproxy '*' -fsS http://设备TAILNET_IP/api/webim/status
```

`tailscale ping` 如果显示 `via <局域网IP>:<端口>`，表示当前是直连；如果显示
`via DERP(...)`，则经过中继，延迟通常更高且波动更大，但这并不一定表示故障。

最后在浏览器打开 `http://设备TAILNET_IP/` 并发送一条聊天消息，同时验证 HTTP
页面和 WebSocket。仅仅 ping 成功，并不能证明浏览器代理、ACL、HTTP 与 WebSocket
链路全部正常。

## 智能体可感知的 Tailscale 控制

内置的 `tailscale_network` Skill 会激活 `cap_tailscale` Capability Group，并让
智能体理解以下五个工具：

| 工具 | 用途 |
| --- | --- |
| `tailscale_status` | 读取连接、所选 Exit Node、实际出口、DNS 兼容出口/数量、错误、节点数、计时、重连计数和有界的 DERP 诊断。 |
| `tailscale_list_exit_nodes` | 列出当前可见的 Exit Node 候选，包括在线/直连状态及已知的 DERP 区域。 |
| `tailscale_set_exit_node` | 按规范 CGNAT IP 或无歧义的主机名选择一个在线节点。 |
| `tailscale_clear_exit_node` | 关闭 Exit Node 路由，让设备自身流量恢复使用 Wi-Fi。 |
| `tailscale_reconnect` | 在不修改注册设置的情况下重连本设备的 Tailscale 运行时。 |

示例请求包括“查看我的 Tailscale 状态和 DERP 诊断”和“列出可用的 Exit Node”。
这些读取操作无需确认。运行时能够提供时，`tailscale_status` 会返回当前/默认 DERP
区域、数量有上限的各区域 RTT 样本、心跳/控制消息时长及重连计数。Exit Node 列表
为空表示本节点当前看不到候选，并不能证明整个 tailnet 中不存在 Exit Node。

例如，状态读取使用空对象，并可能返回下面这样的 DERP 摘要（区域名称和 RTT 来自运行时
实测）：

```text
tool: tailscale_status
input: {}
result excerpt: {"connected":true,"path":"derp","derp":{"active":{"id":4,"name":"<region>"},"rtts":[{"region":{"id":4,"name":"<region>"},"rtt_ms":84,"timed_out":false}]}}
```

DERP 数据是 ESP32 的 Tailscale 运行时针对中继路径提供的遥测，不是逐跳的互联网
traceroute，也不能列出设备与目标之间的每一段网络。使用中继或某个 RTT 样本超时是
排障证据，但单独出现时不能证明连接已经损坏。

智能体发起任何变更，都必须来自当前用户在当前交互中的明确请求，且工具调用必须包含
`user_confirmed: true`。智能体不得从之前的消息或诊断结果推断授权。当前网页提供实时
选择和清除 Exit Node；用户执行任一操作就直接确认了该网页操作，因此对应 HTTP 请求
不使用智能体专属的 `user_confirmed` 字段。重连可通过已确认的
`tailscale_reconnect` 智能体工具和 `/api/tailscale/reconnect` HTTP API 使用，网页
当前没有重连按钮。网页、智能体工具和直接 API 路径共用同一个串行化实时控制服务，
因此共享同一套行为，但各传输层的返回结构并不相同。例如 HTTP 变更结果会暴露
`rollback_attempted` 和 `rollback_recovered`，Capability 结果不包含这两个字段。

`user_confirmed` 和直接网页选择只记录当前操作意图，并不构成身份认证或访问授权。直接
HTTP 变更路由没有额外认证封装。应以可信局域网暴露方式和 tailnet ACL 或 grants 限制
设备可达范围；否则能够访问这些 HTTP 路由的主体也可以调用它们。

智能体不能修改设备主机名、Auth Key、登录服务器、启用状态或最大节点数；这些仍是网页
中的设置项。清除或重置设备身份属于另一条路径：必须使用前文的物理恢复出厂操作，不是
智能体或普通设置页面操作。注册和服务器校验不属于智能体工具。

## Exit Node 行为

初始配置先保持 **Exit Node：不使用**。设备连接后，Tailscale 页面会列出当前节点
能够看到的 exit node。选择节点、清除选择，或让已确认的智能体执行任一操作，都会
立即生效；只修改 Exit Node 无需重启。仅当运行时确认到达请求状态后，成功操作才会
持久化。

状态页会区分“配置的节点”和“实际出口”：

- `exit`：选中的 exit node 健康，ESP32 自己发起的出站流量经它转发。
- `wifi`：当前走普通 Wi-Fi 出口。
- `fallback`：仍配置了 exit node，但它当前不可用，出站流量已回退到 Wi-Fi。

### DNS 兼容策略

当 `egress` 为 `exit` 时，ESP-Claw TS 会按实际路由分类 lwIP/DHCP 当前提供的 IPv4
DNS：私网/链路本地解析器及捕获到的公网解析器走 Wi-Fi STA；CGNAT 解析器（包括
`100.100.100.100`）走 WireGuard。其他公网流量仍走 Exit Node。状态提供
`dns_egress`、`dns_bypass_active` 和有界的 `dns_bypass_count`，但不会把解析器地址暴露
给智能体或网页：

- `sta`：所有有效解析器均走 STA；
- `exit`：所有有效解析器均走 WireGuard/Exit Node；
- `mixed`：有效解析器同时存在两种路径；
- `unavailable`：当前没有已知可用的解析器路径。

`dns_bypass_count` 只统计公网精确 IP 的 STA 旁路，不统计私网、链路本地或 CGNAT
解析器。网站通常看到的是 Exit Node 公网 IP；`sta` 模式下本地解析器或运营商可以看到
查询域名，`mixed` 模式下可看到走本地路径的部分查询。DNS 返回、CDN 地理调度或 DNS
污染可能影响可达性或内容选择。`exit` 不代表存在本地 DNS 泄漏；`unavailable` 表示
域名解析可能失败。这是兼容性行为，不应描述为隐私 VPN。路由钩子只能看到目标 IP，
无法识别端口，所以
命中所捕获公网解析器 IP 的**全部流量**都会走 STA，并非只有 UDP/TCP 53 端口。
Exit Node 生效期间会从 lwIP 刷新该列表；进入 fallback、清除或回滚切换、Wi-Fi 断开、
运行时销毁时都会清空，避免遗留旧地址。

恢复过程采用保守策略：exit node 必须连续探测成功后才会重新切回。这个策略只控制
ESP32 自身发起的出站流量；它不会把 ESP32 宣告为 exit node，也不会替其他局域网
转发流量。

离线候选会被拒绝，不会被选中。设置操作也会拒绝空选择；要停用 Exit Node，必须使用
明确的清除操作。若运行时应用失败或超时，操作会报告失败，运行时控制会尝试恢复之前
的安全状态。若实时切换后持久化失败，共享服务会尝试把运行时回滚到上一次已持久化的
选择，并报告是否尝试回滚及是否恢复。持久化结果无法确认时，当前运行时可能已经恢复，
但下次重启仍可能使用不同的选择；不得把 fallback、部分生效或回滚失败描述成成功。

fallback 状态机和出口选择已有主机测试覆盖；实机验证过普通 Wi-Fi 出口及配置
exit-node 的路径，但不同网络环境在依赖该行为前，仍应自行进行受控的在线/离线测试。

## 硬件建议

| 硬件 | 状态 | 说明 |
| --- | --- | --- |
| ESP32-S3 N16R8 | **已验证，推荐** | 与当前 16 MB 分区、八线 PSRAM、构建配置和实机运行结果一致。 |
| ESP32-S3 N8R8 | 需要适配 | CPU 和 8 MB PSRAM 理论上可用，但当前 16 MB Flash 分区及板型不能直接用于 8 MB Flash。 |
| ESP32-C5 | 未验证候选 | 无线特性有吸引力，但应用、依赖、板型、USB 行为、内存预算和实际运行均未完成移植验证。 |
| ESP32-P4 | 不建议用于当前极简设计 | 需要额外网络协处理器，不再是单板 Wi-Fi 方案。 |
| ESP32-C3、ESP32-C6、ESP32-H2、ESP32-WROOM-32 开发板 | 本版本不支持 | 常见配置的内存、PSRAM、连接能力、目标或板型与已验证的 S3 配置不同。 |

ESP-IDF 或依赖库支持某个芯片系列，并不能证明 ESP-Claw TS 已经适配某块具体开发板。

## 已知问题与排障

### 电脑浏览器显示 HTTP 502，但手机可以访问

通常是电脑浏览器把私网地址送进了 HTTP 或 SOCKS 代理。先绕过代理验证：

```bash
curl --noproxy '*' -v http://设备局域网IP/api/webim/status
curl --noproxy '*' -v http://设备TAILNET_IP/api/webim/status
```

把设备的局域网 IP 和 tailnet IP 作为精确地址加入浏览器扩展的不代理列表。如果代理
工具支持 CIDR，再加入设备所在局域网网段和 `100.64.0.0/10`。不同扩展的规则语法
并不一致，有些扩展不会按预期处理 CIDR 或通配符，因此使用精确 IP 或临时切换
**Direct/直连**模式是最可靠的排查方式。

### 已保存配置，但设备仍显示未连接

修改启用状态、Auth Key、主机名、登录服务器或最大节点数等注册设置后需要重启；实时
设置/清除 Exit Node 及重连操作无需重启。然后查看 Tailscale 页面或
`/api/tailscale/status`，并逐项确认：

- Wi-Fi STA 已连接，DNS 和时间正常
- 注册时 Auth Key 有效
- 控制后台中设备已经获批
- 主机名和最大节点数通过校验
- 来源节点符合 tailnet ACL 或 grants

Auth Key 只写入、不回显。后续保存时把该字段留空会保留原密钥，不会导出或显示它。

### 连接速度慢

执行：

```bash
tailscale ping esp-claw
```

DERP 中继延迟取决于双方到中继区域的互联网链路。应对比多次结果，并观察双方是否
最终协商成直连。在没有实测前，不要把 ESP32 链路当作大文件传输通道。

### 局域网可以访问，但 tailnet 不行

确认 Tailscale 页面显示“已连接”和 tailnet IP，再检查设备授权、密钥有效期、
ACL/grants，以及远端客户端是否接受 tailnet 路由。先测试精确 IP，再排查 DNS 名称。

### 重新烧录后文件消失

这个项目的 `idf.py flash` 会包含生成的 storage 分区。请从自己的备份恢复所需的
`/fatfs` 内容。NVS 配置与 `/fatfs` 文件属于不同的持久化边界，二者都不能替代备份。

## 安全说明

- 不要提交 Auth Key、导出的配置、NVS dump 或设备身份材料。
- 优先使用权限收敛、短期的注册密钥。
- 能从 tailnet 寻址不等于已经完成授权，仍需维护 ACL 或 grants。
- 设备网页使用 HTTP。Tailnet 会保护 overlay 链路，但局域网客户端仍是明文 HTTP，
  除非额外增加可信的保护层。
- 第三方依赖及其许可证需要与顶层项目许可证分别审查。

## 上游项目与许可证

上游项目是 [`espressif/esp-claw`](https://github.com/espressif/esp-claw)。
ESP-Claw TS 保持上游 Apache License 2.0 [`LICENSE`](../LICENSE) 不变，并保留原始
版权声明。

包括 MicroLink 在内的第三方依赖和子模块继续适用各自的版权及许可证条款；被本项目
引用并不意味着它们被重新许可为仓库顶层许可证。
