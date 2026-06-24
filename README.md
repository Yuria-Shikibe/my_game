# my_game

Windows x64 上的 C++23 / Vulkan 游戏项目，使用 xmake 构建，主程序目标为 `game`。

## 前置环境

在开始构建前，确认 Windows 环境中已经安装并可用：

- Git
- xmake
- Visual Studio 2026 C++ toolchain，包含 MSVC `cl.exe`
- Vulkan SDK 1.4 或更新版本，并设置 `VULKAN_SDK`
- 支持 Vulkan 的显卡驱动
- Python 3.11 或更新版本，仅在重新生成 shader / icon 资源时需要
- Slang `slangc`，仅在重新生成 shader 资源时需要


## 从零获取源码

```powershell
git clone --recursive https://github.com/Yuria-Shikibe/my_game.git
cd my_game
```

如果克隆时没有使用 `--recursive`，进入仓库后执行：

```powershell
git submodule update --init --recursive
```

## 环境检查

xrgui 提供了环境检查任务，可先确认 MSVC、Vulkan SDK、子模块和已生成资源是否齐全：

```powershell
xmake xrgui.doctor
```

## Debug 构建

默认开发构建使用 debug 模式。干净构建后需要先构建 `xrgui.default`，再构建主程序目标。

```powershell
xmake xrgui.switch_mode debug
xmake -b xrgui.default
xmake -b game
```

构建产物位于：

```text
build\windows\x64\debug\game.exe
```

## 运行

从仓库根目录运行，保证运行时资源路径能正确解析到 `assets/shader/spv`：

```powershell
xmake run game
```

也可以直接运行 debug 产物，但当前工作目录仍应保持在仓库根目录：

```powershell
.\build\windows\x64\debug\game.exe
```

## Release 构建

```powershell
xmake xrgui.switch_mode release
xmake -b xrgui.default
xmake -b game
```

构建产物位于：

```text
build\windows\x64\release\game.exe
```

## 测试

```powershell
xmake xrgui.switch_mode debug
xmake -b xrgui.default
xmake -b game_tests
xmake run game_tests
```

## 重新生成 shader

仓库已提交运行所需的 SPIR-V 文件，普通构建不需要重新生成。修改 Slang shader 后，使用下面的命令重新生成。

生成 xrgui 的 GUI shader：

```powershell
xmake xrgui.gen_slang
```

生成本项目的 game shader：

```powershell
xmake xrgui.gen_slang -c slangc -f properties/assets_raw/shaders/config.toml -o properties/assets/shader/spv
```

然后重新构建：

```powershell
xmake -b xrgui.default
xmake -b game
```

## 常用命令速查

```powershell
git submodule update --init --recursive
xmake xrgui.doctor
xmake xrgui.switch_mode debug
xmake -b xrgui.default
xmake -b game
xmake run game
```
