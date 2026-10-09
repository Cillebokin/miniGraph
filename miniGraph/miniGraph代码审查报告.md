# miniGraph 代码与功能审查报告

> 审查/更新日期：2026-10-09  
> 审查对象：`D:\MyWorks\ProQt\miniGraph\miniGraph`  画布动态库、测试和基础示例  
> 审查方式：源码静态检查；本报告不等同于安全渗透测试或大场景性能测试
>
> 本次更新：核验 Gemini 3.8 Flash 的部分技术判断，纳入已确认或带条件的补充项，并标明不受源码支持的结论。按用户要求，不评价其“CAD 交互体验与功能缺失（UX Gaps）”部分，也不据此要求改变当前交互效果。

## 1. 总体结论

miniGraph 已形成较完整的画布基础闭环：`CanvasDocument` 管理实体，`CanvasView` 提供视口和交互，默认 `QPainter` 渲染器负责内置图元，捕捉、选择、编辑控制柄和 `CanvasAction` 扩展机制也已接入。当前实现符合项目已确定的 QWidget/QPainter 路线。

目前已形成可用的画布基础能力，但性能适用规模尚未通过基准测试确定；因此不把它定性为“中小规模”或承诺具体承载上限。主要待处理项集中在：

1. 少数公共接口存在“失败但报告成功”的确定缺陷；多段线和圆弧还存在需要结合数据契约判断的跨层行为差异。
2. 非有限数值、极大坐标和超深组树等边界输入保护不足。
3. 每次重绘都全量绘制，命中和捕捉线性扫描；尚无大场景基准，不能承诺可扩展到的图元规模。
4. 持久化、完整图层管理、自定义绘制内容参与选择/捕捉等能力仍属于调用层或后续功能范围。

本报告中的“缺陷”指代码路径已能确认的问题；“风险”指在特定输入或规模下可能出现的问题；“功能缺口”不代表现有接口承诺了该功能。

## 2. 严重度定义

| 标记 | 含义 |
|---|---|
| P1 | 常规公共 API 使用可能造成状态未更新、结果错误或明显交互不一致，建议优先修复 |
| P2 | 边界输入、复杂场景或常见数据格式可能触发，建议补保护和回归测试 |
| P3 | 规模、维护性、产品完整度或接口约定方面的改进项，不代表当前必然出错 |

## 3. 确定缺陷与待明确的跨层语义

### MG-001 · P1 · `SetItemCanvasData()` 可能失败却返回成功

- **现象**：文档将某个 `CanvasData` 实例绑定到图元 A 后，再尝试将同一实例绑定到图元 B。实体层的 `TrySetCanvasData()` 会拒绝第二次绑定，但 `CanvasDocument::SetItemCanvasData()` 调用的是不返回结果的 `SetCanvasData()`，之后仍返回 `true` 并发出 `Changed()`。
- **影响**：调用方无法判断绑定是否真正成功；可能继续基于错误假设更新数据或业务状态，观察者也会收到一次实际未变更的刷新通知。
- **建议**：在文档层直接调用 `TrySetCanvasData()`；失败时返回 `false` 且不发出 `Changed()`。增加“同一个数据实例绑定两个图元”的文档级回归测试。
- **代码位置**：[CanvasDocument.cpp](src/CanvasDocument.cpp#L650)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L227)

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

### MG-004 · P2 · `CanvasTransform` 未统一拒绝 NaN/无穷输入

- **现象**：`SetZoom()`、`ZoomAt()` 和 `PanBy()` 对参数主要检查正负或直接运算，没有完整检查 `std::isfinite()`。例如 NaN 不满足 `factor <= 0`，可能进入缩放状态；无效平移也可能使中心坐标失效。
- **影响**：一次异常参数就可能污染后续坐标映射和视口状态，导致绘制、命中和捕捉都异常。
- **建议**：所有公开变换入口先验证有限性；计算结果也要验证有限且有效，失败时保持原状态不变。增加 NaN、正负无穷和溢出输入测试。
- **代码位置**：[CanvasTransform.cpp](src/CanvasTransform.cpp#L25)、[CanvasTransform.cpp](src/CanvasTransform.cpp#L69)、[CanvasTransform.cpp](src/CanvasTransform.cpp#L75)

### MG-005 · P2 · 网格和标尺把世界坐标索引直接转换为 `int`

- **现象**：网格和标尺使用 `floor/ceil(world / spacing)` 后直接转成 `int`，并用整数循环绘制。坐标虽为有限值，也可能远超 `int` 可表示范围；循环边界接近整数极限时还存在溢出风险。
- **影响**：极大世界坐标下可能产生未定义或平台相关行为，导致网格/标尺错乱或画布绘制异常。
- **建议**：在转换前检查范围并限制可见刻度数量；使用更宽的整数类型或经范围验证的索引。对极小网格间距和极大视图中心补边界测试。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L1902)、[CanvasView.cpp](src/CanvasView.cpp#L2079)

### MG-006 · P2 · 非有限距离可能被命中/捕捉比较逻辑接受

- **现象**：`CanvasVector::SquaredDistanceTo()` 在差值或平方溢出时返回 NaN。命中和捕捉代码随后只判断 `distanceSquared > toleranceSquared`；NaN 与该值比较为假，且首个候选在“尚无最佳结果”分支可能被记录。
- **影响**：极大坐标或中间计算溢出时，远离鼠标的实体可能被错误命中或捕捉。
- **建议**：在命中/捕捉候选入口显式拒绝非有限距离；对异常距离保持“无命中/无捕捉”。增加可构造溢出差值的测试。
- **代码位置**：[CanvasVector.cpp](src/CanvasVector.cpp#L196)、[CanvasSnap.cpp](src/CanvasSnap.cpp#L276)、[CanvasView.cpp](src/CanvasView.cpp#L2696)

### MG-007 · P3 · CanvasData 命令格式不能表达任意逗号文本和有意义的空字段

- **现象**：渲染器按逗号拆分指令并跳过空字段；当前语法没有引号或转义机制，因此不能可靠表达含逗号的文本/路径或有意义的空参数。
- **影响**：这是当前命令格式的表达能力限制。只有在 API 契约要求任意文本、路径或空字段都可无损传入时，才构成数据错误；目前报告未发现这样的格式承诺。
- **建议**：先文档化当前命令语法和限制；若调用方确实需要任意字符串，再增加明确的引用/转义规则或结构化指令，不把格式扩展预先视为必须重构。
- **代码位置**：[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L1026)、[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L1047)

### MG-008 · P2 · 深层组容器带来递归和重复克隆风险

- **现象**：组的校验、克隆及部分遍历会递归进入子组；`AddChild()` 和替换操作还会克隆整个子实体树。接口没有嵌套深度或节点数量限制。
- **影响**：人为构造的深组树可能导致栈深增长、重复克隆成本变大，甚至触发栈耗尽。当前没有序列化入口，因此这是 API 使用/规模风险，而非已确认的外部输入攻击路径。
- **建议**：补深度与节点数压力测试；如果允许任意嵌套，再考虑限制深度或改为显式栈遍历，避免在没有实际需求前扩大架构复杂度。
- **代码位置**：[CanvasEntities.cpp](src/CanvasEntities.cpp#L857)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L909)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L922)

## 5. 绘制与交互性能

### MG-009 · P2 · 当前三层离屏图像会复用缓冲，但每次仍全量重绘

- **现象**：背景、几何、overlay 三张 `QImage` 是视图成员并会复用；仅在像素尺寸、格式或设备像素比变化时重新创建。每次绘制仍会清空三张图并重绘网格、全部实体和叠加内容，最后合成到控件；脏矩形没有用于缩小图层绘制范围。
- **影响**：鼠标移动、捕捉指示或小区域状态变化也可能触发全画布处理。DPR=2、1920×1080 逻辑尺寸时，三张 RGBA 图层的像素缓冲估算约占 95 MiB；这是保留缓冲的估算值，不是实测进程内存，也不意味着每帧重新分配这些内存。
- **建议**：先测量典型交互下的帧耗时和内存；再按数据选择局部重绘、背景/内容缓存或降低离屏层数量。缓存必须有清楚的失效条件，避免引入与性能不成比例的复杂度。
- **代码位置**：[CanvasView.cpp](src/CanvasView.cpp#L541)、[CanvasView.cpp](src/CanvasView.cpp#L1618)

### MG-010 · P2 · 绘制、命中、框选和捕捉缺少空间索引

- **现象**：默认渲染器遍历全部实体；命中和捕捉也分别遍历文档 ID，框选同样需要扫描实体。捕捉会随鼠标移动重复执行。
- **影响**：主要操作成本随实体数量线性增加；样条、圆弧、椭圆还需要生成离散路径。大场景存在输入延迟风险，但目前没有基准数据证明具体图元数量阈值、耗时或帧率上限。
- **建议**：先建立不同实体数、曲线比例和鼠标移动频率下的基准，再决定是否需要空间索引或可见区域裁剪；不要在没有基准前直接引入复杂索引。
- **代码位置**：[CanvasPainterRenderer.cpp](src/CanvasPainterRenderer.cpp#L1070)、[CanvasSnap.cpp](src/CanvasSnap.cpp#L303)、[CanvasView.cpp](src/CanvasView.cpp#L2707)

### MG-011 · P2 · CanvasData 指令和图片在绘制期间重复生成/读取

- **现象**：渲染时调用 `CalculateToQStringList()` 并重新解析命令；遇到 `image` 指令时，每次绘制都会创建 `QImageReader` 并读取图片。
- **影响**：包含动态指令或图片的场景会把字符串生成、拆分和图像解码成本叠加到每次全量重绘上。
- **建议**：通过基准确认瓶颈后，再考虑缓存解析结果或图片；需要明确 `CanvasData` 变更通知和缓存失效时机，并限制图片资源的尺寸/读取成本。
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

### MG-015 · P3 · 线程、安全和变更通知契约需要更明确

- **现状**：`CanvasView` 属于 Qt GUI 控件；文档和扩展数据会被绘制/交互回调读取，接口没有提供并发保护。调用方直接修改 `CanvasData` 后还需显式通知文档刷新。`CanvasGlobalData::Clone()` 当前没有对应的文档克隆流程使用。
- **影响**：若调用方从工作线程同时读写数据，可能出现数据竞争；忘记变更通知会导致画面不刷新；克隆接口也容易让使用者误解其生命周期用途。
- **建议**：明确“画布、文档和绑定数据应在 GUI 线程修改”的约定；给 `CanvasData`/`CanvasGlobalData` 增加最小使用示例，并说明通知责任和 Clone 的适用场景。

## 7. API 一致性与维护性

### MG-016 · P2 · 实体样式校验与文档样式校验范围不同

- **现象**：`CanvasEntity::SetStyle()` 允许 `pointSize == 0`，且不检查颜色有效性；文档入库使用的校验要求颜色有效、`pointSize > 0`。因此实体层可能返回设置成功，但之后 `CanvasDocument` 拒绝该实体。
- **影响**：同一个样式在实体 API 与文档 API 下得到不同的有效性结论，调用方容易把入库失败误判为几何问题。
- **建议**：统一样式校验规则，或在接口文档明确“实体样式设置成功不代表可加入文档”；优先让两层复用同一验证逻辑，并覆盖零尺寸和无效颜色测试。
- **代码位置**：[CanvasDocument.cpp](src/CanvasDocument.cpp#L15)、[CanvasEntities.cpp](src/CanvasEntities.cpp#L207)

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

#### MG-022 · P2 · 极大有限圆弧角度差可能溢出并产生非有限扫掠角

- **现象**：`IsValidGeometry(StruArcGeometry)` 分别确认起止角有限后，直接计算 `endAngle - startAngle` 并与容差比较，没有检查差值本身是否仍有限。两个极大但有限、符号相反的角度相减可能溢出为无穷；该比较仍会通过。随后 `CalculateDirectedSweep()` 对无穷值调用 `std::fmod()`，可能得到 NaN，`GetArcSweep()` 因而返回非有限结果。
- **影响**：几何数据通过有效性校验，但后续扫掠查询可能返回 NaN，圆弧离散化也可能因非有限扫掠角而返回空路径。
- **建议**：校验中间角度差和扫掠结果的有限性；对不能安全计算的输入明确拒绝。增加 `-std::numeric_limits<qreal>::max()` 到 `std::numeric_limits<qreal>::max()` 等极值测试，并验证合法性检查与后续算法不会传播 NaN/无穷。
- **代码位置**：[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L34)、[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L649)、[CanvasGeometry.cpp](src/CanvasGeometry.cpp#L683)

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
- 本次验证覆盖 MG-003 圆弧有效性容差及 MG-002 多段线绘制、点击/框选、编辑柄和捕捉；不代表报告中其他边界用例已验证。
- 建议补充：CanvasData 绑定冲突、NaN/无穷/巨大坐标、距离溢出、深层组；若命令格式承诺支持任意文本/路径，再补带逗号和空字段用例；以及不同实体数量下的性能基准。
- 目前没有足够基准数据来承诺可支持的最大图元数或帧率。

## 11. 建议处理顺序

1. 修复 MG-001，确保文档 API 的返回值、数据状态和变更信号一致。
2. MG-002 已按“父对象统一描边样式、子段可见性独立生效”修复，绘制、选择、编辑柄和捕捉已统一过滤隐藏子段。
3. MG-003 已统一圆弧几何 API 与实体层的角差校验；完整圆周由 Circle 类型表达。
4. 为 MG-004 至 MG-006、MG-022 补齐数值范围和非有限值保护。
5. 明确 MG-007 命令格式的兼容契约；若确需无损表达任意字符串，再定义转义/空字段规则并补测试。
6. 为 MG-009 至 MG-011 建立基准后再做定向性能优化。
7. 根据实际调用需求补充 MG-012 至 MG-015 的文档和示例；暂不做与需求无关的大重构。
