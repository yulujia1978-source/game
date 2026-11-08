# 0.3.0 兼容扩展说明

这是在 PS5 上加载的一个 payload，目标覆盖 PS5 原生游戏、PS4 向下兼容游戏和在 PS5 上运行的模拟器。不是 PS4 主机上的独立插件，也不能据此宣称“全部游戏支持”。

| 程序类型 | 识别规则 | 0.3.0 实机状态 |
|---|---|---|
| PS5 游戏及使用 PPSA 的自制应用 | `eboot.bin` + PPSA 编号 | 0.2.6 用户反馈正常；新版待复测 |
| PS4 游戏及使用 CUSA 的模拟器包 | `eboot.bin` + CUSA 编号 | 待验证 |
| RetroArch | 上述包编号，或 `retroarch` / `.elf` / `.bin` 进程名 | 待验证 |
| PS1 / PS2 模拟器及封装游戏 | 上述包编号，或 `duckstation`、`pcsx2`、`pcsx-rearmed` 进程名 | 待验证 |
| PSP | 上述包编号，或 `ppsspp` 进程名 | 待验证 |
| FBNeo | 上述包编号，或 `fbneo` 进程名 | 待验证 |
| NS | 上述包编号，或 `switchps5`、`suyu`、`ryujinx`、`yuzu` 进程名 | 待验证 |
| 其他编号的自制应用 | 真实 Title ID 加入 `targets.txt` | 待验证 |

进程名识别不区分大小写，接受 `.elf` 和 `.bin` 后缀。识别到程序后仍须成功解析 libScePad 并找到可处理的导入调用；名称相同不能证明某个发行版已适配。不识别游戏 ROM 的文件名，模拟器内部换 ROM 不会自动生成新的应用配置。

## 非标准编号的模拟器

若启动后网页一直等待、日志出现 `unrecognized app XXXX12345`，可在 `/data/ps5-turbo/targets.txt` 添加日志中的**真实应用编号**，每行一个，保存后约一秒内重新读取。示例仅表示格式，不能直接代替主机上的编号：

```text
# Replace with the emulator's actual Title ID from log.txt
BREW12345
```

保留 4 个大写字母加 5 个数字的格式，最多 32 项；系统应用 NPX*、Shell 和 payload 服务不会因为加入清单就被注入。编号不合法时清单整体不生效。多个候选同时运行时暂停选择，应关闭其他候选程序。

## 已实现的输入路径

- `scePadReadState` / `scePadReadStateExt`：单份标准采样。
- `scePadRead` / `scePadReadExt`：标准 120 字节采样数组，按返回数、调用者容量和最多 64 份取最小值处理。
- PS4 的 connected 按单字节处理，其余保留字节不用于判断连接。
- 原有八键选取、快捷键选择、换游戏后继续运行网页等功能保留。

接口签名与 PS4 标准采样布局核对来源：[shadPS4 pad.h](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/pad/pad.h)、[ps4sdk pad.h](https://github.com/ps4dev/ps4sdk/blob/master/include/sce/pad.h)。这些来源可以核对软件接口，不能替代 PS5 实机测试。

不覆盖直接 HID 读取、私有读取接口、自定义采样布局、未被挂接模块的调用，也未加入系统全局手柄驱动修改。延迟加载的模块可能不生效。系统版本、kstuff 桥对 PS4 兼容进程的远程调用和具体模拟器版本仍需实机验证。

## 本地验证

通过自动选择 PS5/PS4/模拟器、自定义编号、排除系统和自身、多候选不注入、PS5→PS4 切换及无编号模拟器切换测试。通过 Ext 数组边界和错误返回测试、PS4 connected 填充字节测试、原有快捷键/HTTP/持久化/服务退出测试、ASan/UBSan 与交叉编译/注入代码独立性检查。

实机测试建议依次验证一个 PS5 游戏、一个 PS4 游戏、RetroArch、PS1/PS2、PSP、FBNeo、NS；每次保留日志中的应用编号、`attached`、`reads`、`changed` 及错误。需要同时验证单键、松手、切换组合、退出重进和更换游戏。出现只挂接成功但 `reads=0`，说明没有覆盖实际读取路径，不能视为兼容成功。

## 0.4.0 震动提示边界

仅手柄快捷键切换产生提示，固定 20 次/秒。依赖目标进程的 `scePthreadCreate`、`scePthreadDetach`、`sceKernelUsleep` 与 `scePadSetVibration`；缺失时输入连发仍可用。震动结束由短暂工作线程负责，不依赖后续游戏读取。模式未知时不强制覆盖原生触觉模式，因此部分原生 PS5 游戏可能没有提示震感。

线程签名核对来源：[PS4 SDK pthread 实现](https://github.com/CTurt/PS4-SDK/blob/master/libPS4/source/pthread.c)。DualSense 传统震动与模式 2 的适配参考：[PS5 Xash3D 的实机记录](https://github.com/mpereiraesaa/ps5-xash3d-halflife/blob/main/docs/DUALSENSE_CONTROLS.md#dualsense-rumble-hardware-accepted-2026-09-09)。这些参考不等于本插件已在主机验证。

## 0.4.1 升级方式

导入同一插件条目时选“替换”，启动新版即可请求旧版正常退出并接替。旧 0.2/0.3/0.4.0 的空锁文件可兼容；新版本记录带 PID 与启动时间，避免重启后残留记录或 PID 重用误判。不会强杀进程；超时不接替。网页首次从旧版切到 0.4.1 要手动刷新一次，之后才有自动刷新代码。

残留文件清理只在已确认新版本成功启动、且管理器元数据明确指向新版本时进行，限同一 `local/ps5-turbo` 目录的已验证旧 ELF。主机实际 Payload Manager 版本若使用其他目录/元数据格式，会跳过清理；导入时的“替换”由管理器自身处理。
