# Genshin Desktop Mode

在 Apple Silicon Mac 上运行《原神》iOS 客户端时，为 PlayCover 提供桌面键鼠与手柄适配。国服和国际服使用同一个插件，插件会在加载时核对客户端的运行时结构。

Genshin Desktop Mode is a keyboard, mouse, and controller compatibility plugin for the iOS version of Genshin Impact running through PlayCover on Apple Silicon Macs.

## R46

- 手柄类型和按键绑定由游戏识别。Xbox Series X 和 DualSense 可使用原生布局查看、修改按键，切换角色、交互和确认、取消也使用该布局。部分 DualSense 连接会显示游戏分配的 Xbox 键名。
- 鼠标拖动地图时，金色光标与地标同步移动。鼠标单击或触控板轻点地图空白处可以新建标记。
- 切换账号时由账号弹窗独占鼠标点击，返回标题页后可看到云层、门、「点击进入」和两侧按钮。
- 游戏使用系统默认的绘图缓冲设置。手柄热切换期间在主线程查询游戏界面。
- 国服和国际服使用同一份插件，已核对两个 7.2 客户端的运行时结构。

[下载 R46](https://github.com/cnSniperMike/GenshinDesktopMode/releases/tag/R46)。需要完整手柄震动时，请选择 `GenshinDesktopMode_R46.dylib`。基础版文件名带有 `_Basic`。

## 构建和使用

需要 Apple Silicon Mac、Xcode 和支持 Mac Catalyst 的 macOS SDK。

```sh
zsh build_genshin.sh
zsh test.sh
```

默认构建生成 `.build/GenshinDesktopMode_R46_Basic.dylib`，并校验签名。退出游戏后，把这个文件导入已有的 PlayCover 插件加载入口。每个客户端只加载一个版本的桌面模式插件。

默认构建不需要游戏文件。依赖原版动作资源的震动播放和对应的震动设置桥接在默认构建中停用。如果你已有与本版本 SHA-256 匹配、并且有权在本机使用的资源包，可指定其本地路径后构建。

```sh
GDM_MOTION_PACK=/path/to/genshin-native-motion.pck zsh build_genshin.sh
```

指定资源包后，完整构建生成 `.build/GenshinDesktopMode_R46.dylib`。

`test.sh` 运行不依赖游戏安装的测试。`tests/` 也保留了需要本地客户端或 macOS 界面环境的核对程序。

## 许可证

本项目整体采用 [GNU Affero General Public License v3.0 only](LICENSE)（SPDX 标识为 `AGPL-3.0-only`）。

已按 MIT 许可发布的早期代码仍可按原许可使用。随本项目保留的原版权与许可声明见 [NOTICES](NOTICES)。

## 致谢

DualSense 蓝牙触觉协议实现参考了 Sdore 的 [SAxense](https://github.com/egormanga/SAxense) 研究。感谢 PlayCover 项目提供运行环境。

本项目是独立兼容性插件。游戏、图标商标和原版资源的权利归各自权利人。仓库提供插件源码和从源码构建的方式。
