# 寒淋输入法词库云同步（一期：手动迁移）

本文说明寒淋输入法（HanLin-IME）如何把本机词库、用户词频权重与全部配置同步到自建的 HanLin Sync 服务，
以及在换机、恢复和冲突时怎么操作。一期的目标是「换设备能快速部署、词频不丢」，
不追求全自动后台同步。

## 1. 组成

| 组件 | 位置 | 作用 |
| --- | --- | --- |
| HanLin Sync 服务端 | `D:/hanlin-release/hanlin-sync-service`（交付材料，未包含在本仓库） | 单进程 Node 22+，只监听 `127.0.0.1:18080`，公网入口由 Cloudflare Tunnel 提供 |
| `src/Sync-HanLin.ps1` | 本仓库 | 配对、上传、拉取、恢复、状态与历史 |
| `HanLinHost.exe --sync-user-data <profile>` | 本仓库 | 调用 librime 一次性导出/导入 `*.userdb.txt` 词频，不进入桌面后端互斥、不注册 TSF |

服务端协议见 `hanlin-sync-service/docs/PROTOCOL.md`，部署和运维见同目录
`deploy/README.md` 与 `docs/USER-ACTIONS.md`。

## 2. 同步范围

上传（服务端 allowlist 白名单）：

| 类别 | 内容 |
| --- | --- |
| 配置 | 根目录全部 `*.yaml`（`default.yaml`、`rime_ice.schema.yaml`、各 `*.custom.yaml`、`weasel.yaml` 等）与 `*.yml` |
| 常用语 | `custom_phrase.txt`、`sogou_phrases.txt` |
| 词库 | `cn_dicts/`、`en_dicts/` 下的词表（如 `tencent.dict.yaml`、`base.dict.yaml`、`ext.dict.yaml`、`cn_en.txt`） |
| 词库与皮肤资源 | `lua/`、`opencc/`、`Skin/`、`skins/` |
| 用户词频权重 | `sync/<installation_id>/*.userdb.txt`（由 librime 导出与合并） |

不传（机器相关、可重建或备份）：

- `installation.yaml`：含本机 `installation_id`，跨机覆盖会破坏 librime 的用户目录身份
- `user.yaml`、`stats.log`：机器相关状态与统计
- LevelDB 目录（`*.userdb/`）、`build/`、日志
- 备份文件（`*.bak-*`、`*.pre-*`、`*~`、`*.tmp`）
- 单文件超过 90 MiB 的内容（服务端与客户端都会拒绝）

词频合并由 librime 的 `user_dict_sync` 任务完成：它导入 `sync/*/*.userdb.txt` 中其他设备的词条，
再把本机词库导出到 `sync/<本机 id>/`。同一词条重复导入不会继续抬高词频。

## 3. 前置条件

1. 服务端已部署并可从本机访问（正式：`https://sync.yanghanlin.cn`）。
2. 服务器已启用动态验证码配对。打开 Authenticator 的「Rock5b+」条目，准备当前六位码。每个时间步在寒淋云同步中只能配对一次。若刚为寒淋笔记配对，等待验证码刷新即可。

3. 本机已安装寒淋输入法，且 `HanLinHost.exe` 与它的 `data` 目录可用。

## 4. 使用

配对后默认每天自动同步一次词频，可以在「设置 → 数据」关闭。输入法只会在键鼠至少空闲两分钟、且没有正在编辑的拼音时开始后台同步。下载与上传在后台进行，合并或导出时短暂重启输入法后端。当天未完成会在后续空闲时重试。

```powershell
# 配对（命令会隐藏输入；设备令牌由 Windows DPAPI 保护）
pwsh -File src\Sync-HanLin.ps1 pair -DeviceName "我的笔记本"

# 上传：先本机导出词频，再上传变更文件并提交新版本
pwsh -File src\Sync-HanLin.ps1 push

# 查看状态：远端最新版本、本地基线、本地导出文件
pwsh -File src\Sync-HanLin.ps1 status

# 拉取远端最新版本：下载词频到 sync/<远端的 installation_id>/，再调用 HanLinHost 合并导入
pwsh -File src\Sync-HanLin.ps1 pull

# 恢复某个历史版本（只改本地，不会自动上传）
pwsh -File src\Sync-HanLin.ps1 restore -Commit <版本 id>

# 最近 20 个版本
pwsh -File src\Sync-HanLin.ps1 history
```

参数：

- `-UserData`：Rime 用户目录，默认 `%APPDATA%\Rime`。便携部署可指向 `D:\桌面\HanLin-IME\Data` 之类的目录。
- `-HostPath` / `-SharedData`：显式指定 `HanLinHost.exe` 与共享数据目录；默认自动查找。
- `-IncludeConfig`：pull/restore 时按相对路径写入配置、词库与皮肤（写入前备份 `sync/` 与全部 yaml/txt）。
  不加时只同步词频文件。没有变化时不创建新版本。

## 5. 换机流程

1. 新机安装寒淋输入法，打开一次让它生成 `installation.yaml`（`installation_id` 每台机器独立）。
2. 在新机输入 Authenticator 当前六位动态验证码，完成配对。
3. 执行 `pull -IncludeConfig`：下载并校验全部文件；配置、词库与皮肤按相对路径写入用户目录
   （写入前备份到 `.hanlin-sync-backup\<时间戳>-pull`），词频写入 `sync\<源设备 id>\` 后调用
   `HanLinHost --sync-user-data` 合并。
4. 在寒淋输入法里「重新部署」一次，再输入几个常用词验证候选顺序与自定义短语。
5. 之后在新机 `push` 一次，让云端记录新设备的词库状态。

新机第一次 `pull` 时如果还没有输入过任何词条（没有 LevelDB 用户词典），librime 只完成导入准备、
不生成 `userdb.txt`；先输入几个词再 `push`，本机词频才会进入云端。

## 6. 冲突与并发

一期按「单写者」设计：

- `push` 前会检查远端 head 与本地基线。远端已被其他设备推进时，`push` 拒绝执行，
  提示先 `pull`；确认要以本机覆盖远端时才加 `-Force`。
- 两端同时修改同一台机器的词库时，librime 的词频合并是加法/衰减语义，一般不会丢词条；
  仍建议一次只在一台设备上 push。
- 服务端每个 app 一条线性历史，parent 必须等于当前 head，旧父提交会返回 `409`，脚本会报告。

## 7. 备份与回退

- `pull`/`restore` 前会把 `sync/` 目录和将被覆盖的顶层配置复制到
  `<UserData>\.hanlin-sync-backup\<时间戳>-pull\`。
- 下载先落到 `.hanlin-sync-staging\<时间戳>\`，逐个校验大小与 SHA-256 后才写入用户目录。
- 恢复历史版本不会上传；要回退云端，请在新版本里用旧内容 `push`（会追加一个提交，不删历史）。
- 完全回退手动方式：把 `sync/<其他 id>/*.userdb.txt` 放在各自的 `sync/<源设备 id>/` 后执行
  `HanLinHost --sync-user-data <UserData>` 也能合并。

## 8. 安全与隐私

- 服务端不做端到端加密，运维者可以读取同步内容；只应部署在自己控制的服务器上。
- 公网入口必须 HTTPS；脚本只对 `127.0.0.1` / `localhost` 允许 http 测试地址。
- 令牌只在配对响应出现一次，服务端只存 SHA-256 摘要；本机用 DPAPI(CurrentUser) 加密保存，
  不写入命令行、日志或同步状态文件。
- 脚本不跟随 HTTP 重定向，避免令牌被带到其他主机。
- 令牌吊销在服务端立即生效（`--revoke ime <deviceId>`）。

## 9. 限制与后续

- 一期没有自动后台同步，也没有 IME 工具窗入口；需要手动执行脚本。
- 词频只交换 `*.userdb.txt`，不直接传 LevelDB 目录；本机从未输入过时不会生成 userdb。
- 单文件上限 90 MiB、单 app 配额 2 GiB；整套配置与词库约 64 MB，余量充足。
- `-IncludeConfig` 写入配置、词库与皮肤后，需要在输入法里重新部署才生效。
- 后续可做：工具窗「云端词库」页、定时检查、历史版本可视化、配置差异预览。

## 10. 故障排查

| 现象 | 处理 |
| --- | --- |
| `尚未配对` | 先 `pair`；验证码已用过或已过期就等手机显示下一码 |
| `401` / `invalid_token` | 令牌被吊销或服务器重装过，重新 `pair` |
| `409 parent_mismatch` | 远端已被其他设备推进，先 `pull`；确认以本机为准才 `-Force` |
| `本地词库同步失败` | 看 `<UserData>\sync-report.json` 的 `error`，确认 `rime.dll` 与 `data\build` 完整 |
| 拉取后候选没有变化 | 确认 `pull` 输出里「已写入 N 个词库文件」，并在输入法里重新部署一次 |
| 新机 `pull` 后没有词频 | 本机还没有 LevelDB 用户词典（没输入过），先输入几个词再 `pull`/`push` |
| 配置恢复后界面没变化 | 加 `-IncludeConfig` 写入后，在输入法里重新部署一次 |
| `共享数据目录不存在` | 用 `-SharedData` 指向含 `build/` 的 `data` 目录（安装目录或构建输出） |


## 桌面入口

「设置 → 数据 → 云同步」仅保留一个操作按钮。首先配对，随后可立即同步。日常词频同步与 CLI 的 `sync` 使用同一脚本，由脚本决定合并与上传顺序。输入法占用词库时延期处理，以保护当前输入。服务端可读取同步数据，客户端加密保存在本机的凭据。
