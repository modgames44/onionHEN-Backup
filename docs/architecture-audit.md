# OnionHEN 架构审计报告

基于 `docs/arch.md`、`docs/util_arch/` 与 `source/` 源码的静态审计。项目整体方向正确（双守护进程、`libonion_*` 抽公共库、ready 协议替代固定 sleep），主要风险集中在 **协议边界、标志生命周期、并发、注入/加载路径的实现漂移**。

> 审计日期：2026-07-10  
> 范围：`source/` 第一方代码（daemon / util / shellui / bootstrapper / fps_elf / libonion_* / libhijacker / libNineS），对照 `docs/arch.md`。

---

## 1. 模块地图与关联关系

```text
OnionHEN.elf (unpacker / LZMA)
        │
        ▼
bootstrapper.elf ──9021──► util.elf ──► kstuff.elf ──► daemon.elf
                              │                           │
                              │ Unix IPC                  │ ptrace inject
                              │ 0x8*                      ├─► shellui → SceShellUI
                              │                           └─► fps_elf → 游戏
                              │
              shellui / fps ──┼── crit: /system_tmp/onionhen/ipc/crit_service (0x9*)
                              └── util: /system_tmp/onionhen/ipc/util_service (0x8*)
```

| 层 | 模块 | 职责 |
|----|------|------|
| 启动链 | unpacker → bootstrapper | 解压、内存发送、顺序拉起 |
| Critical | daemon | Toolbox/FPS 注入、FS IPC、util 看门狗 |
| Utility | util | 金手指、下载、重业务 |
| UI | shellui / fps_elf | Mono 注入 / overlay |
| 共享库 | `libonion_{ipc,settings,proc,platform,ready,detour,payload}` | 协议/配置/进程/平台叶子能力 |
| 注入原语 | libhijacker / libNineS / libNidResolver | 劫持、ptrace ELF 注入、NID |

**依赖方向（目标态，大体已落地）：**

```text
shellui/fps ──► onion_ipc / settings / detour / ready
daemon/util ──► onion_ipc / settings / proc / platform / ready
cheats      ──► util_platform + pt/mdbg/kernel
NineS       ──► onion_proc（不再自带 proc 副本）
```

**运行时环状耦合（有意，但脆弱）：**

- util ↔ daemon（rest 后 toolbox 再注入 vs util 看门狗）
- daemon → shellui（inject）
- shellui → 双 daemon（IPC）

任一侧挂掉都会卡住 rest 恢复。

---

## 2. 架构 Bad Smell（结构债）

### 2.1 共享层「抽了一半」

| 已共享 | 仍散落在各模块 |
|--------|----------------|
| log/notify/fs (`libonion_platform`) | shellui 本地 `if_exists` 前向声明；bootstrapper 自有 notify 包装 |
| IPC 传输环 | 业务仍在 `daemon/util ipc_handle`（合理），但 **JSON 拼装/recv 语义** 仍脆弱 |
| Settings schema | 三套 `LoadSettings` 契约（void/bool/mtime） |
| proc 查询 | 尚可；elfldr/pt **三份分叉副本** |

**elfldr/pt 三叉戟（高维护成本 + 行为漂移）：**

- `bootstrapper/source/{elfldr,pt}.c`
- `libNineS/src/{elfldr,pt}.c`
- `unpacker/source/{elfldr,pt}.c`

NineS 已避免 per-call authid 翻转；bootstrapper/util attach 仍在翻转。util 金手指走 `pt_attach_proc`（翻转）+ NineS `pt_mmap`（不翻转）——**同一条路径两套策略**。

### 2.2 Settings：单 schema、三份真相 — **已落地（2026-07-10）**

- 进程内各有一份 store（daemon / util：`SettingsStore`；shellui：`Settings`）——多进程可接受。
- daemon/util：`SettingsStore` mutex + `snapshot`/`store`/`update`，消除 IPC 写与 worker 读竞争。
- Fan IPC：`update` 后 `settings_save` + `SettingsNoteDiskWritten`（twin 落盘）。
- 统一 `bool LoadSettings()`（缺文件用默认并成功）；util ODR 已修。
- mtime 门控：`settings_config_newest_mtime` 取双路径 max，任一 twin 更新即重载。

### 2.3 Ready 标志语义泄漏

文档称 `util_booted` 用于 rest-mode 延迟；**实现里冷启动几乎总会命中**：

1. util 先起 → `onion_ready_signal(util)` → 立刻 `signal(util_booted)`
2. daemon 再起 → `cmd_enable_toolbox` 见 `util_booted` → `sleep(rest_mode_delay_seconds)`

结果：`Rest_Mode_Delay_Seconds` 会拖慢 **首次** Toolbox 注入，表现为「toolbox 卡很久」。

历史实现只 clear util/kstuff/daemon/toolbox，**不清** `util_booted` / `fps_overlay` → 同 boot 再跑 HEN 时标志粘滞。当前 bootstrapper 会清理 runtime flags；Toolbox 标记则改为持久保存 SceShellUI PID，由 daemon 比较进程实例后自行失效。

### 2.4 浅模块 / 胖入口

- `shellui/src/prx.cpp` ~800+ 行、`toolbox_xml.cpp`、`daemon main` 仍偏厚；onpress 已表驱动，但生命周期/钩子入口仍难测。
- `util_platform` 是金手指正确 seam；daemon 侧 `get_*_pid` 仍是 0..9999 盲扫，未完全收敛到 `libonion_proc`。
- `BREW_LAST_RET`、`BREW_UTIL_TEST_CONNECTION` 等协议位 **名义存在、语义残废**（见下节）。

### 2.5 测试覆盖偏斜

- util 有 host 单测（settings/ready/cheats 等）——好。
- daemon inject、IPC 传输、elfldr 分叉、shellui 并发 IPC **几乎无对等测试**。  
  接口复杂、实现重的路径（注入、ptrace）反而不在可测 seam 上。

---

## 3. 可能的 BUG（按严重度）

### 高

| # | 位置 | 问题 |
|---|------|------|
| 1 | `daemon_inject.cpp` `cmd_enable_fps` / `_new` | **`SuspendApp(appid)` 把 appid 当 pid**。注释已写清 “APP PID NOT TO BE CONFUSED WITH APPID”，却在解析 pid 前 suspend；`_new` 甚至 **suspend 错 id、resume 对 pid**。可致停错进程或挂起失败。 |
| 2 | `daemon_jailbreak.cpp` ~164 | Jailbreak 重试条件 **`isProcessAlive` 逻辑反了**：进程存活时立刻 break（日志却写 “process died”）；应 `!isProcessAlive`。首次 `getHijacker` 失败即放弃，存活目标几乎无法重试。 |
| 3 | 同上 jailbreak 成功路径 | `spawned->jailbreak(true)` **无空指针检查**；失败路径仍 notify “granted jailbreak”。 |
| 4 | `libonion_ipc` `ipc_server.cpp` | 单次 `recv` 无组帧/`MSG_WAITALL`；短读 + `std::string(msg)` 可能未保证 NUL → 脏命令/崩溃。 |
| 5 | `ipc_server.hpp` `ipc_format_reply_body` | `out_var` **未 JSON 转义**；金手指状态、路径含 `"` `\` 时客户端解析失败。 |
| 6 | `daemon/ipc_handle.cpp` STAT/COPY/DELETE | `string_item` 默认 `nullptr`，缺 key 时 **`stat(NULL)` / `rmtree(NULL)`**（仅 CHMOD 有检查）。 |
| 7 | `memory_backends.cpp` `mapCodeCaveCommon` | `pt_mmap` **无 `MAP_FIXED`** 却要求返回地址 == `page_start` → code cave 回退基本必然失败。 |
| 8 | `bootstrapper/elfldr.c:672` | `e_shnum * sizeof(Elf64_Ehdr)` 应为 **`Elf64_Shdr`** → section 表缓冲 undersize / OOB。 |
| 9 | `libonion_detour/detour.cpp` | `mprotect` 失败只打日志，仍写 jump → 注入崩溃。 |
| 10 | NineS `elfldr.c` | `kernel_mprotect` 失败 **不设 error**（bootstrapper 会）；注入「成功」但段权限错误。 |
| 11 | shellui 多线程共用 `IPC_Client` 单例 | 无 mutex；hook/background/onpress 并发 send/recv → 串包、25s 超时。 |
| 12 | `util_booted` + rest delay | 冷启动首次 toolbox 被 rest 延迟；用户体感「卡死」。 |

### 中

| # | 位置 | 问题 |
|---|------|------|
| 13 | Fan IPC | 只改 `g_settings` 内存，不落盘；reload/重启回退旧阈值。 |
| 14 | `BREW_LAST_RET` | `last_ipc_error` 局部恒 `false`，永远「成功」。 |
| 15 | `BREW_UTIL_TEST_CONNECTION` | 枚举/测试有，**util 无 case** → health check 恒失败。 |
| 16 | sticky ready | `util_booted`/`fps_overlay` 同 boot 再加载 HEN 不清理 → 误触发 patch_checker / FPS 注入。 |
| 17 | util 看门狗 | 重启 util 失败 5 次后 **永久不再试**（直到 daemon 重启）。 |
| 18 | `g_settings` / `is_handler_enabled` | 跨线程非原子读写。 |
| 19 | 双路 toolbox 激活 | util rest 路径 `EnableToolbox` IPC + daemon 自注入，可能双 ptrace ShellUI（arch 已警告此类问题）。 |
| 20 | Cheat 多 patch | 中途失败不回滚已写补丁 → 半启用状态。 |
| 21 | `KILL_DAEMON` | `exit` 在 `reply` 前 → 客户端挂到超时。 |
| 22 | `ini.h` | `sprintf` 拼 key、`strncpy` 可不 NUL；畸形 config 可污染 settings。 |
| 23 | `common_utils.c` IP 错误路径 | `memcpy(..., "IP NOT FOUND", sizeof(ip_address))` 读越界短字面量。 |
| 24 | ParseCheatID | `sscanf %[^_]` 无宽度，tid 缓冲可能溢出。 |

### 低 / 气味

- `OnionHEN_log("size %lu", size_buf)` 打的是指针不是字符串。
- REMOUNT 路径校验逻辑怪异（短路径且不含 `/user` 才拒），不是可靠 allowlist。
- 大量 UNUSED 命令仍进 default/失败回复——兼容有意，但无 capability 协商。

---

## 4. 文档 vs 代码偏差

| `docs/arch.md` 说法 | 实际 |
|---------------------|------|
| `util_booted` 仅 rest/toolbox 延迟 | 冷启动首次 inject 也吃 delay |
| Toolbox readiness marker | 主路径是 `/system_tmp/onionhen/ready/toolbox` |
| 仓库布局列不全 | 缺少整组 `libonion_*` |
| 「IPC 协议稳定」 | 线格式稳定；转义/组帧/空 path **不稳** |
| util 可崩溃恢复 | 有 9021 重启，但 5 次后静默放弃 |
| Runtime atomics 描述过时 | settings/fan/`is_handler_enabled` 与 rest-mode 标志并存 |

---

## 5. 优先整改建议（按杠杆）

1. **立刻修明确逻辑错误**
   - FPS：先 `get_game_pid` / 解析 pid，再 `SuspendApp(pid)` / `ResumeApp(pid)`
   - Jailbreak：`!isProcessAlive` + null check 后再 `jailbreak`
   - Code cave：`MAP_FIXED` 或按返回 VA 写
   - elfldr：`sizeof(Elf64_Shdr)`；mprotect 失败 fail-closed

2. **IPC 硬化** — **已落地（2026-07-10）**
   - 全帧 `recv_full`/`send_full` + `ipc_message_force_nul`
   - `ipc_json_escape` + compact escaped `ipc_format_reply_body`
   - daemon path 全 null-check；`BREW_UTIL_TEST_CONNECTION`；`BREW_LAST_RET` 记 process last error
   - `IPC_Client` 实例 mutex + full-frame 收发

3. **Ready / Settings 语义收口** — **已落地（2026-07-16）**
   - rest delay：daemon 冷启动不再因 `util_booted` sleep（`onion/toolbox_timing.h`）
   - Settings store / fan 落盘 / `LoadSettings` 契约 — 见 §2.2
   - bootstrapper sticky flag clear；Toolbox ready 改为 PID 绑定并持久化

4. **加深模块（架构债）** — **已落地（2026-07-10）**
   - 单一 `libonion_elfldr`；shellui `onion/platform`；daemon pid → `onion_proc`

5. **补测** — **已落地（2026-07-16）**
   - `test_ipc_harden` / `test_toolbox_timing` / `test_hijack_retry` / `test_toolbox_injection`

---

## 6. 结论

架构骨架（双 daemon、共享 `libonion_*`、util 承载重 IO、ready 协议）是清晰且在向深模块演进的。

当前仍优先关注：

1. **进程标识混用（appid vs pid）**（FPS suspend 路径）
2. **Jailbreak retry / null spawn** — 策略已修，实机仍需回归
3. **Toolbox PID 生命周期实机回归** — 覆盖 daemon 重启、重复 HEN 与休息模式后的 ShellUI 换 PID
4. **Toolbox 多次失败后的恢复策略** — 当前不会主动 kill ShellUI，仍可评估有限重试

建议先完成 #1–#3 的实机回归，再依据现场失败率决定是否增加注入重试。

---

## 相关文档

| 文档 | 说明 |
|------|------|
| [arch.md](arch.md) | 总体架构 |
| [util_arch/README.md](util_arch/README.md) | util 守护进程架构 |
| [shellui-injection.md](shellui-injection.md) | ShellUI 注入路径 |
| [../source/README.md](../source/README.md) | 源码树与构建 |
