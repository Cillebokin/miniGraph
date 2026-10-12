# RendererCompositionSmokeTest

这是一个独立的 Qt Test/qmake 消费者工程，用于从 miniGraph 动态库的公共头文件和导入库验证多个模块分别贡献绘制内容。

`RendererModuleA` 和 `RendererModuleB` 位于各自的源文件中。测试覆盖按注册顺序绘制、后注册内容覆盖前一层、移除一个 Renderer 后另一个仍可绘制，以及两个 Renderer 各自调用 `RequestUpdate()` 后都能刷新画布。

## 构建环境

- Qt 5.12.10 `msvc2017_64`
- Windows x64 MSVC 开发者命令环境；本机以 VS2019 x64 工具链验证
- 已在相邻的 `miniGraph` 工程构建对应配置的 `MiniGraph.dll` 和 `MiniGraph.lib`

qmake 工程会根据当前 Debug/Release 配置自动链接对应的 `build_polyline_qt512_debug` 或 `build_polyline_qt512_release` 输出，并在链接后把匹配的 `MiniGraph.dll` 复制到测试程序目录。

## 使用

可以直接在 Qt Creator 中打开 `RendererCompositionSmokeTest.pro`，选择 Qt 5.12.10 `msvc2017_64` 套件，构建并运行 `RendererCompositionSmokeTest`。

也可以在 VS x64 开发者命令提示符中，从工程目录创建独立的 shadow build。先将 Qt 的 `msvc2017_64\bin` 加入 `PATH`，以便找到 qmake 和运行时 Qt DLL：

```bat
set "PATH=D:\MySoftwares\Tools\Qt\Qt5.12.10\5.12.10\msvc2017_64\bin;%PATH%"
mkdir build-debug
cd build-debug
qmake ..\RendererCompositionSmokeTest.pro "CONFIG+=debug"
nmake /f Makefile.Debug
set QT_QPA_PLATFORM=offscreen
RendererCompositionSmokeTest.exe
```

Release 配置使用 `build-release` 目录，qmake 参数改为 `"CONFIG+=release"`，并通过 `nmake /f Makefile.Release` 构建。不要把 Debug 和 Release 的 miniGraph `bin` 目录同时放在运行时 DLL 搜索路径中。
