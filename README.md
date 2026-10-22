# 电路原理图编辑器（EDA）— 代码结构与逻辑导读

一个用 **wxWidgets + C++17** 写的逻辑电路原理图编辑器：左侧"元件库"选元件，右侧画布放元件、连线、拖动、缩放，图纸可保存/打开。输入和输出元件支持点击切换 0/1，后端支持六种逻辑门的组合逻辑计算。画布会显示每个元件的运算结果，并用红色/蓝灰色区分为高/低电平导线。

本文档说明**每块逻辑写在哪个文件、哪一行**，以及它们之间怎么串起来。
行号是当前版本的参考值，改动代码后可能偏移，定位请以**函数名**为准。

本文已按此次改动更新原有章节；前端读取计算结果见 **5.7 节**，Git 上传步骤见 **10 节**。画布会显示元件 0/1 值：输入/输出值位于元件中心，逻辑门结果靠近输出脚。

---

## 1. 文件职责（代码地图）

| 文件 | 角色 | 主要内容 |
| --- | --- | --- |
| `Main.cpp` / `Main.h` | 程序入口 | `MyApp::OnInit()` 创建并显示 `MainFrame`，`wxIMPLEMENT_APP` 生成 `main()` |
| `EDA.h` | 全局声明 | 所有结构体、枚举、类声明（数据模型 + 元件库 + 画布 + 主窗口） |
| `EDA.cpp` | **后端数据层** | `Schematic` 的增删改查、选中管理、引脚坐标计算、0/1 切换、连线校验、拓扑求值、文件序列化/反序列化 |
| `Library.cpp` | **元件库层** | 8 种元件的规格表、几何图形绘制、按类型创建元件 |
| `Canvas.cpp` | **画布层（UI + 交互）** | 绘制、鼠标键盘事件、放置/选择/拖动/连线、缩放平移、打开保存 |
| `MainFrame.cpp` | **主窗口层（UI）** | 菜单、侧边栏、状态栏、命令响应、界面与模型同步 |
| `CMakeLists.txt` | 本地构建脚本 | 找 wxWidgets、配置编译编码、生成 `EDA.exe`，可选生成回归测试；已加入忽略规则，已跟踪文件的处理见 10.2 节 |
| `tests/SchematicTests.cpp` | 回归测试 | 验证真值表、信号传播、错误处理、文件兼容和画布点击调用 |

一句话分层：

```
Main.cpp  →  MainFrame（窗口/菜单/侧边栏）  →  Canvas（画布交互与绘制）  →  Schematic（纯数据）
                                                     ↘  ShapeLibrary（元件形状库）
```

`Schematic` 和 `ShapeLibrary` 完全不知道 UI 的存在，可以单独测试；`Canvas` 负责把两者接到界面上。

---

## 2. 构建与运行

MinGW 动态库配置：

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
.\build\EDA.exe
```

MSVC 静态库配置：

```powershell
cmake -S . -B build-msvc -G "Visual Studio 18 2026" -A x64
cmake --build build-msvc --config Debug
.\build-msvc\Debug\EDA.exe
```

请按本机安装的 Visual Studio 调整生成器版本。MinGW 和 MSVC 需要各自的构建目录，已有构建目录不要切换生成器。

构建要点（见 `CMakeLists.txt`）：

- 需要 wxWidgets。脚本依次查找环境变量 `WXWIN`、本机 MSVC 安装 `D:/wxWeigets/wxWidgets-3.3.3`、相邻的 MinGW 安装 `../wxWidgets-3.2.2`，也可直接指定 `wxWidgets_ROOT_DIR`。
- 脚本按库目录中是否存在 DLL 自动选择动态或静态链接。动态库配置会把 wxWidgets DLL 复制到程序目录；编译器、架构及 Debug/Release 库需匹配。
- MSVC 编译选项含 `/utf-8`，MinGW 使用 UTF-8 源码；中文字符串及编辑器编码约定见第 7 节。

开启并运行回归测试（本地 `CMakeLists.txt` 需包含 `EDA_BUILD_TESTS` 选项）：

```powershell
cmake -S . -B build -DEDA_BUILD_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure --timeout 30
```

`EDA_BUILD_TESTS` 默认关闭。开启后生成 `EDA_tests.exe`，CTest 注册 `schematic_logic` 和 `canvas_clicks` 两组测试；此次验证通过 253 项后端检查和 20 项画布事件检查。画布测试使用未显示的窗口直接投递事件。

---

## 3. 界面（UI）部分

### 3.1 主窗口布局 —— `MainFrame::MainFrame`（`MainFrame.cpp:25`）

| 行 | 代码 | 作用 |
| --- | --- | --- |
| 27 | `m_canvas = new Canvas(this);` | 创建右侧画布（先创建，作为 Frame 的第一个子窗口） |
| 29 | `new wxPanel(this)` | 左侧侧边栏容器 |
| 30 | `new wxStaticText(..., L"元件库")` | 侧边栏标题 |
| 31-37 | `m_palette = new wxListBox(...)` | **元件库列表**：第 0 项固定为"选择 / 移动"，随后追加 `ShapeLibrary` 里的 8 种元件；默认选中第 0 项 |
| 39-42 | `sidebarSizer` | 侧边栏纵向布局（标题 / 元件库列表） |
| 44-47 | `sizer` | 主布局：左侧边栏（固定宽）+ 右画布（拉伸填充） |
| 49-77 | 菜单栏 | 文件 / 编辑 / 视图 / 帮助 四个菜单 |
| 79-81 | `CreateStatusBar(2)` | 状态栏分两栏：左为提示或逻辑错误，右为输入/输出高电平统计及元件、导线数量 |
| 83-98 | `Bind(...)` | **所有事件绑定的集中位置**（见下表） |
| 100-102 | `SetSize / Centre / SyncUi` | 窗口尺寸、居中、首次刷新界面 |

当前版本没有 `m_labelEdit`、`OnLabelChanged` 或 `SetSelectedLabel`，原文中对应旧版改名输入框的描述已移除。此次也清理了 `EDA.h` 中残留的合并冲突标记和无实现的改名回调声明。

### 3.2 菜单与命令 ID（`MainFrame.cpp:11-21`）

自定义 ID 从 `wxID_HIGHEST + 1` 开始，避免和 wx 标准 ID 冲突：

```
ID_SAVE  ID_SAVE_AS  ID_DELETE  ID_ROTATE
ID_ZOOM_IN  ID_ZOOM_OUT  ID_ZOOM_RESET  ID_FIT  ID_GRID
```

这些枚举依次递增，具体数值以所用 wxWidgets 的 `wxID_HIGHEST` 为准。

标准 ID：`wxID_NEW`（新建）、`wxID_OPEN`（打开）、`wxID_EXIT`（退出）、`wxID_ABOUT`（关于）。

### 3.3 事件绑定表（`MainFrame.cpp:83` 起）

| 事件 | 处理函数 | 说明 |
| --- | --- | --- |
| `wxEVT_MENU` + 各 ID | `OnNew` / `OnOpen` / `OnSave` / `OnSaveAs` / `OnExit` / `OnDelete` / `OnRotate` / `OnZoom*` / `OnFit` / `OnToggleGrid` / `OnAbout` | 菜单命令，基本都是转发给 `m_canvas` |
| `wxEVT_LISTBOX`（`m_palette`） | `OnPalette`（`:204`） | **在元件库里选元件** → 进入放置模式 |
| `wxEVT_TEXT` + `kCanvasChangedId` | `OnCanvasChanged`（`:213`） | 画布发来的"状态变了"通知 → `SyncUi()` |
| `wxEVT_CLOSE_WINDOW` | `OnClose`（`:217`） | 关窗时询问是否保存 |

### 3.4 画布窗口 —— `Canvas::Canvas`（`Canvas.cpp:37`）

| 行 | 事件 | 处理函数 | 作用 |
| --- | --- | --- | --- |
| 43 | `wxEVT_PAINT` | `OnPaint`（`:349`） | 全部绘制入口（网格 → 导线 → 元件 → 预览） |
| 44 | `wxEVT_LEFT_DOWN` | `OnLeftDown`（`:368`） | 放置 / 选择 / 开始连线；记录点击候选；处理后端拒绝连线 |
| 45 | `wxEVT_LEFT_UP` | `OnLeftUp`（`:446`） | 结束拖动；确认有效单击后调用 `ToggleValue()` |
| 46 | `wxEVT_MOTION` | `OnMotion`（`:466`） | 拖动元件 / 中键平移 / 刷新预览；记录是否发生拖动 |
| 47 | `wxEVT_MOUSEWHEEL` | `OnMouseWheel`（`:506`） | 清除点击候选，以鼠标为中心缩放 |
| 48 | `wxEVT_KEY_DOWN` | `OnKeyDown`（`:517`） | 清除点击候选；Esc 取消、Del 删除、R 旋转、方向键微调 |
| 49-50 | `wxEVT_MIDDLE_DOWN/UP` | `OnMiddleDown/Up`（`:552`,`:560`） | 中键拖拽平移画布 |
| 51 | `wxEVT_MOUSE_CAPTURE_LOST` | `OnCaptureLost`（`:565`） | 新增绑定；清除拖动、点击候选和平移状态 |
| 52 | `wxEVT_ERASE_BACKGROUND` | 空 lambda | 关掉闪烁，配合 `wxBG_STYLE_PAINT` + 双缓冲 |

---

## 4. 后端逻辑（数据与算法）

### 4.1 数据结构（`EDA.h`）

| 类型 | 行 | 字段要点 |
| --- | --- | --- |
| `ComponentType` | 14 | 8 种元件：AND/OR/NOT/NAND/NOR/XOR/INPUT/OUTPUT（`COUNT` 为计数哨兵） |
| `Rotation` | 26 | 0/90/180/270 度；`RotateCW`（33）、`RotationDegrees`（37） |
| `Pin` | 41 | `name`、`offset`（相对元件中心的**未旋转**偏移）、`output` 是否输出脚 |
| `Component` | 47 | 原有位置、旋转、标签、尺寸、引脚和选中属性；新增 `bool value = false`，表示 0/1 |
| `Wire` | 60 | 只记录两个端点的元件 ID 和引脚序号，不存坐标或信号值；起点不一定是信号源 |
| `ComponentSpec` | 113 | 元件"模板"：类型、英文名、中文名、尺寸、引脚表 |
| `Schematic` | 68 | 图纸数据、选择和自增 ID；新增 `m_logicError` 以及切换、求值、错误读取接口 |

> **设计要点**：导线不存坐标，靠 `GetPinPos()` 实时算，所以移动元件时导线自动跟随。

### 4.2 图纸数据操作 —— `EDA.cpp`

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `RotateOffset` | 13 | 内部辅助函数：根据 0/90/180/270 度计算旋转后的偏移 |
| `Schematic::Clear` | 36 | **修改**：清空图纸、选中和逻辑错误，ID 归 1 |
| `AddComponent` | 46 | **修改**：追加元件，分配并返回新 ID，自动求值 |
| `AddWire` | 54 | **修改**：暂存新导线并求值；成功返回非零 ID，失败撤回导线并返回 `0`，保留错误信息，失败不消耗 ID |
| `RemoveComponent` | 67 | **修改**：不存在则返回 `false`；成功时删除元件及相关导线、清理选中并重算 |
| `RemoveWire` | 86 | **修改**：删除导线并重算，成功返回 `true`，不存在返回 `false` |
| `ToggleValue` | 97 | **新增**：仅 INPUT/OUTPUT 可切换；INPUT 切换后求值，失败恢复输入原值并返回 `false`；OUTPUT 临时反转，返回 `true` |
| `Evaluate` | 112 | **新增**：验证元件、引脚和连线，按拓扑顺序计算六种逻辑门；成功统一写回 `Component::value` 并返回 `true`，失败保持原值并返回 `false` |
| `LogicError` | `EDA.h:84` | **新增**：返回最近一次求值错误；求值成功或清空图纸后为空。`AddWire` 被拒绝后仍保留拒绝原因 |
| `Find`（可写 / 只读） / `FindWire` | 220 / 227 / 234 | 按 ID 查对象，未找到返回 `nullptr`；返回指针不能跨越容器增删长期保存 |
| `ClearSelection` | 241 | 清空所有选中标记 |
| `SelectComponent` | 247 | 选中（`add=false` 时先清空，`add=true` 为 Shift 多选） |
| `SelectWire` | 259 | 选中导线，与元件选中互斥 |
| `IsSelected` | 266 | 判断元件或导线是否选中，供绘制高亮用 |
| `SelectedCount` | 272 | 选中对象总数（元件数 + 是否有导线） |
| `DeleteSelection` | 276 | 先拷贝 ID 再调用删除接口，避免遍历时修改容器；删除接口会触发重算 |
| `GetPinPos`（两个重载） | 283 / 293 | 引脚世界坐标 = 元件中心 + 旋转后的引脚偏移 |
| `Serialize` | 298 | **修改**：导出原有 `COMP/WIRE`，并为 INPUT/OUTPUT 增加 `VALUE` 记录 |
| `Deserialize` | 326 | **修改**：兼容旧文件，恢复值并求值；普通坏行可跳过，非法值记录或电路错误返回 `false`，原模型不被替换 |

### 4.3 元件形状库 —— `Library.cpp`

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `DrawAnd` / `DrawOr` / `DrawTriangle` / `DrawInput` / `DrawOutput` | 18 / 29 / 39 / 48 / 55 | 各元件**以原点为中心的几何路径**（半宽 `w`、半高 `h`） |
| `MakePin` | 10 | 创建引脚描述，设置名称、偏移和输入/输出方向 |
| `DrawBody` | 64 | 按类型分发到上面的绘制函数；NAND/NOR/NOT 的输出小圆由 `Draw` 额外绘制 |
| `ShapeLibrary::Get` | 95 | 单例（函数内 `static`），全局唯一元件库 |
| `ShapeLibrary::ShapeLibrary` | 100 | **规格表**：8 种元件的名称、尺寸、引脚偏移 |
| `Find` / `FindByName` | 121 / 128 | 按类型 / 按名字查规格（反序列化时用名字查回类型） |
| `Create` | 135 | 用规格生成一个元件实例，并自动编标签（`AND1`、`OR2`…，计数器按类型分开） |
| `Bounds` | 152 | 元件的矩形包围盒（用于选择框、`FitToWindow`） |
| `Draw` | 156 | **画一个元件**：平移+旋转坐标系 → 画本体 → 画引脚短线和小圆 → 画标签 |

### 4.4 视图与坐标（`Canvas.cpp`）

| 函数 | 行 | 说明 |
| --- | --- | --- |
| `Distance2` / `DistanceToSegment2` | 15 / 21 | 内部辅助函数：两点距离平方、点到线段距离平方，用于点击和导线命中判定 |
| `ScreenToWorld` | 221 | 屏幕坐标 → 世界坐标：`world = screen / zoom + origin` |
| `Snap` | 227 | 吸附到 20px 网格（`kGridSize`，`EDA.h:11`） |
| `HitBody` | 233 | 本体命中判定（方形包围盒） |
| `HitTest` | 240 | **命中测试总入口**，优先级：引脚（半径 10px）→ 元件本体（从后往前）→ 导线（距离 ≤6px） |
| `DrawGrid` | 274 | 画网格；缩得太小时自动按 5 倍步长抽稀 |
| `DrawWires` | 296 | 画"Z 字形"折线导线；高电平为红色，低电平为蓝灰色，选中时变橙色加粗 |
| `DrawComponents` | 316 | 画所有元件、选中框和 0/1 结果徽标；输入/输出值居中，逻辑门结果靠近输出脚 |
| `DrawPreview` | 328 | 放置模式的半透明预览；连线模式的橡皮筋虚线 |
| `OnPaint` | 349 | 双缓冲绘制；整体变换 `Translate(-origin) + Scale(zoom)`；不执行逻辑求值 |

> 坐标系：`pos` 是**世界坐标**；屏幕像素 = `(world - origin) * zoom`。缩放围绕鼠标位置进行（`OnMouseWheel`）。

### 4.5 文件读写 —— `EDA.cpp:298/326` + `Canvas.cpp:174/202`

文本格式采用 UTF-8，当前另存为默认文件名是 `未命名.eda`，过滤器为 EDA 文件。格式本身与扩展名无关：

```
EDA 1
COMP <id> <类型名> <x> <y> <旋转0-3> <标签>
VALUE <输入或输出元件id> <0或1>
WIRE <id> <起始元件id> <起始引脚序号> <结束元件id> <结束引脚序号>
```

- `Serialize()` 逐行拼字符串；标签放在行尾，因此标签里**不要带换行**。
- `VALUE` 仅保存 INPUT 和 OUTPUT，允许出现在对应 `COMP` 前面；普通逻辑门的结果不存储，由加载后的求值恢复。
- 旧文件没有 `VALUE` 时输入默认 `0`。加载后始终重算：OUTPUT 的保存值会被上游结果覆盖，未连接的 OUTPUT 为 `0`，临时手动反转不跨越重新加载保留。
- 文件头继续使用 `EDA 1`，保留原有 `COMP` 行及标签布局。新读取器兼容旧文件；旧读取器可能将新 `VALUE` 行当作未知行跳过，无法恢复输入值。
- `Deserialize()` 忽略空行和 `#` 注释；一般无法识别或部分格式不合法的 `COMP/WIRE` 行会跳过并累计 `badLines`，返回 `true` 时 `error` 仍可能包含跳过提示。
- 非法/重复的 `VALUE`、值引用不存在或非输入输出元件、重复元件/导线 ID、非法引脚、连接方向错误、多源驱动和环路会导致返回 `false`。先构造临时模型并验证，失败不覆盖当前图纸。
- 成功加载后，自增 ID 接着文件中的最大 ID 递增。调用方应先检查返回值，再区分 `error` 是失败原因还是跳过提示。
- `Canvas::OpenFile` 读文件 → `wxString::FromUTF8` 解码 → `Deserialize`；失败立即返回 `false`，成功后复位交互和脏标记。当前 `MainFrame::OnOpen` 只在失败时弹出错误，成功加载的跳过提示尚未单独展示。
- `Canvas::SaveFile` 调用 `Serialize()` → `utf8_str()` 写出；成功后更新路径并清除脏标记，不触发重算。

### 4.6 逻辑运算与错误处理（新增）

`Component::value` 是结果的统一存储位置：INPUT 保存用户设定值，逻辑门保存该门输出，OUTPUT 保存接收结果。`false` 为 `0`，`true` 为 `1`。引脚索引从 `0` 开始；双输入门为 `IN1=0 / IN2=1 / OUT=2`，NOT 为 `IN=0 / OUT=1`，INPUT 只有输出脚 `0`，OUTPUT 只有输入脚 `0`。

| 元件 | 运算 |
| --- | --- |
| INPUT | 保持自身设定的 `value` |
| OUTPUT | 读取上游信号；未连接时为 `0` |
| AND / OR | `a && b` / `a || b` |
| NOT | `!a` |
| NAND / NOR | `!(a && b)` / `!(a || b)` |
| XOR | `a != b` |

`Evaluate()` 的执行过程：

1. 建立元件 ID 到数组下标的索引，验证元件 ID、类型、引脚数量和方向。
2. 校验导线 ID、端点和引脚范围，根据 `Pin::output` 确定信号源，不依赖用户先点哪一端。
3. 每个输入引脚最多接一根导线；一个输出引脚可以连接多个输入。输出接输出、输入接输入、重复连线和多源驱动会报错。
4. 按依赖数量进行拓扑遍历，先计算上游再计算下游；缺失的输入信号按 `0` 处理。
5. 若无法遍历全部元件，返回组合逻辑环路错误；全部成功才统一写回结果，失败不会部分更新结果。

`ToggleValue(INPUT)`、增删元件、增删导线及加载文件会触发求值；选择、移动、旋转、缩放、绘制和保存不触发。`ToggleValue(OUTPUT)` 只临时反转输出，不影响上游；下一次求值恢复计算结果。

`Evaluate(&error)` 失败返回 `false` 并填写中文原因，同时可用 `LogicError()` 获取；成功清空错误。`AddWire()` 被拒绝后，导线已撤回，但 `LogicError()` 仍保留这次拒绝原因供界面提示。

---

## 5. 交互流程（一次点击到底发生了什么）

### 5.1 在元件库里选元件

```
点击列表项 → wxEVT_LISTBOX → MainFrame::OnPalette (MainFrame.cpp:204)
    index == 0 → Canvas::CancelMode()            // 回到"选择/移动"
    index >= 1 → Canvas::StartPlace(type)        // 进入放置模式（index-1 对应 ComponentType）
                     → SetFocus() + RefreshAndNotify()
                     → 状态栏提示"正在放置 XX，点击画布放置，Esc 取消"
```

### 5.2 在画布上放置元件

```
左键按下 → Canvas::OnLeftDown (Canvas.cpp:368)
    m_mode == Place → ScreenToWorld → Snap → ShapeLibrary::Create → Schematic::AddComponent
                     → 后端自动求值并更新模型中的 value
                     → SetDirty() → RefreshAndNotify()
放置模式不会自动退出：可以连续点击放多个；点"选择 / 移动"或按 Esc 才退出
```

### 5.3 选择 / 拖动元件

```
左键按下 → HitTest
    命中引脚 → 进入连线模式（记下起点元件+引脚）
    命中元件 → Schematic::SelectComponent(id, Shift按下?) → m_dragging = true
             → 记录 m_clickScreen；非 Shift 点击记录 m_clickComponent；捕获鼠标
    命中导线 → Schematic::SelectWire(id)
    空白处   → ClearSelection()
按住拖动 → Canvas::OnMotion (Canvas.cpp:466)
    与按下位置相距超过 3 个屏幕像素 → 标记 m_movedWhileDragging
    拖动增量按 20px 网格吸附 → 所有选中元件一起平移 → SetDirty()
左键释放 → Canvas::OnLeftUp (Canvas.cpp:446)
    确认选择模式、同一元件本体、非 Shift、未拖动且按下/释放距离 ≤3px
             → Schematic::ToggleValue(id)
             → INPUT：切换并传播；OUTPUT：临时反转；普通逻辑门：不切换
             → 切换成功 SetDirty()，否则只刷新通知
```

发生实际网格位移也会标记拖动；即使未达到网格移动距离，只要鼠标曾偏离超过 3px，也不会在返回原处松键时误切换。开始放置、取消、滚轮、键盘、中键平移或捕获丢失都会清除点击候选。相关前端调用和防误触处理均已写中文注释。

### 5.4 连线

```
点第一个引脚 → m_wireStartComponent / m_wireStartPin 记录起点，m_mode = Wire
点第二个引脚 → 组成 Wire → Schematic::AddWire
    返回非零 ID → 后端已校验并重算 → 回到选择模式 → SetDirty()
    返回 0      → 后端拒绝并撤回导线 → 保持连线模式 → 刷新中文错误提示
点空白处     → CancelMode()
```

### 5.5 界面同步链路（重要）

画布任何"状态变化"都走同一条通知链：

```
Canvas::SetDirty() / StartPlace() / Zoom*() ...
        ↓
Canvas::RefreshAndNotify()  (Canvas.cpp:578)
        ├─ Refresh(false)                          // 请求重绘
        └─ wxPostEvent(父窗口, wxCommandEvent(wxEVT_TEXT, kCanvasChangedId))
                          ↓
MainFrame::OnCanvasChanged (MainFrame.cpp:213) → MainFrame::SyncUi() (MainFrame.cpp:244)
        ├─ 标题：文件名/未命名 + 脏标记 "*"
        ├─ 状态栏左栏：Canvas::StatusText()，优先显示 LogicError()
        └─ 状态栏右栏：高电平输入/输出数量、元件数、导线数
```

> `kCanvasChangedId`（`EDA.h:12`）是画布与主窗口之间的私有事件 ID，避免和菜单 ID 混用。

### 5.6 其他命令

| 操作 | 入口 | 落到哪 |
| --- | --- | --- |
| 删除选中 | 菜单/`Del` | `MainFrame::OnDelete` → `Canvas::DeleteSelected` → `Schematic::DeleteSelection` |
| 旋转 | 菜单/`R` | `Canvas::RotateSelected` → 每个选中元件 `RotateCW` |
| 缩放 | 菜单/滚轮 | `Canvas::ZoomIn/Out/Reset/FitToWindow` |
| 切换 0/1 | INPUT/OUTPUT 本体单击 | `Canvas::OnLeftUp` → `Schematic::ToggleValue` |
| 新建/打开/保存 | 文件菜单 | `Canvas::NewFile/OpenFile/SaveFile` |

### 5.7 前端如何调用后端、读取运算结果（新增）

当前前端是 wxWidgets 的 `Canvas/MainFrame`，后端是 `Schematic`，两者在同一进程内直接调用 C++ 接口，没有 HTTP 请求或 JSON 接口。后端不持有控件，也不负责刷新窗口。

**调用和读取顺序：**

```text
用户点击 INPUT 本体
    → Canvas::OnLeftUp 确认有效点击
    → m_model.ToggleValue(componentId)
    → Schematic::Evaluate 同步完成传播并更新所有 Component::value
    → Canvas::SetDirty → RefreshAndNotify
    → OnPaint / MainFrame::OnCanvasChanged 消费已经更新的模型
```

输入切换返回成功时，所有逻辑门和 OUTPUT 的结果已经可读，无需等待绘制或通知事件。OUTPUT 手动切换采用临时反转规则，调用本身不会触发求值。

**在现有 Canvas 成员函数中发起切换：**

```cpp
// 与 OnLeftUp 中的调用方式相同；SetDirty/RefreshAndNotify 是 Canvas 私有成员。
if (m_model.ToggleValue(componentId)) {
    SetDirty();          // 更新未保存标记、请求重绘、通知父窗口
} else {
    RefreshAndNotify();  // 展示可能的逻辑错误；普通逻辑门不能手动切换
}
```

**前端通过元件 ID 读取结果：**

```cpp
#include "EDA.h"

wxString ReadLogicValue(eda::Canvas& canvas, int componentId) {
    const eda::Schematic& model = canvas.Model();
    const eda::Component* c = model.Find(componentId);
    if (!c) return L"元件不存在";
    return wxString::Format(L"%d", c->value ? 1 : 0);
}
```

`Canvas::DrawComponents` 现在直接读取 `Component::value` 绘制 0/1 徽标，`Canvas::DrawWires` 根据输出脚所属元件的 `value` 选择导线颜色。元件值不会写回 `label`，因此不会改变名称或保存格式。选中单个元件时，状态栏左侧显示其输入值、输出值或运算结果；右侧显示高电平输入和输出数量。

| 前端需求 | 后端接口 / 数据 | 约定 |
| --- | --- | --- |
| 读取一个元件的结果 | `Find(id)->value` | 先判断 `Find` 是否为空；INPUT 为设定值，门元件为输出值，OUTPUT 为接收值 |
| 遍历结果 | `Components()` 中每个 `c.value` | 绘制时只读，不在每次重绘中重新求值 |
| 切换输入/输出 | `ToggleValue(id)` | 返回 `bool`；未知 ID 和普通逻辑门返回 `false`，不一定有新错误 |
| 创建连线 | `AddWire(wire)` | 返回非零 ID 才成功；`0` 表示拒绝，读取 `LogicError()` |
| 主动重算 | `Evaluate(&error)` | 成功才使用本次计算结果；失败时原值仍保留 |
| 展示错误 | `LogicError()` / `error` | 当前 `StatusText()` 已接入最近一次求值或拒绝连线的原因 |
| 显示导线信号 | 根据两端 `Pin::output` 找到信号源，再读源元件 `value` | 当前已在 `DrawWires` 中实现；`Wire` 没有独立 `value` 字段 |

`Components()`、`Wires()` 和 `Find()` 也提供可写访问，直接改 `value` 或直接增删集合会绕过自动重算、连线校验和 UI 通知。新增前端交互应优先封装在 Canvas 成员函数中，调用现有模型接口后走 `SetDirty/RefreshAndNotify`；若确需直接编辑模型，应显式执行 `Evaluate()` 并处理结果。跨越元件增删时只保存 ID，使用时重新 `Find()`，避免缓存失效的指针。

---

## 6. 函数速查表

**MainFrame.cpp**

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `MainFrame::MainFrame` | 25 | 建窗口、侧边栏、菜单、状态栏、绑定事件 |
| `OnNew` | 105 | 未保存时询问是否新建，再清空图纸 |
| `OnOpen` | 116 | 打开文件对话框，调用画布加载，失败弹错误框；当前无打开前的未保存确认 |
| `OnSave` / `OnSaveAs` | 131 / 140 | 保存到现有路径或选择 `.eda` 保存路径 |
| `SaveTo` | 151 | 调用 `Canvas::SaveFile`，失败弹错误框 |
| `OnExit` | 161 | `Close()`，触发关闭流程 |
| `OnDelete` / `OnRotate` | 165 / 169 | 转发到画布 |
| `OnZoomIn/Out/Reset` / `OnFit` / `OnToggleGrid` | 173 / 177 / 181 / 185 / 189 | 缩放、适应窗口、网格命令 |
| `OnAbout` | 198 | 关于对话框；此次恢复损坏的中文 |
| `OnPalette` | 204 | 元件库选择 → 放置模式；选择第 0 项则取消模式 |
| `OnCanvasChanged` | 213 | 画布通知 → `SyncUi` |
| `OnClose` | 217 | 关窗前询问保存；取消或保存未完成则阻止关闭 |
| `SyncUi` | 244 | 更新标题、未保存标记和两栏状态；此次恢复“未命名”等中文 |

**Canvas.cpp**

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `Canvas::Canvas` | 37 | 构造并绑定绘制、输入事件；新增捕获丢失处理绑定 |
| `StatusText` | 55 | **修改**：优先返回 `LogicError()`，否则显示放置/连线/选中提示；恢复中文 |
| `StartPlace` / `CancelMode` | 72 / 83 | **修改**：切换模式，同时清除点击候选、拖动状态及非平移时的鼠标捕获 |
| `RotateSelected` | 93 | 旋转选中元件并标记脏，不重算逻辑 |
| `DeleteSelected` / `SelectAll` | 104 / 110 | 删除选中对象（模型负责重算）/ 全选 |
| `ZoomIn` / `ZoomOut` / `ZoomReset` / `FitToWindow` / `ToggleGrid` | 118 / 123 / 128 / 135 / 156 | 视图控制，不重算逻辑 |
| `NewFile` | 161 | **修改**：先取消交互，再清空模型、路径、脏标记和视图 |
| `OpenFile` | 174 | **修改**：读取并解码文件，检查 `Deserialize` 返回值；仅成功后取消交互、更新路径并清除脏标记 |
| `SaveFile` | 202 | 写出模型的 UTF-8 序列化内容，成功后更新路径、清除脏标记 |
| `ScreenToWorld` / `Snap` / `HitBody` / `HitTest` | 221 / 227 / 233 / 240 | 坐标换算与命中测试，详见 4.4 节 |
| `DrawGrid` / `DrawWires` / `DrawComponents` / `DrawPreview` / `OnPaint` | 274 / 296 / 316 / 328 / 349 | 绘制网格、按电平着色的导线、元件 0/1 徽标、预览和选中框 |
| `OnLeftDown` | 368 | **修改**：记录点击候选并捕获鼠标；连线返回 `0` 时保留连线模式、展示错误，不标记脏 |
| `OnLeftUp` | 446 | **修改**：区分点击/拖动，确认同一元件本体、非 Shift 后调用 `ToggleValue`；释放捕获并清理状态 |
| `OnMotion` | 466 | **修改**：保留拖动和平移；用屏幕距离记录拖动，防止不足一个网格时误切换值 |
| `OnMouseWheel` / `OnKeyDown` | 506 / 517 | **修改**：先清除点击候选，再处理缩放或快捷键 |
| `OnMiddleDown` / `OnMiddleUp` | 552 / 560 | **修改前者**：清除点击候选和左键拖动，避免重复捕获；后者结束平移并释放捕获 |
| `OnCaptureLost` | 565 | **新增**：清除左键拖动、移动标记、点击候选和中键平移 |
| `SetDirty` / `RefreshAndNotify` | 573 / 578 | 设置脏标记 / 请求重绘并向父窗口异步发送 `kCanvasChangedId` |

**EDA.cpp**：见 4.2 表。 **Library.cpp**：见 4.3 表。

**头文件内联接口（EDA.h）**

| 函数 | 作用 |
| --- | --- |
| `RotateCW` / `RotationDegrees` | 顺时针旋转枚举递增 / 枚举转换为角度 |
| `Schematic::Components` / `Wires` | 返回元件/导线集合的引用，含只读和可写重载；可写操作不会自动求值 |
| `Schematic::Selection` / `LogicError` | 返回选中元件 ID 集合 / 最近一次求值或拒绝连线的错误 |
| `ShapeLibrary::All` | 返回元件规格表，供侧边栏枚举 |
| `Canvas::Model` / `FilePath` / `IsDirty` | 访问模型 / 当前文件路径 / 是否有未保存修改 |

**回归测试函数（新增，tests/SchematicTests.cpp）**

| 函数 | 作用 |
| --- | --- |
| `Check` / `CHECK` | 记录检查次数，失败时抛出含行号和表达式的错误 |
| `Add` / `Connect` | 通过正式模型接口创建测试元件 / 连线 |
| `TruthTables` | 六种逻辑门的真值表，覆盖反向操作连线和下游先创建的顺序 |
| `PropagationAndToggle` | 多级传播、扇出、INPUT/OUTPUT 切换、删除后的重算及清空 |
| `InvalidConnections` | 非法端点/引脚/方向、重复连接、多源驱动、环路、失败回滚 |
| `Serialization` | 旧文件兼容、中文标签、VALUE 往返、ID 递增及加载失败不覆盖原模型 |
| `Mouse` / `CanvasClicks` | 投递鼠标事件，验证点击、Shift、拖动、引脚连线、取消及捕获丢失 |
| `TestApp::OnInit` / 测试 `main` | 初始化不显示窗口的测试应用；按是否有 `--canvas` 参数选择测试组，输出检查总数 |

程序入口 `MyApp::OnInit`（`Main.cpp`）仍只负责创建并显示 `MainFrame`、设置顶层窗口，本次未变更。

---

## 7. 中文乱码问题与修复（已处理）

### 现象

曾出现状态栏中文显示异常；此次检查还发现 `EDA.cpp`、`Canvas.cpp`、`MainFrame.cpp` 含损坏的编码字节，导致菜单、对话框、提示语及注释不完整。

### 原因

需要同时保证源码字节有效和字符串转换正确。UTF-8 源码配合 MSVC `/utf-8` 时，**宽字符串字面量 `L"..."`** 可以正确编译；但仅增加 `L` 前缀无法恢复已经损坏的源文件内容。

但代码里有几处用了**窄字符串字面量**（没有 `L` 前缀）传给 `wxString::Format`：

```cpp
wxString::Format("正在放置 %s，点击画布放置，Esc 取消", ...)   // ✗ 窄字符串
```

MSVC 会把这种窄字面量按 UTF-8 字节存进程序，而 **wxWidgets 在 Windows 上把窄字符串按当前系统 ANSI 代码页（简体中文是 GBK/CP936）解释**，于是 UTF-8 字节被当成 GBK 解码 → 乱码。

### 修复

此次修复覆盖以下位置，并保持中文控件字符串和格式串使用宽字符串：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `Canvas.cpp` | `StatusText()` 连线、选中和默认提示 | 恢复损坏汉字，默认提示改为与实际操作一致的“依次点击引脚” |
| `MainFrame.cpp` | 元件库、另存为、退出、文件对话框、错误提示、关于、未命名标题 | 恢复损坏汉字和标点，保留 `L"..."` |
| `EDA.cpp` | 选中说明注释、`Deserialize()` 跳过提示 | 恢复中文注释和“有 %d 行无法识别，已跳过。” |
| `EDA.cpp` / `Canvas.cpp` | 新增运算错误和点击调用注释 | 中文错误使用宽字符串，交互接线写中文注释 |

相关源码统一保存为有效 UTF-8。另将 `.vscode/settings.json` 的全局编码、`[c]` 和 `[cpp]` 编码覆盖项及 `terminal.integrated.encoding` 从 `gbk` 改为 `utf8`。
**这很重要**：源码实际是 UTF-8，如果编辑器按 GBK 打开/保存，中文会再次被破坏。

### 写新代码时的规矩

> 在 wxWidgets 里，凡是给 `wxString` / `wxString::Format` / 各种控件传的字符串字面量，**统一加 `L` 前缀**（或 `wxT("...")`）。纯 ASCII 的格式串可以不加，但加了更统一。

---

## 8. 已知问题与可改进点

1. **选中状态存在两处**
   `Component::selected` 与 `Schematic::m_selectedComponents` 需要同步维护；`SelectWire` 与元件选中互斥。新增选中相关功能时注意同时更新两者。

2. **`Schematic::Find` 返回裸指针**
   一旦在持有指针期间增删元件（`std::vector` 可能重新分配），指针就会失效。当前代码都在同一语句内用完即弃，新增逻辑需保持这个习惯。

3. **连线判定较宽松**
   `HitTest` 中引脚命中半径 10px（`Distance2(...) <= 100.0`），元件本体是方形包围盒而非真实图形；密集摆放时可能误选。

4. **标签会写进文件行尾**
   `Serialize` 用空格分隔、标签在行尾；通过模型直接设置标签时，不应包含换行。当前版本没有改名输入框。

5. **打开文件前尚无未保存确认**
   当前 `OnNew` 和 `OnClose` 有未保存提示，`OnOpen` 会直接加载所选文件；打开前应先保存当前图纸。直接覆盖现有保存路径也没有二次确认。

6. **仅支持组合逻辑**
   环路会被拒绝，尚不支持时钟、锁存器或时序仿真。未连接输入按 `0` 处理；OUTPUT 手动反转是临时状态，下次求值会覆盖。

7. **加载警告与失败需区分**
   `Deserialize` 成功时也可能通过 `error` 返回跳过行提示；当前主窗口只展示失败弹窗。外部调用者应检查返回值并自行决定如何展示成功时的警告。

---

## 9. 术语对照

| 代码 | 中文界面 |
| --- | --- |
| `Component` | 元件 |
| `Pin` | 引脚 |
| `Wire` | 导线 |
| `Schematic` | 原理图 / 图纸 |
| `ShapeLibrary` | 元件库 |
| `Canvas` | 画布 |
| `MainFrame` | 主窗口 |
| Place / Wire / Select 模式 | 放置 / 连线 / 选择 模式 |
| `kCanvasChangedId` | 画布→主窗口的状态通知 ID |
| `Component::value` | 元件的 0/1 设定值或运算结果 |
| `ToggleValue` / `Evaluate` / `LogicError` | 切换值 / 重新计算 / 获取逻辑错误 |

---

## 10. Git 拉取与构建产物

### 10.1 日常拉取

本地 `main` 跟踪 `origin/main`，远端地址为 `git@github.com:jeojon/EDA_JUN.git`。拉取前先查看状态，确认没有未提交的源码改动：

```powershell
git status
git pull
```

如果出现合并冲突，先解决冲突并暂存相关文件，再执行 `git commit` 完成合并；需要放弃本次合并时使用 `git merge --abort`。

### 10.2 不要提交 `build/`

`build/` 是 CMake 生成的编译目录，已经写入 `.gitignore`，并从 Git 索引中移除；本机目录和其中的可执行文件仍然保留。以后不要用 `git add -f build/` 强行提交编译产物。若旧提交中仍跟踪该目录，可用下面的命令停止跟踪而不删除本地文件：

```powershell
git rm -r --cached build
git commit -m "停止跟踪构建产物"
```
