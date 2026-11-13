# RDR2 修改器安装器

给《荒野大镖客 2》单机模式做的一键安装器。Windows 和 SteamOS/Linux 各一个单文件可执行程序，
里面打包了 ScriptHookRDR2 + ASI 加载器 + Rampage Trainer，双击（或 Konsole 里跑一下）就装好，
不用手动往游戏目录里拖文件、不用自己改 Steam 启动项。

主要目标是 Steam Deck / SteamOS —— 那边没有 Wine 环境、Steam 配置在别处、游戏可能在 SD 卡上，
照着 Windows 教程手动装很容易漏掉 `WINEDLLOVERRIDES` 那一行然后白折腾半天。

## 两个产物

| 文件 | 平台 | 用法 |
|---|---|---|
| `dist/RDR2修改器.exe` | Windows x64 | 双击，出菜单 |
| `dist/rdr2mod-deck` | SteamOS / Linux x64（静态，musl） | `chmod +x` 后直接跑 |

两个是同一份 C 源码编出来的，行为一致。

## 它会做什么

1. 扫所有 Steam 库——内置盘、SD 卡、外接硬盘，从每个库根的 `steamapps/libraryfolders.vdf` 往外扩
2. 确认目录里有 `RDR2.exe`，并试写一个临时文件确认有权限
3. 备份：Proton 前缀里的存档（`compatdata/1174180/pfx/.../Rockstar Games/RDR2`）+ 即将被覆盖的原文件
4. 释放 20 个文件到游戏目录
5. 给 appid `1174180` 写启动项 `WINEDLLOVERRIDES="dinput8,ScriptHookRDR2=n,b" %command%`
   （改 `localconfig.vdf` 前先备份，CRLF 换行和其余键原样保留；没有该 appid 块会新建一个）
6. 逐字节校验装进去的文件
7. 全程写日志到 `~/RDR2-mod-install.log`

另外有：状态查看、临时禁用/启用（改 `dinput8.dll` 名字）、卸载并还原启动项、诊断模式、
手动指定游戏目录、`--no-vdf`（只放文件不动 Steam 配置）。

已经装过一遍再装不会出问题：自己上次装的文件不会被误当成"原版"备份，重复执行后
`localconfig.vdf` 一个字节都不变。

## 命令行

```
rdr2mod-deck                     # 交互菜单
--install [--root DIR]           # 直接装
--uninstall                      # 卸掉并还原启动项
--status                         # 看装没装、启动项现在是什么
--toggle                         # 临时关掉/打开修改器
--diagnose                       # 出问题先跑这个
--no-vdf                         # 只放文件，不碰 Steam 配置
--nativetrainer                  # 额外装官方 NativeTrainer.asi
--yes                            # 不询问（脚本里用）
--home DIR --steam-root DIR      # 测试用：覆盖家目录和 Steam 根
```

## 构建

需要 [zig](https://ziglang.org/) 当交叉编译器（`zig cc` 同时能出 Windows PE 和 Linux musl ELF，
不用装两套工具链）：

```bash
python fetch_assets.py     # 下 ScriptHookRDR2 和 Rampage 到 assets/（不入库）
bash build.sh              # 编两个平台，把负载追加到可执行文件尾部
```

`build.sh` 里改 `ZIG=` 那一行指向你的 `zig` 可执行文件。产物在 `build/`。

体积分布：代码约 270KB / 1.6MB（Windows / Linux），其余 13MB 全是追加在文件尾部的修改器负载。
格式是自定义容器：`u32 数量` + 每条 `u16 路径长 + 路径 + u64 长度 + 数据`，
文件末尾 24 字节 trailer 记录负载偏移和长度，`RDR2PKG1` 做魔数。

## 测试

```bash
python make_sandbox.py     # 造一个假 Steam 目录树（游戏放第二库里，模拟 SD 卡）
python test.py             # 30 项检查
python verify_pkg.py build/rdr2mod-win.exe build/rdr2mod-linux
```

`test.py` 覆盖：第二库发现、文件逐字节比对、启动项写入 / 重复安装幂等 / 卸载还原、
没有 appid 块时自动新建、CRLF 保留、`--no-vdf`、`--nativetrainer`、交互菜单。

Windows 版在这套沙盒上全过。Linux 版只做了编译和负载解析校验，没有在真机 Steam Deck 上跑过——
第一次用请先 `--diagnose`。

## 里面打包的第三方组件

| 组件 | 版本 | 来源 |
|---|---|---|
| ScriptHookRDR2.dll | 1.0.1491.17 | Alexander Blade，dev-c.com |
| dinput8.dll（ASI 加载器） | 同分发 | Alexander Blade，dev-c.com |
| NativeTrainer.asi | 同分发 | Alexander Blade，dev-c.com |
| Rampage.asi + RampageFiles | 1.6.8（游戏 1.0.1491.50） | github.com/rampage-trainer/rdr2 |

素材不入库，`fetch_assets.py` 按上面的地址下载。版权归各自作者。

## 注意

- **只玩故事模式。** 加载了 ScriptHookRDR2 进 Red Dead Online 会被直接踢，也别拿去线上用。
- 游戏更新后 ScriptHook 可能报 `can't find native`，等 ScriptHookRDR2 更新、重跑一次安装器覆盖。
- 只在 Steam 版上测过路径逻辑；Rockstar 启动器版/Epic 版的目录结构没覆盖。

## 许可

安装器本身（`src/`、`*.py`、`build.sh`）随便用。打包的第三方组件跟各自的授权走。
