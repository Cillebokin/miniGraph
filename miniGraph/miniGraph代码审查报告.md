# miniGraph 代码与功能审查报告

> 审查/更新日期：2026-10-09  
> 审查对象：`D:\MyWorks\ProQt\miniGraph\miniGraph`  画布动态库、测试和基础示例  
> 审查方式：源码静态检查；本报告不等同于安全渗透测试或大场景性能测试
>
> 本次更新：核验 Gemini 3.8 Flash 的部分技术判断，纳入已确认或带条件的补充项，并标明不受源码支持的结论；后续复核并修复 MG-004、MG-005，确认 MG-001、MG-006、MG-022 已由当前源码和测试覆盖，并同步修正文档记录。按用户要求，不评价其“CAD 交互体验与功能缺失（UX Gaps）”部分，也不据此要求改变当前交互效果。

## 1. 总体结论

miniGraph 已形成较完整的画布基础闭环：`CanvasDocument` 管理实体，`CanvasView` 提供视口和交互，默认 `QPainter` 渲染器负责内置图元，捕捉、选择、编辑控制柄和 `CanvasAction` 扩展机制也已接入。当前实现符合项目已确定的 QWidget/QPainter 路线。

目前已形成可用的画布基础能力，但性能适用规模尚未通过基准测试确定；因此不把它定性为“中小规模”或承诺具体承载上限。主要待处理项集中在：

1. 非有限数值、极大坐标和超深组树等边界输入仍有保护或测试待补充。
2. 每次重绘都全量绘制，命中和捕捉线性扫描；尚无大场景基准，不能承诺可扩展到的图元规模。
3. 持久化、完整图层管理、自定义绘制内容参与选择/捕捉等能力仍属于调用层或后续功能范围。

本报告中的“缺陷”指代码路径已能确认的问题；“风险”指在特定输入或规模下可能出现的问题；“功能缺口”不代表现有接口承诺了该功能。

## 2. 严重度定义

| 标记 | 含义 |
|---|---|
| P1 | 常规公共 API 使用可能造成状态未更新、结果错误或明显交互不一致，建议优先修复 |
| P2 | 边界输入、复杂场景或常见数据格式可能触发，建议补保护和回归测试 |
| P3 | 规模、维护性、产品完整度或接口约定方面的改进项，不代表当前必然出错 |

## 3. 确定缺陷与待明确的跨层语义

### MG-001 · P1 · `SetItemCanvasData()` 绑定冲突失败契约【已修复】

- **原报告描述已过时**：当前 `CanvasDocument::SetItemCanvasData()` 已调用返回状态的 `TrySetCanvasData()`；数据已绑定到其他图元时立即返回 `false`，不会发出 `Changed()`。
- **原子性**：实体层先检查新数据的绑定所有者，冲突时在解绑旧数据或更改实体状态之前返回失败。因此目标图元不获得该数据，原图元的绑定保持不变。
- **处理与接口契约**：当前实现只在 `TrySetCanvasData()` 成功后发出 `Changed()`；本次补充公共头文件注释，明确图元不存在或数据绑定冲突时返回 `false` 且不发信号。
- **验证**：`DocumentRejectsCanvasDataBindingConflictAtomically` 覆盖共享数据绑定到第二个图元时的失败返回、无 `Changed()`、原绑定保留和目标图元未绑定；`DocumentOwnsCanvasDataAndGlobalData` 覆盖成功路径。Qt 5.12.10 x64 Debug 动态库/测试程序构建成功，专项测试通过；全量 QtTest 为 179 passed、0 failed、0 skipped。
- **代码位置**：[CanvasDocument.h](include/MiniGraph/CanvasDocument.h#L140)、[CanvasDocument.cpp](src/CanvasDocument.cpp#L650)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L229)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L1834)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L1877)

### MG-002 · P2 · 多段线子段属性在显示、命中和捕捉中的处理不一致【已修复】

- **原现象**：加入多段线的线段/圆弧会被克隆，因此子实体自身的样式和 `CanvasData` 会保留；但默认绘制和选择路径不检查子段可见性，捕捉则会跳过不可见子段。
- **确认契约**：多段线父对象统一控制描边样式；子段 `visible` 独立控制该段的绘制、命中、框选和捕捉；多段线仍作为一个顶层图元参与选择。隐藏子段的 `CanvasData`、标签等属性仍保留，但其绘制内容随该段隐藏。
- **处理**：绘制器和 CanvasView 的共享命中/框选路径过滤隐藏子段；隐藏线段独占的内部顶点不再显示或响应编辑柄；捕捉路径原已过滤隐藏子段，补上了专门回归测试。
- **验证**：Qt 5.12.10 Debug/Release 全量构建成功；全量 QtTest 两种配置均为 173 passed、0 failed。专项覆盖 `CanvasRendererSkipsInvisiblePolylineSegments`、`CanvasViewSelectionSkipsInvisiblePolylineSegments`、`CanvasViewDoesNotDrawEditHandlesForHiddenPolylineVertices` 和 `CanvasSnapperSkipsInvisiblePolylineSegments`。
- **代码位置**：[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L957)、[CanvasView.cpp](src/CanvasView.cpp#L282)、[CanvasSnap.cpp](src/CanvasSnap.cpp#L36)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L3161)

### MG-003 · P2 · 圆弧几何 API 与实体层对整周角差的接受范围不一致【已修复】

- **原现象**：公开的 `IsValidGeometry(StruArcGeometry)` 接受起止角差为 `2π` 的数据；`CalculateDirectedSweep()` 会将该整周差处理为完整的 `2π` 扫掠。实体层 `CanvasArc` 则拒绝同一输入。
- **统一契约**：`CanvasArc` 表示非整周圆弧，完整圆周使用 `CanvasCircle` / `StruCircleGeometry`；起止角差对 `2π` 取余后，在 `1e-12` 弧度几何容差内等价于零的输入无效。
- **处理**：新增内部 `detail::IsValidArcSweep()`，由几何 API 与 `CanvasArc` 共用；两层一致拒绝零扫掠、整周/多整周等价角差、容差内近零扫掠及角差溢出输入。
- **验证**：`GeometryAndCanvasArcUseConsistentSweepTolerance` 覆盖零扫掠、正负整周/多整周、容差内外边界、极小有效扫掠和顺逆方向；`GeometryRejectsOverflowingArcAngleDifference` 验证几何 API 与实体层都拒绝角差溢出。Qt 5.12.10 Debug/Release 全量 QtTest 均为 174 passed、0 failed、0 skipped。
- **代码位置**：[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L497)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L53)、[CanvasGeometryPrivate.h](src/CanvasGeometryPrivate.h#L9)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L2555)

## 4. 数值、边界与数据格式风险

### MG-004 · P2 · `CanvasTransform` 未统一拒绝 NaN/无穷输入【已修复】

- **原报告描述有误**：当前源码中的 `SetZoom()`、`SetCenter()`、`PanBy()` 和 `ZoomAt()` 已有有限性/有效性检查；其中 `ZoomAt()` 在输入或中间结果无效时会保持原状态。不能再将这些路径描述为缺少 NaN/无穷保护。
- **实际缺口**：`MapToWorld()`、`MapFromWorld()` 未统一拒绝零、负数或非有限视口尺寸；`MapFromWorld()` 的差值或映射结果溢出时也可能返回错误坐标。`FitRect()` 将非有限 `marginPixels` 静默替换为零，且未验证拟合中间结果。
- **统一契约**：所有使用视口尺寸的映射、缩放及拟合入口要求宽、高均为有限正数；`MapToWorld()` 输入无效时返回无效 `CanvasVector`，`MapFromWorld()` 输入无效或计算结果非有限时返回默认 `QPointF()`。`FitRect()` 遇到非有限参数、无效视口或非有限中间结果时返回 `false` 且不修改变换状态；有限负边距继续按零处理。
- **处理**：提取有限点和有效视口尺寸检查；映射结果经过有限性校验；`ZoomAt()` 复用统一视口尺寸规则；`FitRect()` 先验证并计算候选中心/缩放，再一次性更新状态。
- **验证**：新增 `CanvasTransformRejectsInvalidViewportDimensions` 与 `FitRectRejectsNonFiniteInputsWithoutMutation`，分别覆盖零/负/非有限视口尺寸、映射差值溢出、NaN/正负无穷边距、拟合矩形溢出和失败时状态不变。Qt 5.12.10 x64 Debug 全量 QtTest 为 176 passed、0 failed、0 skipped。
- **代码位置**：[CanvasTransform.cpp](src/CanvasTransform.cpp#L64)、[CanvasTransform.cpp](src/CanvasTransform.cpp#L80)、[CanvasTransform.cpp](src/CanvasTransform.cpp#L114)、[CanvasTransform.cpp](src/CanvasTransform.cpp#L148)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L1469)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L1563)

### MG-005 · P2 · 网格刻度边界核查发现间距 setter 未拒绝非有限值【已修复】

- **原报告描述有误/已过时**：当前网格和标尺并未把世界坐标索引直接转换为 `int`。`CalculateVisibleTickRange()` 先验证坐标与间距有限、索引处于可安全表示的范围，并将每轴可见刻度限制在 10000 个以内；通过校验后才转换为 `qint64`，绘制循环也使用 `qint64`。因此，不能再把整数溢出描述为当前已确认问题。
- **实际缺口**：`CanvasView::SetGridSpacing()` 原先只检查间距是否小于等于零；NaN 会绕过该检查，正无穷也可能被保存并发出变更信号。后续网格/标尺计算会因非有限间距而跳过绘制，但对象属性仍会处于无效状态。
- **处理**：在 setter 入口拒绝 NaN 和正负无穷，同时保留原有的正数约束；无效输入不改变间距、不发出 `GridSpacingChanged`。既有刻度范围保护保持不变。
- **验证**：新增测试验证极大坐标下网格和标尺不绘制、超出 10000 个上限时跳过刻度绘制，以及 NaN/正负无穷网格间距被拒绝且状态/信号不变。已有 `CanvasViewGridAndRulerHandleLargeFiniteCoordinates` 继续覆盖超出 `int` 范围的有限坐标。Qt 5.12.10 x64 Debug 动态库和测试程序均重新构建；三项专项测试通过，全量 QtTest 为 179 passed、0 failed、0 skipped。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L78)、[CanvasView.cpp](src/CanvasView.cpp#L990)、[CanvasView.cpp](src/CanvasView.cpp#L1972)、[CanvasView.cpp](src/CanvasView.cpp#L2185)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L4897)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L4929)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L4971)

### MG-006 · P2 · 非有限距离可能被命中/捕捉比较逻辑接受【已修复】

- **原报告描述已过时**：`CanvasVector::SquaredDistanceTo()` 在坐标差或平方结果非有限时返回无效标量；`CanvasSnapper` 与 `CanvasView::HitTestItem()` 的候选入口都显式拒绝非有限平方距离，再进行容差和最优候选比较。
- **处理**：当前两处候选过滤保证 NaN/无穷值不能通过“首个候选”分支成为有效捕捉或命中；无需再增加重复的运行时保护。
- **验证**：`CanvasSnapperRejectsNonFiniteDistances` 使用极大相反坐标验证不会捕捉；`CanvasViewHitTestRejectsNonFiniteDistances` 验证对应点击不会选中实体。本次 Qt 5.12.10 x64 Debug 两项专项测试均通过。
- **代码位置**：[CanvasVector.cpp](src/CanvasVector.cpp#L196)、[CanvasSnap.cpp](src/CanvasSnap.cpp#L276)、[CanvasView.cpp](src/CanvasView.cpp#L2825)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L3117)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L5813)

### MG-007 · P3 · CanvasData 命令格式限制【契约已明确；当前限制接受】

- **现状**：每个 `QStringList` 项是一条以英文逗号分隔的命令；解析跳过空字段，没有引号或转义机制。文本、字体名称、图片路径等字段不能包含逗号，空参数不能表达。
- **契约决策**：当前 API 仅承诺表格所列命令语法，不承诺任意字符串或空字段无损传入；未知、字段不足或校验失败的命令会静默忽略，固定参数命令的多余字段会忽略。
- **处理**：新增 [CanvasData命令格式.md](CanvasData命令格式.md)，并在 `CanvasData`、`CanvasGlobalData` 公共头文件中指向该契约。暂不改解析行为；若未来需要逗号/空字段，应另行设计带兼容规则的转义或结构化格式。
- **代码位置**：[CanvasData命令格式.md](CanvasData命令格式.md)、[CanvasData.h](include/MiniGraph/CanvasData.h)、[CanvasGlobalData.h](include/MiniGraph/CanvasGlobalData.h)、[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L1041)

### MG-008 · P2 · 深层组容器带来递归和重复克隆风险【深度已修复；节点总量未设上限】

- **处理**：`CanvasGroupContainer::MaximumNestingDepth` 将组嵌套限制为 64 层（最外层组计为一层）。`AddChild()` 和 `ReplaceChild()` 在递归校验及深拷贝前，使用显式栈检查待加入子树；若操作会使结果超过上限，则拒绝且保持原组不变。由这些公共接口构造的组树，其递归校验、克隆、文档校验、绘制、捕捉和视图遍历均受该深度上限约束。
- **遍历说明**：`TryGetGeometryBounds()` 本身已使用显式待处理列表，不依赖组嵌套递归。
- **验证**：最大深度可成功校验、克隆并加入文档；会造成第 65 层的追加和替换均被拒绝且状态不变；256 个同层子图元仍可正常加入。
- **剩余边界**：没有设置组内子图元总量上限。超宽组仍可能带来较大的遍历和深拷贝成本；本次不引入任意总量限制，除非产品/API 契约需要为其定义明确上限。
- **代码位置**：[CanvasEntities.h](include/MiniGraph/CanvasEntities.h#L345)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L26)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L912)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L1314)

## 5. 绘制与交互性能

### MG-009 · P2 · 当前三层离屏图像会复用缓冲，但每次仍全量重绘

- **现象**：背景、几何、overlay 三张 `QImage` 是视图成员并会复用；仅在像素尺寸、格式或设备像素比变化时重新创建。每次绘制仍会清空三张图并重绘网格、全部实体和叠加内容，最后合成到控件；脏矩形没有用于缩小图层绘制范围。
- **影响**：鼠标移动、捕捉指示或小区域状态变化也可能触发全画布处理。DPR=2、1920×1080 逻辑尺寸时，三张 RGBA 图层的像素缓冲估算约占 95 MiB；这是保留缓冲的估算值，不是实测进程内存，也不意味着每帧重新分配这些内存。
- **建议**：已建立本机 Release 基线；后续再按目标硬件和调用场景决定是否做局部重绘、背景/内容缓存或减少离屏层。缓存必须有清楚的失效条件，避免引入与性能不成比例的复杂度。
- **本机基线**：1280×720、DPR=1、5,000 个混合实体时，完整帧中位数为 7.173 ms、P95 为 10.001 ms；三张 QImage 图层的像素缓冲估算为 11,059,200 字节。详见 [MG-009～MG-011 性能基线](MG-009至MG-011性能基线.md)。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L541)、[CanvasView.cpp](src/CanvasView.cpp#L1618)

### MG-010 · P2 · 绘制、命中、框选和捕捉缺少空间索引

- **现象**：默认渲染器遍历全部实体；命中和捕捉也分别遍历文档 ID，框选同样需要扫描实体。捕捉会随鼠标移动重复执行。
- **影响**：主要操作成本随实体数量线性增加；样条、圆弧、椭圆还需要生成离散路径。当前已有一组本机不同实体数和曲线比例的基准，但它不能直接代表其他硬件或真实桌面合成环境。
- **建议**：以本机基线作为后续对比起点；在明确目标硬件和交互响应要求后，再决定是否需要空间索引或可见区域裁剪，不据单机数据贸然承诺容量或帧率。
- **本机基线**：5,000 个曲线占比 67% 的实体下，点击命中中位数 5.430 ms、点击未命中 5.524 ms、窗口框选 5.180 ms、几何捕捉未命中 0.403 ms。完整分组数据见 [性能基线报告](MG-009至MG-011性能基线.md)。
- **代码位置**：[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L1070)、[CanvasSnap.cpp](src/CanvasSnap.cpp#L303)、[CanvasView.cpp](src/CanvasView.cpp#L2707)

### MG-011 · P2 · CanvasData 指令和图片在绘制期间重复生成/读取

- **现象**：渲染时调用 `CalculateToQStringList()` 并重新解析命令；遇到 `image` 指令时，每次绘制都会创建 `QImageReader` 并读取图片。
- **影响**：包含动态指令或图片的场景会把字符串生成、拆分和图像解码成本叠加到每次全量重绘上。
- **建议**：基线显示图像命令成本随数量明显增加；如后续需要缓存解析结果或图片，仍需先明确 `CanvasData` 变更通知和缓存失效时机，并限制图片资源的尺寸/读取成本。
- **本机基线**：50 条动态直线命令的完整帧中位数为 0.878 ms；8 张 256×256 PNG 图片命令为 5.545 ms（P95 6.180 ms，暖文件缓存）。该值包含画布帧开销，不是孤立的解码耗时。详见 [性能基线报告](MG-009至MG-011性能基线.md)。
- **代码位置**：[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L835)、[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L870)

## 6. 扩展能力和产品边界

### MG-012 · P3 · 自定义 Renderer 的绘制内容不进入实体交互链路

- **现状**：自定义 Renderer 可在背景、实体绘制后或 overlay 阶段画内容，但这些内容不是 `CanvasDocument` 实体，默认不参与选择、命中、捕捉、编辑控制柄和实体包围范围计算。
- **影响**：适合装饰、辅助线和预览；如果调用方希望自定义内容可选、可捕捉或可持久化，需要自行实现对应模型和交互。
- **建议**：在公共 API 文档和示例中明确此边界；只有在确有用户需求时，再设计 renderer 与交互代理的接入点。

### MG-013 · P3 · Action 示例尚未覆盖全部实体创建流程

- **现状**：基础示例展示点和直线创建 Action；圆弧、圆、椭圆、多段线、样条和组容器尚没有对应的交互闭环示例。正交模式由画布提供状态，具体 Action 仍需自行读取并实现约束。
- **影响**：库具备相应的几何/实体 API，不等于调用层已经具备可直接使用的创建工具。
- **建议**：按实际调用需求逐步补建模 Action 和最小示例，并分别验证预览、取消、完成、捕捉和非法参数处理。
- **代码位置**：[BasicPointPlacementAction.cpp](examples/basic/BasicPointPlacementAction.cpp)、[BasicLineCreationAction.cpp](examples/basic/BasicLineCreationAction.cpp)、[CanvasAction.h](include/MiniGraph/CanvasAction.h)

### MG-014 · P3 · 元数据字段不等于完整图层系统；持久化由调用方承担

- **现状**：实体包含图层名、标签和业务 ID 等字段，但当前没有完整的图层管理、过滤、锁定和图层级可见性工作流；库内也没有完整的文档序列化/反序列化闭环。
- **影响**：调用方需要自行实现文档保存、加载以及图层业务逻辑。应避免把“可设置 layerName”描述成已经提供完整图层功能。
- **建议**：在功能清单中继续把库能力和调用层责任分开；只有在明确要求由 miniGraph 接管持久化或图层管理时，再扩展公共 API。

### MG-015 · P3 · 线程、安全和变更通知契约需要更明确【已明确】

- **契约**：`CanvasView`、关联的 `CanvasDocument` 及附加数据不提供并发保护，应在 GUI 线程访问和修改；扩展绘制回调在绘制期间同步执行，不能与工作线程并发读写。普通 QObject 成员函数调用不会自动排队到对象线程。
- **通知**：替换数据通过 `SetItemCanvasData()` / `SetCanvasGlobalData()`；原地修改后须在 GUI 线程调用对应 `Notify...Changed()`，成功时由文档发出 `Changed()`，关联视图据此刷新。
- **克隆**：实体克隆会调用 `CanvasData::Clone()`，要求非空、不同于源对象且隔离可变状态，之后框架重新绑定副本。`CanvasGlobalData::Clone()` 当前没有自动调用路径，只由需要独立文档状态的调用方显式调用。
- **处理**：更新公共头文件注释、功能清单与 [Canvas扩展数据使用契约.md](Canvas扩展数据使用契约.md)，并在基础示例中展示实体数据克隆、两类数据的原地修改和刷新通知；不新增锁或线程断言，不改变库运行时行为。
- **验证**：Qt 5.12.10 x64 Release 下重新构建动态库、测试程序及基础示例，并运行全量 QtTest；结果记录在本节验证项。
- **代码位置**：[CanvasView.h](include/MiniGraph/CanvasView.h)、[CanvasDocument.h](include/MiniGraph/CanvasDocument.h)、[CanvasData.h](include/MiniGraph/CanvasData.h)、[CanvasGlobalData.h](include/MiniGraph/CanvasGlobalData.h)、[BasicCanvasData.h](examples/basic/BasicCanvasData.h)、[BasicCanvasData.cpp](examples/basic/BasicCanvasData.cpp)、[Main.cpp](examples/basic/Main.cpp#L114)、[Canvas扩展数据使用契约.md](Canvas扩展数据使用契约.md)

## 7. API 一致性与维护性

### MG-016 · P2 · 实体样式校验与文档样式校验范围不同【已修复】

- **统一规则**：`StruCanvasStyle::IsValid()` 要求描边色和选中描边色有效、线宽为有限非负数、点尺寸为有限正数；`strokeWidth == 0` 仍然有效。
- **处理**：`CanvasEntity::SetStyle()`、文档实体树校验和 `CanvasDocument::SetItemStyle()` 共用 `StruCanvasStyle::IsValid()`，实体设置与文档入库/更新不再对同一样式给出不同结论。
- **验证**：新增 `CanvasStyleValidationIsConsistentAcrossEntityAndDocument`，覆盖零点尺寸、两种无效颜色、零线宽，以及失败后实体/文档状态不变且不发出 `Changed()`。Qt 5.12.10 x64 Release 常规版全量 QtTest 为 184 passed、0 failed；性能分析版为 186 passed、0 failed。
- **代码位置**：[CanvasTypes.h](include/MiniGraph/CanvasTypes.h)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L262)、[CanvasDocument.cpp](src/CanvasDocument.cpp#L15)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L2852)

### MG-017 · P3 · 视图和核心实现文件职责较集中

- **现状**：`CanvasView.cpp` 承担绘制、导航、选择、捕捉、编辑和 Action 调度等多种职责；`CanvasDocument.cpp` 和默认渲染器也包含较多逻辑；测试集中在一个较大的测试源文件中。
- **影响**：后续增加交互时更容易产生回归，审查和定位成本上升。
- **建议**：当前不建议为“拆文件”而做大规模重构。先修复确定性问题并补齐针对性测试；当某一职责持续扩展或难以独立验证时，再做小步提取。

## 8. 安全边界说明

本次源码检查没有发现网络服务、远程输入协议或库内文档加载器，因此没有证据把当前问题定性为远程可利用漏洞。需要注意的是，CanvasData 的 `image` 命令会按指令中的路径读取图片；如果未来宿主把不可信文件/指令直接交给该渲染链路，可能带来本地文件读取范围和超大图像解码造成资源消耗的问题。该风险取决于宿主输入来源，不是当前已确认的远程攻击路径。

## 9. 外部审查结论核验与补充

以下内容依据当前工作区源码核验。外部分析中的行号与当前源码有不一致处，因此以当前文件和函数行为为准。新增观察只记录实现事实或条件性风险，不将其等同于“必须复刻 CADLab 的交互差异”。

### 9.1 核实后补充的观察项

#### MG-018 · P3 · 内置移动/控制柄编辑路径不消费当前捕捉结果

- **现象**：鼠标移动时，控制柄编辑调用 `UpdateEdit(MapToWorld(...))`；图元拖动则由当前与上次鼠标世界坐标的差值调用 `TranslateItems()`。这两条路径使用映射后的原始坐标，没有使用 `m_CurrentSnapResult`。创建 Action 可以自行读取捕捉结果，因此当前捕捉能力并非整体不可用。
- **影响**：只有当产品契约要求内置移动或控制柄编辑吸附到捕捉结果时，这才构成行为缺口；若捕捉目前只用于光标提示和创建 Action，则属于现有范围。若将来接入移动捕捉，还需明确被移动实体是否从候选中排除，避免自捕捉问题。
- **建议**：先在 API/使用说明中明确当前捕捉适用范围；只有确认要扩展到内置编辑时，再设计排除实体策略并增加交互测试。本项不要求改变当前交互效果。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L1410)、[CanvasView.cpp](src/CanvasView.cpp#L1464)、[CanvasSnap.h](include/MiniGraph/CanvasSnap.h#L67)

#### MG-019 · P3 · 两种交互模式对触控板纵向 `pixelDelta` 使用相反符号

- **现象**：`PreviewNavigation` 将纵向 `pixelDelta` 原样传给 `PanBy()`；`Edit` 模式则传入其相反数。已有测试覆盖 PreviewNavigation 手势，但没有找到用于确认两种模式纵向映射契约的对应测试。
- **影响**：源码中的符号差异是确定的；是否属于问题取决于两种模式是否应保持相同的触控板手势语义，不能仅凭符号差异判定为错误。
- **建议**：保持当前产品效果时，应补测试或文档明确两种模式各自的方向约定；若用户反馈方向不一致，再在目标 Qt 版本和实际触控板设备上验证。本项不预设应改成哪一种方向。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L1728)、[CanvasView.cpp](src/CanvasView.cpp#L1804)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L4189)

#### MG-020 · P3 · 多选删除逐项发出文档变更并刷新捕捉

- **现象**：Delete/Backspace 对选中 ID 逐个调用 `CanvasDocument::RemoveItem()`；每次成功移除都会发出 `Changed()`，视图收到后调用 `UpdateSnap()`。在编辑模式且捕捉开启时，这会导致逐项重复扫描候选实体。代码中的多次 `update()` 请求不等同于同次数的实际绘制事件。
- **影响**：大量实体同时删除时存在重复通知和捕捉扫描成本；没有数据表明普通规模下会造成可感知延迟。
- **建议**：若批量删除是常见场景，先加基准或计数测试；确认成本后再考虑最小化的批量删除/合并通知接口，避免仅为假设场景扩大 API。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L1192)、[CanvasDocument.cpp](src/CanvasDocument.cpp#L492)、[CanvasView.cpp](src/CanvasView.cpp#L675)、[CanvasView.cpp](src/CanvasView.cpp#L1825)

#### MG-021 · P3 · 基础示例中的 Qt 控件成员命名不符合项目编码规范

- **现象**：`GeometryCreateDialog` 使用 `m_TypeComboBox`、`m_PointXSpinBox`、`m_PolylineClosedCheckBox` 等成员名；《二维组代码规范要求》第 I.9 节规定 Qt 控件变量使用类型前缀，例如 `QComboBox` 用 `cb`、`QDoubleSpinBox` 用 `spin`、`QCheckBox` 用 `ck`。
- **影响**：这是示例代码的规范一致性问题，不影响动态库运行，也不代表库公共 API 有缺陷。
- **建议**：后续修改该示例时按规范命名；本次仅更新审查报告，不顺带重命名代码。
- **代码位置**：[GeometryCreateDialog.h](examples/basic/GeometryCreateDialog.h#L39)、[GeometryCreateDialog.h](examples/basic/GeometryCreateDialog.h#L42)、[GeometryCreateDialog.h](examples/basic/GeometryCreateDialog.h#L62)、[二维组代码规范要求.docx](../二维组代码规范要求.docx)

### 9.2 本次源码复核发现的补充数值风险

#### MG-022 · P2 · 极大有限圆弧角度差可能溢出并产生非有限扫掠角【已修复】

- **原报告描述已过时**：几何 API 与 `CanvasArc` 共用 `detail::IsValidArcSweep()`；该函数验证起止角、角度差及归一化差值均有限，并拒绝容差内的零/整周差。角度差溢出时，实体设置和几何合法性检查都会失败。
- **处理**：`GetArcSweep()` 对无效几何返回 `0.0`，圆弧离散化拒绝无效几何；两层共享的校验避免了输入通过验证后再把 NaN/无穷传播到扫掠结果。
- **验证**：`GeometryRejectsOverflowingArcAngleDifference` 使用 `-max` 到 `+max` 的角度，检查几何有效性为 false、扫掠为零、离散化为空，且 `CanvasArc::SetGeometry()` 拒绝输入。本次 Qt 5.12.10 x64 Debug 专项测试通过。
- **代码位置**：[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L34)、[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L497)、[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L672)、[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L708)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L49)、[TstCanvasCore.cpp](tests/TstCanvasCore.cpp#L2734)

### 9.3 外部分析中未被当前源码支持或不应直接定性的说法

| 外部说法 | 当前源码核验 | 结论 |
|---|---|---|
| 三张 `QImage` 每帧都重新分配，造成持续分配/释放 | `PrepareLayer()` 只在尺寸、格式或 DPR 变化时创建新图像；普通绘制复用缓冲，但每帧清空并全量重绘 | “每帧分配”不成立；真实待测点是全量清空、绘制和合成，详见 MG-009 |
| 默认渲染器每帧创建五个 `QHash` | 当前 `CanvasPainterRenderer::Render()` 遍历 `GetItemIds()` 并逐项取实体，没有该处创建五个 `QHash` 的代码 | 对当前实现不成立；线性遍历风险见 MG-010 |
| 框选矩形完全位于闭合多段线内部却没有选中，是交叉框选遗漏 | 当前闭合多段线按无填充轮廓绘制，选择以几何路径相交为准；框完全位于轮廓内部时并未与轮廓相交 | 在当前“仅轮廓”语义下不是已确认缺陷；若以后增加填充区域语义，再单独定义面积选择规则 |
| `FitRect()` 对点或退化包围盒的处理必然造成严重缩放错误 | 当前实现将世界宽高下限设为 1，可能导致退化范围按至少 1 个世界单位拟合；缩放仍受变换边界限制 | 属于退化范围的拟合策略，可补边界测试或讨论期望行为；现有证据不足以定为 P1 或失控缩放缺陷 |
| 一定能减少 90% 绘制耗时、十万图元会耗时若干毫秒，或相关项应定为 P0/P1 | 当前没有相应的大场景性能基准；本报告也未验证这些数字 | 删除定量结论和无基准严重度判断，先测量再评估 |
| 实体采用纯数据结构，因此新增类型的核心问题是避免虚函数分发 | `CanvasEntity` 是多态基类，`Clone()` 为纯虚函数 | 该前提与当前设计不符；新增实体是否需要新的扩展抽象，应由开放式插件需求决定，不作为当前缺陷 |
| 只能设置一个自定义 Renderer 本身就是架构缺陷 | 当前确实提供单个 `SetCustomRenderer()`；宿主可按需用组合 Renderer 汇总多个绘制模块 | 这是当前 API 形态，不是已确认缺陷；只有出现多模块独立注册需求时再考虑列表接口 |

## 10. 测试与验证情况

- 本次 MG-003 修复后，Qt 5.12.10 x64 Debug/Release 均完成全量构建，各有 174 项 QtTest 通过；Qt 6.5.3 的最近一次已知记录仍为上一轮 163 项通过，本次未重跑。
- 本次 MG-004 修复后，Qt 5.12.10 x64 Debug 动态库和测试程序均重新构建；全量 QtTest 为 176 passed、0 failed、0 skipped。新增映射和拟合边界测试先以预期失败复现，再通过修复。
- 本次 MG-005 修复后，Qt 5.12.10 x64 Debug 动态库和测试程序均重新构建；三项 MG-005 专项测试通过，全量 QtTest 为 179 passed、0 failed、0 skipped。专项测试覆盖不安全坐标索引、10000 刻度上限及非有限网格间距。
- 本次复核 MG-006 的捕捉/命中非有限距离保护及 MG-022 的圆弧角差溢出保护均已存在；两项 MG-006 专项测试和一项 MG-022 专项测试通过，全量 QtTest 为 179 passed、0 failed、0 skipped。
- 本次 MG-008 增加 64 层边界与深层克隆/文档接入测试；Qt 5.12.10 x64 Debug、Release 动态库和测试程序均重新构建，全量 QtTest 各为 180 passed、0 failed、0 skipped。新测试先确认原实现会接受第 65 层，再验证追加/替换拒绝和失败时状态保持。
- 本次 MG-015 明确扩展数据的 GUI 线程访问、原地修改后的通知责任与两类 Clone 的调用边界；Qt 5.12.10 x64 Release 动态库、测试程序和基础示例重新构建，全量 QtTest 为 184 passed、0 failed、0 skipped。该项仅补契约、文档和示例，不改变库运行时逻辑。
- 当前专项覆盖 MG-004 变换入口的非有限输入、非法视口尺寸、映射/拟合溢出及失败时状态保持，MG-005 网格/标尺的刻度范围和间距边界，MG-006 命中/捕捉非有限距离，以及 MG-022 几何 API 与实体层对圆弧角差溢出的共同拒绝；也覆盖此前的 MG-003 圆弧有效性容差、MG-002 多段线绘制/选择/编辑柄/捕捉。Qt 6.5.3 本次未重跑；MG-008 的 Qt 5.12.10 x64 Debug/Release 全量验证见上一条。
- 剩余验证/需求项：组的总子图元数量尚无硬上限；若命令格式承诺支持任意文本/路径，再补带逗号和空字段用例；以及不同实体数量下的性能基准。
- 目前没有足够基准数据来承诺可支持的最大图元数或帧率。

## 11. 建议处理顺序

1. MG-001 已确认由当前实现和回归测试覆盖；API 失败返回、绑定状态与变更信号保持一致。
2. MG-002 已按“父对象统一描边样式、子段可见性独立生效”修复，绘制、选择、编辑柄和捕捉已统一过滤隐藏子段。
3. MG-003 已统一圆弧几何 API 与实体层的角差校验；完整圆周由 Circle 类型表达。
4. MG-004、MG-005、MG-006、MG-022 已由当前源码和回归测试覆盖；数值保护项暂告一段落。
5. MG-008 已将组嵌套深度限制为 64 层；是否需要为超宽组设置总子项上限，按实际产品/API 契约决定。
6. MG-007 已明确并记录当前命令格式契约；只有产品需求要求无损表达逗号或空字段时，才另行设计兼容扩展。
7. MG-009 至 MG-011 的本机 Release 基线已记录；后续对照目标硬件及实际场景决定是否进行定向优化。
8. MG-012、MG-014、MG-015 的调用边界和数据使用契约已记录；后续根据实际需求补充 MG-013 的创建 Action 示例，暂不做与需求无关的大重构。
