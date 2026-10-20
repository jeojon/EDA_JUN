#include "EDA.h"

#include <algorithm>
#include <cmath>

#include <wx/dcbuffer.h>
#include <wx/file.h>
#include <wx/graphics.h>
#include <wx/menu.h>

namespace eda {

namespace {

double Distance2(const wxPoint& a, const wxPoint& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

double DistanceToSegment2(const wxPoint& p, const wxPoint& a, const wxPoint& b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double length2 = dx * dx + dy * dy;
    if (length2 < 0.001) return Distance2(p, a);

    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / length2;
    t = std::max(0.0, std::min(1.0, t));
    const wxPoint projected(
        static_cast<int>(std::lround(a.x + t * dx)),
        static_cast<int>(std::lround(a.y + t * dy)));
    return Distance2(p, projected);
}

} // namespace

Canvas::Canvas(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(*wxWHITE);

    Bind(wxEVT_PAINT, &Canvas::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &Canvas::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &Canvas::OnLeftUp, this);
    Bind(wxEVT_MOTION, &Canvas::OnMotion, this);
    Bind(wxEVT_MOUSEWHEEL, &Canvas::OnMouseWheel, this);
    Bind(wxEVT_KEY_DOWN, &Canvas::OnKeyDown, this);
    Bind(wxEVT_MIDDLE_DOWN, &Canvas::OnMiddleDown, this);
    Bind(wxEVT_MIDDLE_UP, &Canvas::OnMiddleUp, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &Canvas::OnCaptureLost, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
}

wxString Canvas::StatusText() const {
    if (!m_model.LogicError().IsEmpty()) return m_model.LogicError();
    if (m_mode == Mode::Place) {
        const ComponentSpec* spec = ShapeLibrary::Get().Find(m_placeType);
        return wxString::Format(L"正在放置 %s，点击画布放置，Esc 取消",
                                spec ? spec->displayName : L"元件");
    }
    if (m_mode == Mode::Wire) {
        return m_wireStartComponent ? L"请点击第二个引脚完成连线，Esc 取消"
                                    : L"请点击第一个引脚开始连线";
    }
    if (m_model.SelectedCount() > 0) {
        return wxString::Format(L"已选中 %d 个对象", m_model.SelectedCount());
    }
    return L"选择左侧元件后点击画布放置；依次点击引脚可连线";
}

void Canvas::StartPlace(ComponentType type) {
    m_clickComponent = 0;
    m_dragging = false;
    if (HasCapture() && !m_middlePanning) ReleaseMouse();
    m_mode = Mode::Place;
    m_placeType = type;
    m_wireStartComponent = 0;
    SetFocus();
    RefreshAndNotify();
}

void Canvas::CancelMode() {
    m_mode = Mode::Select;
    m_wireStartComponent = 0;
    m_wireStartPin = -1;
    m_dragging = false;
    m_clickComponent = 0;
    if (HasCapture() && !m_middlePanning) ReleaseMouse();
    RefreshAndNotify();
}

void Canvas::RotateSelected() {
    bool changed = false;
    for (int id : m_model.Selection()) {
        if (Component* c = m_model.Find(id)) {
            c->rotation = RotateCW(c->rotation);
            changed = true;
        }
    }
    if (changed) SetDirty();
}

void Canvas::DeleteSelected() {
    if (m_model.SelectedCount() == 0) return;
    m_model.DeleteSelection();
    SetDirty();
}

void Canvas::SelectAll() {
    m_model.ClearSelection();
    for (const auto& c : m_model.Components()) {
        m_model.SelectComponent(c.id, true);
    }
    RefreshAndNotify();
}

void Canvas::ZoomIn() {
    m_zoom = std::min(4.0, m_zoom * 1.25);
    RefreshAndNotify();
}

void Canvas::ZoomOut() {
    m_zoom = std::max(0.25, m_zoom / 1.25);
    RefreshAndNotify();
}

void Canvas::ZoomReset() {
    m_zoom = 1.0;
    m_originX = 0.0;
    m_originY = 0.0;
    RefreshAndNotify();
}

void Canvas::FitToWindow() {
    if (m_model.Components().empty()) {
        ZoomReset();
        return;
    }

    wxRect box = ShapeLibrary::Get().Bounds(m_model.Components().front());
    for (const auto& c : m_model.Components()) {
        box.Union(ShapeLibrary::Get().Bounds(c));
    }
    box.Inflate(40);

    const wxSize size = GetClientSize();
    const double sx = static_cast<double>(size.x) / std::max(1, box.width);
    const double sy = static_cast<double>(size.y) / std::max(1, box.height);
    m_zoom = std::max(0.25, std::min(4.0, std::min(sx, sy)));
    m_originX = box.x - (size.x / m_zoom - box.width) / 2.0;
    m_originY = box.y - (size.y / m_zoom - box.height) / 2.0;
    RefreshAndNotify();
}

void Canvas::ToggleGrid() {
    m_showGrid = !m_showGrid;
    RefreshAndNotify();
}

void Canvas::NewFile() {
    CancelMode();
    m_model.Clear();
    m_filePath.clear();
    m_dirty = false;
    m_mode = Mode::Select;
    m_wireStartComponent = 0;
    m_originX = 0.0;
    m_originY = 0.0;
    m_zoom = 1.0;
    RefreshAndNotify();
}

bool Canvas::OpenFile(const wxString& path, wxString* error) {
    wxFile file;
    if (!file.Open(path, wxFile::read)) {
        if (error) *error = L"无法打开文件";
        return false;
    }

    const wxFileOffset length = file.Length();
    wxString text;
    if (length > 0) {
        std::vector<char> buffer(static_cast<size_t>(length) + 1, 0);
        const ssize_t count = file.Read(buffer.data(), static_cast<size_t>(length));
        if (count < 0) {
            if (error) *error = L"读取文件失败";
            return false;
        }
        text = wxString::FromUTF8(buffer.data(), static_cast<size_t>(count));
    }

    if (!m_model.Deserialize(text, error)) return false;
    CancelMode();
    m_filePath = path;
    m_dirty = false;
    m_mode = Mode::Select;
    RefreshAndNotify();
    return true;
}

bool Canvas::SaveFile(const wxString& path, wxString* error) {
    wxFile file;
    if (!file.Create(path, true) || !file.IsOpened()) {
        if (error) *error = L"无法写入文件";
        return false;
    }

    const wxScopedCharBuffer data = m_model.Serialize().utf8_str();
    if (file.Write(data.data(), data.length()) != data.length()) {
        if (error) *error = L"写入文件失败";
        return false;
    }

    m_filePath = path;
    m_dirty = false;
    RefreshAndNotify();
    return true;
}

wxPoint Canvas::ScreenToWorld(const wxPoint& screen) const {
    return wxPoint(
        static_cast<int>(std::lround(screen.x / m_zoom + m_originX)),
        static_cast<int>(std::lround(screen.y / m_zoom + m_originY)));
}

wxPoint Canvas::Snap(const wxPoint& world) const {
    return wxPoint(
        static_cast<int>(std::lround(static_cast<double>(world.x) / kGridSize)) * kGridSize,
        static_cast<int>(std::lround(static_cast<double>(world.y) / kGridSize)) * kGridSize);
}

bool Canvas::HitBody(const Component& c, const wxPoint& world) const {
    const int halfW = std::max(c.width, c.height) / 2;
    const int halfH = halfW;
    return std::abs(world.x - c.pos.x) <= halfW &&
           std::abs(world.y - c.pos.y) <= halfH;
}

Canvas::Hit Canvas::HitTest(const wxPoint& world) const {
    Hit hit;

    for (const auto& c : m_model.Components()) {
        for (int i = 0; i < static_cast<int>(c.pins.size()); ++i) {
            if (Distance2(m_model.GetPinPos(c, i), world) <= 100.0) {
                hit.componentId = c.id;
                hit.pinIndex = i;
                return hit;
            }
        }
    }

    for (auto it = m_model.Components().rbegin(); it != m_model.Components().rend(); ++it) {
        if (HitBody(*it, world)) {
            hit.componentId = it->id;
            return hit;
        }
    }

    for (const auto& w : m_model.Wires()) {
        const wxPoint a = m_model.GetPinPos(w.startComponent, w.startPin);
        const wxPoint b = m_model.GetPinPos(w.endComponent, w.endPin);
        const int midX = (a.x + b.x) / 2;
        if (DistanceToSegment2(world, a, wxPoint(midX, a.y)) <= 36.0 ||
            DistanceToSegment2(world, wxPoint(midX, a.y), wxPoint(midX, b.y)) <= 36.0 ||
            DistanceToSegment2(world, wxPoint(midX, b.y), b) <= 36.0) {
            hit.wireId = w.id;
            return hit;
        }
    }
    return hit;
}

void Canvas::DrawGrid(wxGraphicsContext* gc, const wxSize& size) const {
    if (!gc || !m_showGrid) return;

    const double left = m_originX;
    const double top = m_originY;
    const double right = m_originX + size.x / m_zoom;
    const double bottom = m_originY + size.y / m_zoom;

    int step = kGridSize;
    while (step * m_zoom < 8.0) step *= 5;

    gc->SetPen(wxPen(wxColour(225, 230, 238), 1));
    const int x0 = static_cast<int>(std::floor(left / step)) * step;
    const int y0 = static_cast<int>(std::floor(top / step)) * step;
    for (int x = x0; x <= static_cast<int>(right); x += step) {
        gc->StrokeLine(x, top, x, bottom);
    }
    for (int y = y0; y <= static_cast<int>(bottom); y += step) {
        gc->StrokeLine(left, y, right, y);
    }
}

void Canvas::DrawWires(wxGraphicsContext* gc) const {
    for (const auto& w : m_model.Wires()) {
        const wxPoint a = m_model.GetPinPos(w.startComponent, w.startPin);
        const wxPoint b = m_model.GetPinPos(w.endComponent, w.endPin);
        const int midX = (a.x + b.x) / 2;
        const bool selected = m_model.IsSelected(w.id);

        gc->SetPen(wxPen(selected ? wxColour(220, 100, 20) : wxColour(30, 90, 180),
                         selected ? 3 : 2));
        gc->StrokeLine(a.x, a.y, midX, a.y);
        gc->StrokeLine(midX, a.y, midX, b.y);
        gc->StrokeLine(midX, b.y, b.x, b.y);

        gc->SetBrush(wxBrush(selected ? wxColour(220, 100, 20)
                                      : wxColour(30, 90, 180)));
        gc->DrawEllipse(a.x - 3, a.y - 3, 6, 6);
        gc->DrawEllipse(b.x - 3, b.y - 3, 6, 6);
    }
}

void Canvas::DrawComponents(wxGraphicsContext* gc) const {
    ShapeLibrary& library = ShapeLibrary::Get();
    for (const auto& c : m_model.Components()) {
        library.Draw(gc, c);
        if (c.selected) {
            gc->SetPen(wxPen(wxColour(255, 150, 0), 1, wxPENSTYLE_DOT));
            const wxRect box = library.Bounds(c);
            gc->DrawRectangle(box.x - 4, box.y - 4, box.width + 8, box.height + 8);
        }
    }
}

void Canvas::DrawPreview(wxGraphicsContext* gc) const {
    if (m_mode == Mode::Place) {
        Component preview;
        if (const ComponentSpec* spec = ShapeLibrary::Get().Find(m_placeType)) {
            preview.type = m_placeType;
            preview.pos = Snap(m_mouseWorld);
            preview.width = spec->width;
            preview.height = spec->height;
            preview.pins = spec->pins;
            preview.label = spec->name;
        }
        gc->BeginLayer(0.5);
        ShapeLibrary::Get().Draw(gc, preview);
        gc->EndLayer();
    } else if (m_wireStartComponent) {
        const wxPoint start = m_model.GetPinPos(m_wireStartComponent, m_wireStartPin);
        gc->SetPen(wxPen(wxColour(40, 150, 80), 2, wxPENSTYLE_SHORT_DASH));
        gc->StrokeLine(start.x, start.y, m_mouseWorld.x, m_mouseWorld.y);
    }
}

void Canvas::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(*wxWHITE));
    dc.Clear();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;

    gc->PushState();
    gc->Translate(-m_originX, -m_originY);
    gc->Scale(m_zoom, m_zoom);
    DrawGrid(gc, GetClientSize());
    DrawWires(gc);
    DrawComponents(gc);
    DrawPreview(gc);
    gc->PopState();
    delete gc;
}

void Canvas::OnLeftDown(wxMouseEvent& event) {
    SetFocus();
    if (m_middlePanning) return;
    m_clickComponent = 0;
    m_dragging = false;
    const wxPoint world = ScreenToWorld(event.GetPosition());

    if (m_mode == Mode::Place) {
        Component c = ShapeLibrary::Get().Create(m_placeType, Snap(world));
        m_model.AddComponent(c);
        SetDirty();
        return;
    }

    const Hit hit = HitTest(world);

    if (m_mode == Mode::Wire) {
        if (hit.pinIndex >= 0) {
            if (m_wireStartComponent == 0) {
                m_wireStartComponent = hit.componentId;
                m_wireStartPin = hit.pinIndex;
            } else if (m_wireStartComponent != hit.componentId ||
                       m_wireStartPin != hit.pinIndex) {
                Wire wire;
                wire.startComponent = m_wireStartComponent;
                wire.startPin = m_wireStartPin;
                wire.endComponent = hit.componentId;
                wire.endPin = hit.pinIndex;
                if (m_model.AddWire(wire) == 0) {
                    RefreshAndNotify();
                    return;
                }
                m_wireStartComponent = 0;
                m_wireStartPin = -1;
                m_mode = Mode::Select;
                SetDirty();
                return;
            }
        } else {
            CancelMode();
        }
        RefreshAndNotify();
        return;
    }

    if (hit.pinIndex >= 0) {
        m_wireStartComponent = hit.componentId;
        m_wireStartPin = hit.pinIndex;
        m_mode = Mode::Wire;
        RefreshAndNotify();
        return;
    }

    if (hit.componentId) {
        m_model.SelectComponent(hit.componentId, event.ShiftDown());
        m_dragging = true;
        m_dragLast = world;
        m_movedWhileDragging = false;
        // 这里只记录点击候选；等左键释放后确认不是拖动，再调用后端切换值。
        // Shift 用于多选，不参与输入/输出元件的 0/1 切换。
        m_clickScreen = event.GetPosition();
        if (!event.ShiftDown()) m_clickComponent = hit.componentId;
        // 捕获鼠标，确保在画布外松开左键时也能结束拖动并清理点击候选。
        if (!HasCapture()) CaptureMouse();
        RefreshAndNotify();
        return;
    }

    if (hit.wireId) {
        m_model.SelectWire(hit.wireId);
        RefreshAndNotify();
        return;
    }

    m_model.ClearSelection();
    RefreshAndNotify();
}

void Canvas::OnLeftUp(wxMouseEvent& event) {
    // 必须在同一元件本体上按下、释放；引脚点击仍交给原有连线逻辑。
    const Hit hit = HitTest(ScreenToWorld(event.GetPosition()));
    const int clicked = m_clickComponent;
    const bool toggle = m_mode == Mode::Select && m_dragging &&
        !m_middlePanning && !event.ShiftDown() && !m_movedWhileDragging &&
        clicked != 0 && hit.componentId == clicked && hit.pinIndex < 0 &&
        Distance2(event.GetPosition(), m_clickScreen) <= 9.0;
    m_dragging = false;
    m_movedWhileDragging = false;
    m_clickComponent = 0;
    if (HasCapture() && !m_middlePanning) ReleaseMouse();
    if (toggle) {
        // 画布只调用模型接口：INPUT 切换后由后端传播，OUTPUT 临时反转。
        // 普通逻辑门不允许手动切换；只有实际修改成功才标记图纸未保存。
        if (m_model.ToggleValue(clicked)) SetDirty();
        else RefreshAndNotify();
    }
}

void Canvas::OnMotion(wxMouseEvent& event) {
    const wxPoint world = ScreenToWorld(event.GetPosition());
    m_mouseWorld = world;

    if (m_middlePanning) {
        m_originX -= world.x - m_dragLast.x;
        m_originY -= world.y - m_dragLast.y;
        m_dragLast = world;
        Refresh(false);
        return;
    }

    if (m_dragging && event.Dragging() && event.LeftIsDown()) {
        // 使用屏幕像素区分点击与拖动；即使移动不足一个网格也不能误切换。
        if (Distance2(event.GetPosition(), m_clickScreen) > 9.0) {
            m_movedWhileDragging = true;
        }
        wxPoint delta(world.x - m_dragLast.x, world.y - m_dragLast.y);
        if (m_snap) {
            delta.x = static_cast<int>(std::lround(static_cast<double>(delta.x) / kGridSize)) * kGridSize;
            delta.y = static_cast<int>(std::lround(static_cast<double>(delta.y) / kGridSize)) * kGridSize;
        }

        if (delta.x || delta.y) {
            for (const auto& c : m_model.Components()) {
                if (c.selected) {
                    Component* selected = m_model.Find(c.id);
                    if (selected) selected->pos += delta;
                }
            }
            m_dragLast = world;
            m_movedWhileDragging = true;
            SetDirty();
        }
        return;
    }

    Refresh(false);
}

void Canvas::OnMouseWheel(wxMouseEvent& event) {
    m_clickComponent = 0;
    const wxPoint screen = event.GetPosition();
    const wxPoint before = ScreenToWorld(screen);
    const double factor = event.GetWheelRotation() > 0 ? 1.1 : 1.0 / 1.1;
    m_zoom = std::max(0.25, std::min(4.0, m_zoom * factor));
    m_originX = before.x - screen.x / m_zoom;
    m_originY = before.y - screen.y / m_zoom;
    RefreshAndNotify();
}

void Canvas::OnKeyDown(wxKeyEvent& event) {
    m_clickComponent = 0;
    switch (event.GetKeyCode()) {
        case WXK_ESCAPE:
            CancelMode();
            return;
        case WXK_DELETE:
            DeleteSelected();
            return;
        case 'R':
            RotateSelected();
            return;
        case WXK_LEFT:
        case WXK_RIGHT:
        case WXK_UP:
        case WXK_DOWN: {
            const int dx = event.GetKeyCode() == WXK_LEFT ? -kGridSize :
                           event.GetKeyCode() == WXK_RIGHT ? kGridSize : 0;
            const int dy = event.GetKeyCode() == WXK_UP ? -kGridSize :
                           event.GetKeyCode() == WXK_DOWN ? kGridSize : 0;
            for (const auto& c : m_model.Components()) {
                if (c.selected) {
                    if (Component* selected = m_model.Find(c.id)) {
                        selected->pos += wxPoint(dx, dy);
                    }
                }
            }
            SetDirty();
            return;
        }
        default:
            event.Skip();
    }
}

void Canvas::OnMiddleDown(wxMouseEvent& event) {
    m_clickComponent = 0;
    m_dragging = false;
    m_middlePanning = true;
    m_dragLast = ScreenToWorld(event.GetPosition());
    if (!HasCapture()) CaptureMouse();
}

void Canvas::OnMiddleUp(wxMouseEvent&) {
    m_middlePanning = false;
    if (HasCapture()) ReleaseMouse();
}

void Canvas::OnCaptureLost(wxMouseCaptureLostEvent&) {
    // 切换窗口等情况可能使鼠标捕获丢失，清理候选以免下次松键误切换。
    m_dragging = false;
    m_movedWhileDragging = false;
    m_clickComponent = 0;
    m_middlePanning = false;
}

void Canvas::SetDirty(bool dirty) {
    m_dirty = dirty;
    RefreshAndNotify();
}

void Canvas::RefreshAndNotify() {
    Refresh(false);
    if (wxWindow* parent = GetParent()) {
        wxCommandEvent event(wxEVT_TEXT, kCanvasChangedId);
        event.SetEventObject(this);
        wxPostEvent(parent, event);
    }
}

} // namespace eda
