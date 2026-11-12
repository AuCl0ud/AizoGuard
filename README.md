# 卫戍协议 AIZO 播放器

原生 C++ / Win32 桌面程序，面向明日方舟 Windows 电脑客户端。直接打开 `dist/AizoGuard.exe`。音频由 Windows Media Foundation 解码、waveOut 循环输出，支持本机可解码的 MP3 / WAV。歌曲需自行提供。

## 使用

1. 打开明日方舟电脑客户端，在播放器选择对应游戏窗口。
2. 选择本地 AIZO 音频，点击“试听 / 停止”确认能播放。
3. 在联防进行中点击“采集联防开始标志”。程序会切换到游戏并截图；在弹出的静态截图上拖框，选择**联防特有、持续可见**的文字或图标。松开保存，Esc 取消。不要使用计时器、动态角色、其他模式共用图标或尚未开始时的按钮。
4. 在联防结算时采集结束标志，选择稳定的结算文字或图标。两次采集须保持相同游戏窗口宽高比。
5. 点击“开始监听”，切回游戏。每 150ms 识别一次，连续 3 次确认开始后循环播放，确认结束后停止，自动等待下一局。
6. 点击“停止监听和音乐”或关闭播放器可立即停止。游戏窗口关闭也会停止。
7. 窗口底部“音乐音量”滑块支持 0～100%，0% 为静音；试听或联防播放时调整会立即生效，设置自动保存，下次启动恢复。

模板和配置保存在 EXE 同目录，请放在有写权限的文件夹。已按用户提供的 2560×1600 截图配置“联防阶段”和“作战结束”文字模板。默认对应 16:10 游戏客户区，其他比例需要重新采集。静态截图回归已验证，动画期间的实际识别仍需实机确认。

## 识图实现与边界

- 绑定游戏窗口 HWND，以 ScreenDC 获取客户区画面，归一化到宽 1280 像素，再在模板原位置附近 ±12 像素范围匹配。
- 独立实现灰度零均值归一化相关系数（ZNCC），与 OpenCV `TM_CCOEFF_NORMED` 的核心数学思路一致；为控制计算量对大模板采样，不声称与 OpenCV 数值逐点一致。拒绝无细节模板。
- 每个状态只在连续三帧命中后切换，同时命中开始和结束时不会启动播放。
- 当前只在游戏窗口为前台且客户区完全位于桌面范围内时识别。支持窗口在不同显示器之间移动；不支持后台、最小化识别或被其他顶置窗口遮挡。独占全屏若截图异常，请使用窗口化或无边框。
- 切走游戏时清空连续命中计数，暂停识别，已经开始的音乐继续。若切走时游戏结束，返回后需能看到结束标志才会停止；也可手动停止。
- 保持窗口比例不变时会统一尺度；改变比例后需重新采集。字体抗锯齿、渲染尺度变化仍可能降低识别分数。
- 系统播放器循环有可能出现短暂间隙，不提供无缝循环保证。
- 没有操作游戏、点击、按键注入、读取游戏内存或上传截图。

参考 MAA 的区域模板识别和 Win32 截图架构。原汁原味的 vibe coding 产物，你值得拥有（）

参考源码（阅读分支 dev-v2）：
- https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/dev-v2/src/MaaCore/Vision/Matcher.cpp
- https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/dev-v2/src/MaaCore/Vision/Config/MatcherConfig.cpp
- https://github.com/MaaAssistantArknights/MaaAssistantArknights/blob/dev-v2/src/MaaCore/Controller/Win32Controller.cpp

## 编译与测试

Windows x64，MinGW-w64 g++（支持 C++17）运行 `powershell -ExecutionPolicy Bypass -File build.ps1`。脚本仅供开发者编译；用户直接运行产物 EXE。另附 CMake 工程。

`AizoGuard.exe --self-test` 检查连续帧状态转换、重复联防、冲突信号、模板正负样本、比例变化和纯色模板（退出码 0 表示通过）。`--smoke-test` 创建真实界面后自动退出。实际游戏识别和真实 AIZO 音频需要在本机使用实际素材验证。

音频首次打开会解码并缓存到内存（本次 AIZO 约 36 MiB），后续触发直接播放。解码上限 256 MiB。配置文件使用 Unicode，保留中文音频路径。

