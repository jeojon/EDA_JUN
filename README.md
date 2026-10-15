# 电路原理图编辑器（EDA）— 代码结构与逻辑导读

一个用 **wxWidgets + C++17** 写的逻辑电路原理图编辑器：左侧"元件库"选元件，右侧画布放元件、连线、拖动、缩放，图纸可保存/打开。

本文档说明**每块逻辑写在哪个文件、哪一行**，以及它们之间怎么串起来。
行号是当前版本的参考值，改动代码后可能偏移，定位请以**函数名**为准。

---

## 1. 文件职责（代码地图）

| 文件 | 角色 | 主要内容 |
| --- | --- | --- |
| `Main.cpp` / `Main.h` | 程序入口 | `MyApp::OnInit()` 创建并显示 `MainFrame`，`wxIMPLEMENT_APP` 生成 `main()` |
| `EDA.h` | 全局声明 | 所有结构体、枚举、类声明（数据模型 + 元件库 + 画布 + 主窗口） |
| `EDA.cpp` | **后端数据层** | `Schematic` 的增删改查、选中管理、引脚坐标计算、文件序列化/反序列化 |
| `Library.cpp` | **元件库层** | 8 种元件的规格表、几何图形绘制、按类型创建元件 |
| `Canvas.cpp` | **画布层（UI + 交互）** | 绘制、鼠标键盘事件、放置/选择/拖动/连线、缩放平移、打开保存 |
| `MainFrame.cpp` | **主窗口层（UI）** | 菜单、侧边栏、状态栏、命令响应、界面与模型同步 |
| `CMakeLists.txt` | 构建脚本 | 找 wxWidgets、设 `/utf-8`、生成 `EDA.exe` |

一句话分层：

```
Main.cpp  →  MainFrame（窗口/菜单/侧边栏）  →  Canvas（画布交互与绘制）  →  Schematic（纯数据）
                                                     ↘  ShapeLibrary（元件形状库）
```

`Schematic` 和 `ShapeLibrary` 完全不知道 UI 的存在，可以单独测试；`Canvas` 负责把两者接到界面上。

---

## 2. 构建与运行

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\Debug\EDA.exe
```

构建要点（见 `CMakeLists.txt`）：

- 需要 wxWidgets（本机为 `D:/wxWeigets/wxWidgets-3.3.3`，也可用环境变量 `WXWIN` 指定）。
- 使用 **Debug Unicode 静态库**（`wxmsw33ud_core` + `wxbase33ud`），所以程序带 wxWidgets 的断言检查。
- MSVC 编译选项含 `/utf-8`：**源码按 UTF-8 读取**，这决定了中文字符串的正确写法（见第 9 节）。

---

## 3. 界面（UI）部分

### 3.1 主窗口布局 —— `MainFrame::MainFrame`（`MainFrame.cpp:25`）

| 行 | 代码 | 作用 |
| --- | --- | --- |
| 27 | `m_canvas = new Canvas(this);` | 创建右侧画布（先创建，作为 Frame 的第一个子窗口） |
| 29 | `new wxPanel(this)` | 左侧侧边栏容器 |
| 30 | `new wxStaticText(..., L"元件库")` | 侧边栏标题 |
| 31-37 | `m_palette = new wxListBox(...)` | **元件库列表**：第 0 项固定为"选择 / 移动"，随后追加 `ShapeLibrary` 里的 8 种元件；默认选中第 0 项 |
| 39-48 | `sidebarSizer` | 侧边栏纵向布局（标题 / 列表 / "元件名称" / 输入框） |
| 44-47 | `m_labelEdit = new wxTextCtrl(...)` | **"元件名称"输入框**，改动后实时改选中元件的标签 |
| 50-53 | `sizer` | 主布局：左侧边栏（固定宽）+ 右画布（拉伸填充） |
| 55-83 | 菜单栏 | 文件 / 编辑 / 视图 / 帮助 四个菜单 |
| 85-87 | `CreateStatusBar(2)` | 状态栏分两栏：左为提示文本，右为"元件 N 导线 M" |
| 89-105 | `Bind(...)` | **所有事件绑定的集中位置**（见下表） |
| 107-109 | `SetSize / Centre / SyncUi` | 窗口尺寸、居中、首次刷新界面 |

### 3.2 菜单与命令 ID（`MainFrame.cpp:11-21`）

自定义 ID 从 `wxID_HIGHEST + 1` 开始，避免和 wx 标准 ID 冲突：

```
ID_SAVE(6000)  ID_SAVE_AS(6001)  ID_DELETE(6002)  ID_ROTATE(6003)
ID_ZOOM_IN(6004)  ID_ZOOM_OUT(6005)  ID_ZOOM_RESET(6006)  ID_FIT(6007)  ID_GRID(6008)
```

标准 ID：`wxID_NEW`（新建）、`wxID_OPEN`（打开）、`wxID_EXIT`（退出）、`wxID_ABOUT`（关于）。

### 3.3 事件绑定表（`MainFrame.cpp:89-105`）

| 事件 | 处理函数 | 说明 |
| --- | --- | --- |
| `wxEVT_MENU` + 各 ID | `OnNew` / `OnOpen` / `OnSave` / `OnSaveAs` / `OnExit` / `OnDelete` / `OnRotate` / `OnZoom*` / `OnFit` / `OnToggleGrid` / `OnAbout` | 菜单命令，基本都是转发给 `m_canvas` |
| `wxEVT_LISTBOX`（`m_palette`） | `OnPalette`（`:209`） | **在元件库里选元件** → 进入放置模式 |
| `wxEVT_TEXT`（`m_labelEdit`） | `OnLabelChanged`（`:218`） | 改"元件名称"输入框 → 改选中元件标签 |
| `wxEVT_TEXT` + `kCanvasChangedId` | `OnCanvasChanged`（`:222`） | 画布发来的"状态变了"通知 → `SyncUi()` |
| `wxEVT_CLOSE_WINDOW` | `OnClose`（`:226`） | 关窗时询问是否保存 |

### 3.4 画布窗口 —— `Canvas::Canvas`（`Canvas.cpp:38`）

| 行 | 事件 | 处理函数 | 作用 |
| --- | --- | --- | --- |
| 44 | `wxEVT_PAINT` | `OnPaint`（`:371`） | 全部绘制入口（网格 → 导线 → 元件 → 预览） |
| 45 | `wxEVT_LEFT_DOWN` | `OnLeftDown`（`:390`） | **核心交互**：放置 / 选择 / 开始连线 |
| 46 | `wxEVT_LEFT_UP` | `OnLeftUp`（`:456`） | 结束拖动 |
| 47 | `wxEVT_MOTION` | `OnMotion`（`:461`） | 拖动元件 / 中键平移 / 刷新预览 |
| 48 | `wxEVT_MOUSEWHEEL` | `OnMouseWheel`（`:497`） | 以鼠标为中心缩放 |
| 49 | `wxEVT_KEY_DOWN` | `OnKeyDown`（`:507`） | Esc 取消、Del 删除、R 旋转、方向键微调 |
| 50-51 | `wxEVT_MIDDLE_DOWN/UP` | `OnMiddleDown/Up`（`:541`,`:547`） | 中键拖拽平移画布 |
| 52 | `wxEVT_MOUSE_CAPTURE_LOST` | `OnCaptureLost`（`:552`） | 鼠标捕获丢失时复位状态 |
| 53 | `wxEVT_ERASE_BACKGROUND` | 空 lambda | 关掉闪烁，配合 `wxBG_STYLE_PAINT` + 双缓冲 |

---

## 4. 后端逻辑（数据与算法）

### 4.1 数据结构（`EDA.h`）

| 类型 | 行 | 字段要点 |
| --- | --- | --- |
| `ComponentType` | 14 | 8 种元件：AND/OR/NOT/NAND/NOR/XOR/INPUT/OUTPUT（`COUNT` 为计数哨兵） |
| `Rotation` | 26 | 0/90/180/270 度；`RotateCW`（33）、`RotationDegrees`（37） |
| `Pin` | 41 | `name`、`offset`（相对元件中心的**未旋转**偏移）、`output` 是否输出脚 |
| `Component` | 47 | `id`、`type`、`pos`（中心点，世界坐标）、`rotation`、`label`、`width/height`、`pins`、`selected` |
| `Wire` | 59 | 只记录"从哪个元件的第几个引脚连到哪个元件的第几个引脚"，不存坐标 |
| `ComponentSpec` | 107 | 元件"模板"：类型、英文名、中文名、尺寸、引脚表 |
| `Schematic` | 67 | 图纸数据：元件数组 + 导线数组 + 选中集合 + 自增 ID |

> **设计要点**：导线不存坐标，靠 `GetPinPos()` 实时算，所以移动元件时导线自动跟随。

### 4.2 图纸数据操作 —— `EDA.cpp`

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `Schematic::Clear` | 34 | 清空图纸与选中，ID 归 1 |
| `AddComponent` | 43 | 追加元件并分配新 `id` |
| `AddWire` | 50 | 追加导线并分配新 `id` |
| `RemoveComponent` | 57 | 删元件，**同时删掉连在它身上的所有导线**，并清理选中 |
| `RemoveWire` | 74 | 删单根导线 |
| `Find` / `FindWire` | 84 / 91 / 98 | 按 ID 查对象（返回裸指针，调用方保证不长期持有） |
| `ClearSelection` | 105 | 清空所有选中标记 |
| `SelectComponent` | 111 | 选中（`add=false` 时先清空，`add=true` 为 Shift 多选） |
| `SelectWire` | 123 | 选中导线（与元件互斥） |
| `IsSelected` | 128 | 判断元件或导线是否选中（供绘制高亮用） |
| `SelectedCount` | 134 | 选中对象总数（元件数 + 是否有导线） |
| `DeleteSelection` | 138 | 先拷贝 ID 再删除，避免边遍历边改容器 |
| `GetPinPos` | 145 / 155 | 引脚世界坐标 = 元件中心 + **旋转后**的引脚偏移（`RotateOffset`，行 11） |
| `Serialize` | 160 | 导出成文本 |
| `Deserialize` | 185 | 解析文本，遇错行跳过并累计 `badLines` |

### 4.3 元件形状库 —— `Library.cpp`

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `DrawAnd` / `DrawOr` / `DrawTriangle` / `DrawInput` / `DrawOutput` | 18 / 29 / 39 / 48 / 55 | 各元件**以原点为中心的几何路径**（半宽 `w`、半高 `h`） |
| `DrawBody` | 64 | 按类型分发到上面的绘制函数；NAND/NOR/NOT 额外画输出小圆（`:174` 附近） |
| `ShapeLibrary::Get` | 95 | 单例（函数内 `static`），全局唯一元件库 |
| `ShapeLibrary::ShapeLibrary` | 100 | **规格表**：8 种元件的名称、尺寸、引脚偏移 |
| `Find` / `FindByName` | 121 / 128 | 按类型 / 按名字查规格（反序列化时用名字查回类型） |
| `Create` | 135 | 用规格生成一个元件实例，并自动编标签（`AND1`、`OR2`…，计数器按类型分开） |
| `Bounds` | 152 | 元件的矩形包围盒（用于选择框、`FitToWindow`） |
| `Draw` | 156 | **画一个元件**：平移+旋转坐标系 → 画本体 → 画引脚短线和小圆 → 画标签 |

### 4.4 视图与坐标（`Canvas.cpp`）

| 函数 | 行 | 说明 |
| --- | --- | --- |
| `ScreenToWorld` | 243 | 屏幕坐标 → 世界坐标：`world = screen / zoom + origin` |
| `Snap` | 249 | 吸附到 20px 网格（`kGridSize`，`EDA.h:11`） |
| `HitBody` | 255 | 本体命中判定（方形包围盒） |
| `HitTest` | 262 | **命中测试总入口**，优先级：引脚（半径 10px）→ 元件本体（从后往前，后画的在上层）→ 导线（按折线段距离 ≤6px） |
| `DrawGrid` | 296 | 画网格；缩得太小时自动按 5 倍步长抽稀 |
| `DrawWires` | 318 | 画"Z 字形"折线导线（先水平→垂直→水平），选中时变橙色加粗 |
| `DrawComponents` | 338 | 画所有元件，选中的画橙色虚线选择框 |
| `DrawPreview` | 350 | 放置模式下的**半透明预览**；连线模式下的橡皮筋虚线 |
| `OnPaint` | 371 | 双缓冲绘制；整体变换 `Translate(-origin) + Scale(zoom)` |

> 坐标系：`pos` 是**世界坐标**；屏幕像素 = `(world - origin) * zoom`。缩放围绕鼠标位置进行（`OnMouseWheel`）。

### 4.5 文件读写 —— `EDA.cpp:160-269` + `Canvas.cpp:179/222`

文本格式（UTF-8 保存；当前另存为对话框默认文件名 `未命名.txt`，过滤器为"文本文件 (*.txt)"；格式本身与扩展名无关，改回 `.eda` 也能直接用）：

```
EDA 1
COMP <id> <类型名> <x> <y> <旋转0-3> <标签>
WIRE <id> <起始元件id> <起始引脚序号> <结束元件id> <结束引脚序号>
```

- `Serialize()` 逐行拼字符串；标签放在行尾，因此标签里**不要带换行**。
- `Deserialize()` 逐行解析，`#` 开头视为注释，无法识别的行计入 `badLines` 并通过 `error` 返回提示；元件 ID 与导线 ID 会接着文件里的最大值继续递增。
- `Canvas::OpenFile`（`:179`）读文件 → `wxString::FromUTF8` 解码 → 交给 `Deserialize`。
- `Canvas::SaveFile`（`:222`）`Serialize()` → `utf8_str()` 写出。

---

## 5. 交互流程（一次点击到底发生了什么）

### 5.1 在元件库里选元件

```
点击列表项 → wxEVT_LISTBOX → MainFrame::OnPalette (MainFrame.cpp:209)
    index == 0 → Canvas::CancelMode()            // 回到"选择/移动"
    index >= 1 → Canvas::StartPlace(type)        // 进入放置模式（index-1 对应 ComponentType）
                     → SetFocus() + RefreshAndNotify()
                     → 状态栏提示"正在放置 XX，点击画布放置，Esc 取消"
```

### 5.2 在画布上放置元件

```
左键按下 → Canvas::OnLeftDown (Canvas.cpp:390)
    m_mode == Place → ScreenToWorld → Snap → ShapeLibrary::Create → Schematic::AddComponent
                     → SetDirty() → RefreshAndNotify()
放置模式不会自动退出：可以连续点击放多个；点"选择 / 移动"或按 Esc 才退出
```

### 5.3 选择 / 拖动元件

```
左键按下 → HitTest
    命中引脚 → 进入连线模式（记下起点元件+引脚）
    命中元件 → Schematic::SelectComponent(id, Shift按下?) → m_dragging = true
    命中导线 → Schematic::SelectWire(id)
    空白处   → ClearSelection()
按住拖动 → Canvas::OnMotion (Canvas.cpp:461)
    拖动增量按 20px 网格吸附 → 所有选中元件一起平移 → SetDirty()
```

### 5.4 连线

```
点第一个引脚 → m_wireStartComponent / m_wireStartPin 记录起点，m_mode = Wire
点第二个引脚 → 组成 Wire → Schematic::AddWire → 回到选择模式
点空白处     → CancelMode()
```

### 5.5 界面同步链路（重要）

画布任何"状态变化"都走同一条通知链：

```
Canvas::SetDirty() / StartPlace() / Zoom*() ...
        ↓
Canvas::RefreshAndNotify()  (Canvas.cpp:562)
        ├─ Refresh(false)                          // 请求重绘
        └─ wxPostEvent(父窗口, wxCommandEvent(wxEVT_TEXT, kCanvasChangedId))
                          ↓
MainFrame::OnCanvasChanged (MainFrame.cpp:222) → MainFrame::SyncUi() (MainFrame.cpp:253)
        ├─ 标题：文件名/未命名 + 脏标记 "*"
        ├─ 状态栏左栏：Canvas::StatusText()
        ├─ 状态栏右栏：元件数 / 导线数
        └─ "元件名称"输入框：选中一个元件时启用并填入它的标签
```

> `kCanvasChangedId`（`EDA.h:12`）是画布与主窗口之间的私有事件 ID，避免和菜单 ID 混用。

### 5.6 其他命令

| 操作 | 入口 | 落到哪 |
| --- | --- | --- |
| 删除选中 | 菜单/`Del` | `MainFrame::OnDelete` → `Canvas::DeleteSelected` → `Schematic::DeleteSelection` |
| 旋转 | 菜单/`R` | `Canvas::RotateSelected` → 每个选中元件 `RotateCW` |
| 缩放 | 菜单/滚轮 | `Canvas::ZoomIn/Out/Reset/FitToWindow` |
| 改名 | 输入框 | `OnLabelChanged` → `Canvas::SetSelectedLabel` |
| 新建/打开/保存 | 文件菜单 | `Canvas::NewFile/OpenFile/SaveFile` |

---

## 6. 函数速查表

**MainFrame.cpp**

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `MainFrame::MainFrame` | 25 | 建窗口、侧边栏、菜单、状态栏、绑定事件 |
| `OnNew` / `OnOpen` / `OnSave` / `OnSaveAs` | 112 / 123 / 138 / 147 | 文件命令；未保存时先询问 |
| `SaveTo` | 156 | 调用 `Canvas::SaveFile`，失败弹错误框 |
| `OnExit` | 166 | `Close()` |
| `OnDelete` / `OnRotate` | 170 / 174 | 转发到画布 |
| `OnZoomIn/Out/Reset` / `OnFit` / `OnToggleGrid` | 178 / 182 / 186 / 190 / 194 | 视图命令 |
| `OnAbout` | 203 | 关于对话框 |
| `OnPalette` | 209 | **元件库选择 → 放置模式** |
| `OnLabelChanged` | 218 | 名称输入框 → 改元件标签 |
| `OnCanvasChanged` | 222 | 画布通知 → `SyncUi` |
| `OnClose` | 226 | 关窗前询问保存 |
| `SyncUi` | 253 | 标题 / 状态栏 / 名称框的统一刷新 |

**Canvas.cpp**

| 函数 | 行 | 作用 |
| --- | --- | --- |
| `StatusText` | 56 | 底部提示文字（放置/连线/已选中） |
| `StartPlace` / `CancelMode` | 72 / 80 | 进入放置模式 / 回到选择模式 |
| `RotateSelected` / `DeleteSelected` / `SetSelectedLabel` / `SelectAll` | 88 / 99 / 105 / 116 | 编辑选中对象 |
| `ZoomIn` / `ZoomOut` / `ZoomReset` / `FitToWindow` / `ToggleGrid` | 124 / 129 / 134 / 141 / 162 | 视图控制 |
| `NewFile` / `OpenFile` / `SaveFile` | 167 / 179 / 222 | 文件读写（含脏标记与视图复位） |
| `ScreenToWorld` / `Snap` / `HitBody` / `HitTest` | 243 / 249 / 255 / 262 | 坐标换算与命中测试 |
| `DrawGrid` / `DrawWires` / `DrawComponents` / `DrawPreview` / `OnPaint` | 296 / 318 / 338 / 350 / 371 | 绘制 |
| `OnLeftDown` / `OnLeftUp` / `OnMotion` / `OnMouseWheel` | 390 / 456 / 461 / 497 | 鼠标交互 |
| `OnKeyDown` | 507 | 键盘快捷键 |
| `OnMiddleDown` / `OnMiddleUp` / `OnCaptureLost` | 541 / 547 / 552 | 中键平移与捕获 |
| `SetDirty` / `RefreshAndNotify` | 557 / 562 | 脏标记与"重绘 + 通知主窗口" |

**EDA.cpp**：见 4.2 表。 **Library.cpp**：见 4.3 表。

---

## 7. 中文乱码问题与修复（已处理）

### 现象

菜单、按钮、元件名正常，但**状态栏**里出现类似 `姝ｅ湪鏀剧疆 与门...` 的乱码。

### 原因

源码文件是 UTF-8，编译器也开了 `/utf-8`，所以源码里的**宽字符串字面量 `L"..."`** 会被正确转成 UTF-16，显示正常。

但代码里有几处用了**窄字符串字面量**（没有 `L` 前缀）传给 `wxString::Format`：

```cpp
wxString::Format("正在放置 %s，点击画布放置，Esc 取消", ...)   // ✗ 窄字符串
```

MSVC 会把这种窄字面量按 UTF-8 字节存进程序，而 **wxWidgets 在 Windows 上把窄字符串按当前系统 ANSI 代码页（简体中文是 GBK/CP936）解释**，于是 UTF-8 字节被当成 GBK 解码 → 乱码。

### 修复

把这些中文格式串改成宽字符串（与文件其余部分一致）：

| 文件 | 位置 | 改动 |
| --- | --- | --- |
| `Canvas.cpp` | `StatusText()` 放置提示 / 已选中提示 | `"..."` → `L"..."` |
| `MainFrame.cpp` | `SyncUi()` 状态栏右栏"元件/导线" | `"..."` → `L"..."` |
| `EDA.cpp` | `Deserialize()` 错误提示 | `"..."` → `L"..."` |

另外把 `.vscode/settings.json` 的编码从 `gbk` 改为 `utf8`（`files.encoding` 与 `terminal.integrated.encoding`）。
**这很重要**：源码实际是 UTF-8，如果编辑器按 GBK 打开/保存，中文会再次被破坏。

### 写新代码时的规矩

> 在 wxWidgets 里，凡是给 `wxString` / `wxString::Format` / 各种控件传的字符串字面量，**统一加 `L` 前缀**（或 `wxT("...")`）。纯 ASCII 的格式串可以不加，但加了更统一。

---

## 8. 已知问题与可改进点

1. **`SyncUi()` 的输入框回写有焦点条件**
   `MainFrame.cpp:270` 处 `if (selected && !m_labelEdit->HasFocus())`：只有当输入框没有焦点时才回填标签，避免打字时被覆盖。若将来要让"选中不同元件也强制刷新输入框"，需要区分"用户正在编辑"与"程序赋值"两种状态。

2. **选中状态存在两处**
   `Component::selected` 与 `Schematic::m_selectedComponents` 需要同步维护；`SelectWire` 与元件选中互斥。新增选中相关功能时注意同时更新两者。

3. **`Schematic::Find` 返回裸指针**
   一旦在持有指针期间增删元件（`std::vector` 可能重新分配），指针就会失效。当前代码都在同一语句内用完即弃，新增逻辑需保持这个习惯。

4. **连线判定较宽松**
   `HitTest` 中引脚命中半径 10px（`Distance2(...) <= 100.0`），元件本体是方形包围盒而非真实图形；密集摆放时可能误选。

5. **标签会写进文件行尾**
   `Serialize` 用空格分隔、标签在行尾，因此标签中若包含换行会破坏格式；目前 UI 是单行输入框，暂时不会出现。

6. **未保存提示只覆盖"新建/打开/退出"**
   关闭窗口、新建、打开前都有询问；但直接覆盖保存没有二次确认。

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
