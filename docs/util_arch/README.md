# util 守护进程架构

`util.elf` 是 OnionHEN 的 **Utility 守护进程**：与 `daemon.elf`（critical）分离，承载网络 IO、PKG 安装、金手指、Toolbox 相关修补等较重业务。

| 项 | 值 |
|----|-----|
| 产物 | `build/bin/util.elf` |
| 源码根 | `source/util/` |
| Unix IPC | `/system_tmp/onionhen/ipc/util_service` |
| IPC 命令前缀 | `0x8000000`（`BREW_UTIL_*`） |
| 日志 | `OnionHEN_log` → stdout + klog + `/data/OnionHEN/OnionHEN_util_daemon.log` |
| 崩溃日志 | `/data/OnionHEN/OnionHEN_util_crash.log` |

全局架构见 [arch.md](../arch.md)。  
金手指 **C++ 设计**见 [cheats_cpp.md](cheats_cpp.md)；格式细节见 `source/util/source/cheats/README.md`。

---

## 1. 在整体系统中的位置

```text
┌─────────────┐     spawn/load      ┌──────────────┐
│ bootstrapper│ ──────────────────► │  daemon.elf  │  critical IPC
└─────────────┘                     │  (main)      │  /system_tmp/onionhen/ipc/crit_service
                                    └──────┬───────┘
                                           │ 也可拉起
                                           ▼
                                    ┌──────────────┐
                                    │  util.elf    │  util IPC + 网络服务
                                    └──────┬───────┘
           ┌───────────────────────────────┼───────────────────────────────┐
           ▼                               ▼                               ▼
    ShellUI / fps_elf                                          游戏进程
    (Unix socket 客户端)                               mdbg|kdirect 写内存
```

**典型客户端**：`shellui.elf`、`fps_elf` 通过 `IPC_Client(util_daemon=true)` 连 util socket。

---

## 2. 启动顺序（`main` 调用链）

入口：`source/util/source/main.cpp`。

```text
main()
 │
 ├─ 1. sceNetCtlInit / sceUserServiceInitialize
 ├─ 2. setjmp 故障恢复锚点
 ├─ 3. fault_handler_init(cleanup)          # faulthandler.c
 ├─ 4. payload_get_args() → kernel_base
 ├─ 5. set_proc_authid(self, DEBUG_AUTHID) # 提权本进程
 ├─ 6. 默认 global_conf
 ├─ 7. unlink 旧 util 日志
 ├─ 8. LoadSettings()                      # /data/OnionHEN/config.ini
 ├─ 9. start_ip_thread()                   # cpp_service：后台刷本机 IP
 ├─10. pthread_create(IPC_loop)            # msg.cpp：常驻 Unix 监听
 ├─11. IniliatizeHTTP()                    # http.c：curl 下载能力
 ├─12. (可选) patch_checker()              # 非首启时激活 Toolbox 相关
 │
 └─13. for(;;) 主循环 ─────────────────────────────┐
        │                                           │
        │  无网？                                   │
        │    → sleep / 探测 rest mode              │
        │    → 可选 patch_checker()（工具箱 reinject） │
        │                                           │
        └─ sleep → 回到循环 ───────────────────────┘
```

要点：

- **IPC 线程先于主循环启动**，且不随 rest/网络重启销毁。
- 金手指 **service 状态**在冷启动后 `ensureDir` 一次；主循环只做 rest/网络探测。

---

## 3. 模块地图与职责

```text
source/util/
├── source/
│   ├── main.cpp                 # 生命周期编排
│   ├── msg.cpp                  # Unix IPC 服务端 + handleIPC 分发
│   ├── common_utils.c           # OnionHEN_log / notify / ptrace attach / 通用工具
│   ├── faulthandler.c           # 信号与崩溃落盘
│   ├── http.c                   # curl 下载、zip 解压、cheats commit 检查
│   ├── cpp_service.cpp          # IP 线程
│   ├── util_platform.c          # 共享平台：固件/版本/模块/读文件
│   └── cheats/                  # 金手指领域（C++ 编排 + 解析 + C 适配）
│       ├── CheatService / Repository / Applier
│       ├── ICheatParser + Factory (json/shn/mc4)
│       ├── ShnExt adapter + C crypto/utils/flatten
│       └── （第三方实现已集中到仓库根目录 third_party/cheat_support）
├── include/
│   ├── common_utils.h / ipc.hpp / pt.h / sfo.hpp / ...
│   ├── util_platform.h
│   └── cheats/                  # 金手指公共/内部头
└── （keystone 已集中到仓库根目录 third_party/keystone）
```

| 模块 | 文件 | 依赖方向（被谁用） |
|------|------|-------------------|
| Lifecycle | `main.cpp` | 无（顶层） |
| IPC | `msg.cpp` | main 线程创建 |
| Logging / notify | `common_utils.c` | 几乎全部 |
| Platform | `util_platform.c` | cheats、可被其它业务复用 |
| HTTP | `http.c` | msg（下载 cheats/kstuff） |
| IP poll | `cpp_service.cpp` | main 启动 |
| Toolbox reinject | `util_toolbox.cpp` | rest/网络路径 |
| Cheats | `cheats/*` | msg IPC |

**依赖原则（目标态）**：

```text
main ──► msg / cheats(init) / ip_thread
msg  ──► CheatService / http / common_utils
CheatService ──► Repository / ParserFactory / Applier ──► util_platform + pt/mdbg/kernel
```

金手指 **不再**内嵌第二套进程/固件/读文件栈；平台能力集中在 `util_platform`。

---

## 4. 线程模型

| 线程 | 创建位置 | 生命周期 | 作用 |
|------|----------|----------|------|
| main | `main` | 进程 | 主循环、rest/网络探测 |
| IPC accept | `IPC_loop` | 常驻 | accept Unix 连接 |
| IPC client | `ipc_client`（每连接一个，detach） | 连接级 | 读 `IPCMessage` → `handleIPC` |
| IP poll | `start_ip_thread` | 常驻 | 刷新本机 IP 字符串 |

故障：`faulthandler` 触发 `cleanup` → cleanup → `exit`。

---

## 5. IPC 协议与命令分发

### 5.1 报文

定义见 `source/include/msg.hpp`：

```text
struct IPCMessage {
  int magic = 0xDEADBABE;
  DaemonCommands cmd;
  int error;
  char msg[0x1000];   // JSON 入参 / 文本出参
};
```

服务端：`networkListen(UTIL_IPC_SOC)` → accept → 校验 magic → `handleIPC(client, json, cmd)` → `reply(error, out_var)`。

### 5.2 util 命令表（`BREW_UTIL_*`）

| 命令 | 作用 | 内部去向 |
|------|------|----------|
| `SHELLUI_ON_STANDBY` | ShellUI 休息模式标记 | 原子标志 `real_rest_mode_detected` |
| `DAEMON_PID` | 返回 util pid | `getpid` |
| `GET_GAME_VER` | 游戏版本字符串 | param.json / param.sfo（msg 内实现） |
| `GET_GAME_CHEAT` | 导出金手指列表 JSON 文件路径 | `CheatService::exportList` |
| `TOGGLE_CHEAT` | 开关某条金手指 | `CheatService::toggle` |
| `DOWNLOAD_CHEATS` | 下仓库 zip → staging → flatten | `http` + `CheatService::flattenInstallTree` |
| `RELOAD_CHEATS` | **已移除**（枚举占位 `UNUSED_RELOAD_CHEATS`） | 列表/开关靠文件签名热重载，无索引重建 |
| `DOWNLOAD_KSTUFF` | 下载 kstuff.elf | `http` |
| `LAUNCH_PAYLOAD` | 加载 payload `.elf` | `load_payload` → `onion_payload_load`（仅私有 9020，必须返回精确 PID；失败不回退 9021） |
| `UNUSED_LEGACY_CMD_SERVER` | 已移除（原 TCP 9028） | 固定失败 |
| `LAUNCH_ELFLDR` | 已移除 | 固定失败 |
| `UNUSED_FTP` / `UNUSED_KLOG` | 已移除 | 固定失败 |
| `BREW_KILL_DAEMON` / `BREW_RELOAD_SETTINGS` | 杀进程 / 重载 ini | main 侧状态 |

### 5.3 金手指 IPC 时序（ShellUI → 游戏）

```text
ShellUI
  │ GET_GAME_VER(tid)
  ▼
util handleIPC ──► param.json / sfo ──► version 字符串
  │
  │ GET_GAME_CHEAT(tid, version)
  ▼
cheat_service_export_list
  │  resolve /data/OnionHEN/cheats/<TID>_<VER>.{json,shn,mc4,ShnExt}
  │  load + parse → 写 /user/data/OnionHEN/<tid>_cheats
  ▼
ShellUI 读列表 JSON，渲染开关
  │
  │ TOGGLE_CHEAT(tid, version, pid, cheat_id)
  ▼
cheat_service_toggle_index
  │  refresh 文件签名 → onion_toggle_cheat
  ▼
cheat_engine_runtime
  │  util_find_module → base
  │  remote_ops(fw)：mdbg | kdirect
  ▼
目标游戏进程内存
```

---

## 6. 各子系统内部逻辑

### 6.1 `common_utils` / 日志

- **`OnionHEN_log`**：格式化 → 追加 `\n` → `printf` + `klog_printf` + append 日志文件。
- **`notify`**：系统通知气泡。
- **`pt_attach_proc` / `pt_detach_proc`**：提权 authid 后 `ptrace`（code cave mmap 用）。
- 其它：HTTP 初始化封装声明、`download` 相关声明等。

全模块日志统一走 `OnionHEN_log`，金手指不再单独 logger。

### 6.2 `util_platform`（共享平台）

| API | 行为摘要 |
|-----|----------|
| `util_system_fw_major` | `sceKernelGetProsperoSystemSwVersion` → `version >> 16` |
| `util_file_read_alloc` | 整文件读入 heap（带 max size） |
| `util_resolve_game_version` | 扫 appmeta 的 param.json / param.sfo |
| `util_get_running_bigapp` | BigApp appid + 进程表匹配 + 版本解析 |
| `util_find_module` | dynlib 列表匹配（filename/libname/basename）→ 失败则 eboot 映像基址 fallback |
| `util_find_module_in_app` | 按 appid 扫进程再 `util_find_module` |

**与 hijacker 的固件号差异**：hijacker `getSystemSwVersion()` 用 `kern.sdk_version` 做偏移表；金手指门控用 Prospero major。二者用途不同，不可混用。

### 6.3 GitHub 响应解析（`http_github.cpp`）

- `onion_http_extract_commit_sha`：从 GitHub commit JSON 中提取 SHA。
- 网络下载、curl/TLS 初始化和 minizip 解压链已移除；本模块只负责解析调用方提供的 JSON。

解析逻辑使用树内 cJSON。


### 6.5 IP / Toolbox reinject

- **`start_ip_thread`**（`cpp_service.cpp`）：维护全局 IP 字符串，供 notify 文案使用。
- **`patch_checker` / `enable_toolbox`**（`util_toolbox.cpp`）：与 Toolbox 注入、ShellCore 相关修补。

### 6.6 金手指（C++：`onion::cheats`）

编排与格式解析均为 C++（Facade / Strategy / Factory）；ShnExt crypto 经 Adapter 调 C。详见 [cheats_cpp.md](cheats_cpp.md)。

```text
┌──────────────────────────────────────────────┐
│ CheatService   Facade + mutex + 热重载状态    │
└───────────────────┬──────────────────────────┘
        ┌───────────┼──────────────┐
        ▼           ▼              ▼
 CheatRepository  CheatApplier   Flatten
 路径/签名/load    toggle 补丁    仓库安装
        │           │
        ▼           ▼
 CheatParserFactory  IMemoryBackend
  json/shn/mc4/C++   Mdbg | Kdirect
  ShnExt → C adapter (Factory by FW)
        │           │
        └─────┬─────┘
              ▼
       util_platform + pt/mdbg/kernel
```

#### 路径与格式

```text
/data/OnionHEN/cheats/<TITLE_ID>_<VERSION>.{json,shn,mc4,ShnExt}
```

解析优先级：`json` → `shn` → `mc4` → `ShnExt`。  
**不支持** KCF / WMDW。

#### 热重载

`cheat_service` 缓存：path + size + inode + mtime + ctime。变化则 `disable` 已启用项并重新 `onion_load_cheat_file`。

#### Toggle 写内存顺序（`cheat_engine_runtime`）

```text
1. util_system_fw_major → 选 backend（<0x840 mdbg，否则 kdirect）
2. util_find_module(module_name) → 失败则 util_find_module_in_app
3. 检测 PS2 模拟模块；base = sections[0].vaddr
4. Master Code / MC 依赖偏移修正（可选）
5. 对每个 patch：
     addr = absolute|ps2 ? offset : base+offset
     write on/off → readback verify
     失败则 code_cave：pt_attach → pt_mmap 页 → mprotect → 再写
6. 更新 entry.enabled / last_applied_pid
```

#### 下载 flatten

在线 `DOWNLOAD_CHEATS` 命令已保留为 unsupported 兼容槽位。`onion_cheat_flatten_install_tree` 仍用于把本地导入树规范成 flat 命名并装入 `ONION_CHEATS_DIR`。

---

## 7. 与外部库 / 二进制依赖

| 依赖 | 用途 |
|------|------|
| libhijacker | 内核原语、偏移（shellcore / 注入路径） |
| libonion_elfldr | ptrace / mmap 注入原语；内置 9020 loader 复用其 spawn/read 实现 |
| keystone | ShnExt 汇编（`third_party/keystone/`）；C++ runtime 由 PS5 SDK 提供 |
| cJSON | IPC 与 GitHub 响应 JSON 解析 |
| AES/base64 third_party | MC4 / ShnExt 解密 |
| miniz / sha256 | ShnExt 解压与密钥派生 |

---

## 8. 配置与运行时数据

| 路径 | 用途 |
|------|------|
| `/data/OnionHEN/config.ini` | toolbox、rest mode 等 |
| `/data/OnionHEN/cheats/` | 金手指 flat 文件 |
| `/data/OnionHEN/cheats_staging/` | 下载解压临时区 |
| `/user/data/OnionHEN/<tid>_cheats` | ShellUI 消费的列表 JSON |
| `/data/OnionHEN/OnionHEN_util_daemon.log` | 运行日志 |
| `/data/OnionHEN/kstuff.elf` | 下载的 kstuff |
| `ONION_FLAG_UTIL_BOOTED` | util 是否已完成过冷启动（typed ready flag） |

`LoadSettings` 读取的主要键（节选）：`Rest_Mode_Delay_Seconds`、`Util_rest_kill`、`APP_JB_Debug_Msg`。

---

## 9. 源码导航速查

| 想改… | 先看 |
|--------|------|
| 守护进程启停 / 主循环 | `source/util/source/main.cpp` |
| 新 IPC 命令 | `source/include/msg.hpp` + `source/util/source/msg.cpp` |
| 金手指列表/开关 | `cheats/cheat_service.cpp` → `cheat_applier.cpp` |
| 新金手指格式 | `ICheatParser` + `CheatParserFactory::createByFormat` |
| 写内存策略 | `i_memory_backend.hpp` / `memory_backends.cpp` |
| 版本/模块/固件 | `util_platform.c` |
| IP 线程 | `cpp_service.cpp` |
| 下载/zip | `http.c` |
| 日志 | `common_utils.c` → `OnionHEN_log` |

---

## 10. 设计约束（实现时应遵守）

1. **业务进 util，不进 critical daemon**（稳定性边界）。
2. **平台能力放 `util_platform`**，避免功能目录再复制一份进程/固件逻辑。
3. **日志统一 `OnionHEN_log`**（klog 仅作底层 sink，不是第二套业务 logger）。
4. **金手指路径 flat、无 txt 索引、无扩展名子目录**。
5. **高固件写内存优先 kdirect**，避免无必要 `PT_ATTACH` 停进程；code cave 仍可短时 ptrace mmap。
6. **C++ 单元勿 include NineS `freebsd-helper`**（与 SDK 头冲突）；模块信息用 `util_module_info_t` ABI 对齐。
