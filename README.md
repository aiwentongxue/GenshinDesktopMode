# Genshin Desktop Mode

在 Apple Silicon Mac 上运行《原神》iOS 客户端时，为 PlayCover 提供桌面键鼠与手柄适配。国服和国际服使用同一个插件，插件会在加载时核对客户端的运行时结构。

Genshin Desktop Mode is a keyboard, mouse, and controller compatibility plugin for the iOS version of Genshin Impact running through PlayCover on Apple Silicon Macs.

## R45

- 支持桌面控制界面、键鼠与手柄切换，以及按设备显示手柄按键提示。
- 适配地图、图鉴、调时间、队伍重排和聊天输入。大世界多项 F 交互使用分格滚轮切换。
- 当前源码含双缓冲试用改动。它为识别到的 Unity 游戏窗口减少一层绘图缓冲。实际延迟和帧率取决于显示器、场景和游戏设置。
- 兼容性按运行时结构判断，当前入口包含已核对过的 7.2 国服和国际服。客户端更新后需要重新核对。

## 构建和使用

需要 Apple Silicon Mac、Xcode 和支持 Mac Catalyst 的 macOS SDK。

```sh
zsh build_genshin.sh
zsh test.sh
```

构建生成 `.build/GenshinDesktopMode_R45.dylib`，并校验签名。退出游戏后，把这个文件导入已有的 PlayCover 插件加载入口。每个客户端只加载一个版本的桌面模式插件。

默认构建不需要游戏文件。依赖原版动作资源的震动播放和对应的震动设置桥接在默认构建中停用。如果你已有与本版本 SHA-256 匹配、并且有权在本机使用的资源包，可指定其本地路径后构建。

```sh
GDM_MOTION_PACK=/path/to/genshin-native-motion.pck zsh build_genshin.sh
```

`test.sh` 运行不依赖游戏安装的测试。`tests/` 也保留了需要本地客户端或 macOS 界面环境的核对程序。

## 许可证

本项目的源码采用 [MIT License](LICENSE)。

## 致谢

DualSense 蓝牙触觉协议实现参考了 Sdore 的 [SAxense](https://github.com/egormanga/SAxense) 研究。感谢 PlayCover 项目提供运行环境。

本项目是独立兼容性插件。游戏、图标商标和原版资源的权利归各自权利人。仓库提供插件源码和从源码构建的方式。
