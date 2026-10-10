# CanvasData 绘制命令格式

`CanvasData::CalculateToQStringList()` 与 `CanvasGlobalData` 的前置、后置指令使用同一套格式。每个 `QStringList` 元素是一条独立命令，命令按列表顺序执行：

```cpp
QStringList()
        << QStringLiteral("Pen,255,64,32,255,1.5,1")
        << QStringLiteral("Line,0,0,100,50")
        << QStringLiteral("Text,示例文字,10,20,5,0");
```

## 通用语法

- 命令格式为 `命令名,参数1,参数2,...`，使用英文逗号（`,`）分隔。
- 命令名大小写不敏感；命令名字段不要添加前后空白。
- 当前格式不是 CSV：不支持引号、逗号转义或其他转义规则。文本、字体名称、图片路径等参数不能包含逗号。
- 解析时会跳过空字段。因此空参数不能表达；例如连续逗号不会产生空参数，而会使后续字段位置前移。
- 固定参数命令至少需要下表列出的参数数量；额外参数当前会被忽略。多段坐标命令会消费所有剩余坐标。
- 数值参数使用十进制整数或以 `.` 表示小数点的实数；实数必须是有限值。命令未知、字段数量不足或参数校验失败时，该命令会被忽略，不会报错。
- `Pen`、`Brush`、`Font` 会更新当前命令列表中的绘制状态，影响其后的绘制命令；状态不会泄漏到该列表之外。

## 命令参数

以下参数均写在命令名之后。

| 命令 | 参数 | 规则 |
| --- | --- | --- |
| `Pen` | `red,green,blue,alpha,width,style` | RGBA 为 0–255 整数；`width` 为非负有限实数；`style` 为 0–6 的 `Qt::PenStyle` 值 |
| `Brush` | `red,green,blue,alpha,style` | RGBA 为 0–255 整数；`style` 支持 `Qt::BrushStyle` 数值 0–17 和 24，包含 hatch 图案 |
| `Font` | `family,pointSize,bold,italic` | `pointSize` 为正整数；`bold`、`italic` 为整数，非零表示启用；字体名称中的空白会被合并 |
| `Point` | `x,y` | 世界坐标 |
| `Line` | `x1,y1,x2,y2` | 起点和终点的世界坐标 |
| `Arc` | `centerX,centerY,radius,startAngle,endAngle,counterClockwise` | 角度用弧度；半径必须大于 0；方向参数只能为 `0`（顺时针）或 `1`（逆时针） |
| `Ellipse` | `centerX,centerY,radiusX,radiusY` | 两个半径必须大于 0 |
| `Polyline` | `x1,y1,x2,y2,...` | 至少 2 个点；坐标必须成对出现，按给定顺序连接，不闭合 |
| `Polygon` | `x1,y1,x2,y2,...` | 至少 3 个点；坐标必须成对出现，按给定顺序连接并闭合 |
| `Rect` | `left,top,width,height` | 以 `(left, top)` 为左上角；另一侧坐标为 `(left + width, top - height)` |
| `Cubic` | `closed,startX,startY,control1X,control1Y,control2X,control2Y,endX,endY,...` | `closed` 为 `0` 或 `1`；起点后每段提供 6 个坐标值；至少一段 |
| `Quad` | `closed,startX,startY,controlX,controlY,endX,endY,...` | `closed` 为 `0` 或 `1`；起点后每段提供 4 个坐标值；至少一段 |
| `Text` | `text,x,y,alignment,radians` | 文本字段不能包含逗号；位置为世界坐标；旋转角用弧度；`0` 使用指定点作为基线起点，`1`–`9` 依次表示上/中/下 × 左/中/右对齐 |
| `Image` | `path,x,y,radians,scaleX,scaleY,followScale` | 路径不能包含逗号；位置为图像左上角的世界坐标；角度用弧度；缩放值非正时按 1 处理；`followScale=1` 时随画布缩放，其余值按屏幕固定尺寸绘制 |

## 兼容性边界

该格式用于当前 miniGraph 绘制指令，不承诺任意文本、路径或空参数的无损往返。调用方应避免在字段中使用逗号或空字段。若未来需要支持这类数据，应新增明确的转义或结构化格式，并制定与现有逗号格式的兼容规则；当前实现不隐式解释引号或反斜杠转义。
