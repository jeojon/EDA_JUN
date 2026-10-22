#include "EDA.h"

#include <cmath>
#include <map>

namespace eda {

namespace {

Pin MakePin(const wchar_t* name, int x, int y, bool output = false) {
    Pin p;
    p.name = name;
    p.offset = wxPoint(x, y);
    p.output = output;
    return p;
}

void DrawAnd(wxGraphicsContext* gc, double w, double h) {
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(-w, -h);
    path.AddLineToPoint(-w, h);
    path.AddLineToPoint(0, h);
    path.AddQuadCurveToPoint(w, h, w, 0);
    path.AddQuadCurveToPoint(w, -h, 0, -h);
    path.CloseSubpath();
    gc->StrokePath(path);
}

void DrawOr(wxGraphicsContext* gc, double w, double h) {
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(-w, -h);
    path.AddQuadCurveToPoint(0, -h, w, 0);
    path.AddQuadCurveToPoint(0, h, -w, h);
    path.AddQuadCurveToPoint(-w * 0.35, 0, -w, -h);
    path.CloseSubpath();
    gc->StrokePath(path);
}

void DrawTriangle(wxGraphicsContext* gc, double w, double h) {
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(-w, -h);
    path.AddLineToPoint(-w, h);
    path.AddLineToPoint(w, 0);
    path.CloseSubpath();
    gc->StrokePath(path);
}

void DrawInput(wxGraphicsContext* gc, double w, double h) {
    gc->StrokeLine(-w * 0.75, -h * 0.55, w * 0.75, -h * 0.55);
    gc->StrokeLine(w * 0.75, -h * 0.55, w * 0.75, h * 0.55);
    gc->StrokeLine(w * 0.75, h * 0.55, -w * 0.75, h * 0.55);
    gc->StrokeLine(-w * 0.75, h * 0.55, -w * 0.75, -h * 0.55);
}

void DrawOutput(wxGraphicsContext* gc, double w, double h) {
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(-w * 0.7, -h * 0.65);
    path.AddLineToPoint(w * 0.7, 0);
    path.AddLineToPoint(-w * 0.7, h * 0.65);
    path.CloseSubpath();
    gc->StrokePath(path);
}

void DrawBody(wxGraphicsContext* gc, ComponentType type, double w, double h) {
    switch (type) {
        case ComponentType::AND:
        case ComponentType::NAND:
            DrawAnd(gc, w, h);
            break;
        case ComponentType::OR:
        case ComponentType::NOR:
            DrawOr(gc, w, h);
            break;
        case ComponentType::XOR:
            DrawOr(gc, w, h);
            gc->StrokeLine(-w - 5, -h, -w * 0.35 - 5, 0);
            gc->StrokeLine(-w * 0.35 - 5, 0, -w - 5, h);
            break;
        case ComponentType::NOT:
            DrawTriangle(gc, w, h);
            break;
        case ComponentType::INPUT:
            DrawInput(gc, w, h);
            break;
        case ComponentType::OUTPUT:
            DrawOutput(gc, w, h);
            break;
        case ComponentType::COUNT:
            break;
    }
}

} // namespace

ShapeLibrary& ShapeLibrary::Get() {
    static ShapeLibrary library;
    return library;
}

ShapeLibrary::ShapeLibrary() {
    m_specs = {
        { ComponentType::AND, L"AND", L"与门", 60, 60,
          { MakePin(L"IN1", -50, -20), MakePin(L"IN2", -50, 20), MakePin(L"OUT", 50, 0, true) } },
        { ComponentType::OR, L"OR", L"或门", 60, 60,
          { MakePin(L"IN1", -50, -20), MakePin(L"IN2", -50, 20), MakePin(L"OUT", 50, 0, true) } },
        { ComponentType::NOT, L"NOT", L"非门", 60, 60,
          { MakePin(L"IN", -50, 0), MakePin(L"OUT", 52, 0, true) } },
        { ComponentType::NAND, L"NAND", L"与非门", 60, 60,
          { MakePin(L"IN1", -50, -20), MakePin(L"IN2", -50, 20), MakePin(L"OUT", 52, 0, true) } },
        { ComponentType::NOR, L"NOR", L"或非门", 60, 60,
          { MakePin(L"IN1", -50, -20), MakePin(L"IN2", -50, 20), MakePin(L"OUT", 52, 0, true) } },
        { ComponentType::XOR, L"XOR", L"异或门", 60, 60,
          { MakePin(L"IN1", -50, -20), MakePin(L"IN2", -50, 20), MakePin(L"OUT", 50, 0, true) } },
        { ComponentType::INPUT, L"INPUT", L"输入", 50, 40,
          { MakePin(L"OUT", 35, 0, true) } },
        { ComponentType::OUTPUT, L"OUTPUT", L"输出", 50, 40,
          { MakePin(L"IN", -35, 0) } },
    };
}

const ComponentSpec* ShapeLibrary::Find(ComponentType type) const {
    for (const auto& spec : m_specs) {
        if (spec.type == type) return &spec;
    }
    return nullptr;
}

const ComponentSpec* ShapeLibrary::FindByName(const wxString& name) const {
    for (const auto& spec : m_specs) {
        if (name.IsSameAs(spec.name, false)) return &spec;
    }
    return nullptr;
}

Component ShapeLibrary::Create(ComponentType type, const wxPoint& pos) const {
    static std::map<int, int> counters;

    Component c;
    c.type = type;
    c.pos = pos;

    const ComponentSpec* spec = Find(type);
    if (!spec) return c;

    c.width = spec->width;
    c.height = spec->height;
    c.pins = spec->pins;
    c.label = wxString::Format("%s%d", spec->name, ++counters[static_cast<int>(type)]);
    return c;
}

wxRect ShapeLibrary::Bounds(const Component& c) const {
    return wxRect(c.pos.x - c.width / 2, c.pos.y - c.height / 2, c.width, c.height);
}

void ShapeLibrary::Draw(wxGraphicsContext* gc, const Component& c) const {
    if (!gc) return;

    const double w = c.width / 2.0;
    const double h = c.height / 2.0;

    gc->PushState();
    gc->Translate(c.pos.x, c.pos.y);
    gc->Rotate(RotationDegrees(c.rotation) * M_PI / 180.0);

    gc->SetPen(wxPen(*wxBLACK, 2));
    gc->SetBrush(*wxWHITE_BRUSH);
    DrawBody(gc, c.type, w, h);

    if (c.type == ComponentType::NAND || c.type == ComponentType::NOR ||
        c.type == ComponentType::NOT) {
        gc->SetBrush(*wxWHITE_BRUSH);
        gc->DrawEllipse(w * 0.78, -h * 0.22, h * 0.44, h * 0.44);
    }
    gc->PopState();

    for (int i = 0; i < static_cast<int>(c.pins.size()); ++i) {
        const Pin& pin = c.pins[i];

        // 旋转后的引脚位置
        int rx = pin.offset.x;
        int ry = pin.offset.y;
        switch (c.rotation) {
            case Rotation::Rot0: break;
            case Rotation::Rot90: rx = -pin.offset.y; ry = pin.offset.x; break;
            case Rotation::Rot180: rx = -pin.offset.x; ry = -pin.offset.y; break;
            case Rotation::Rot270: rx = pin.offset.y; ry = -pin.offset.x; break;
        }
        const wxPoint world(c.pos.x + rx, c.pos.y + ry);
        const wxPoint inner(c.pos.x + rx - (rx > 0 ? 10 : rx < 0 ? -10 : 0),
                            c.pos.y + ry - (ry > 0 ? 10 : ry < 0 ? -10 : 0));

        gc->SetPen(wxPen(wxColour(40, 80, 150), 2));
        gc->StrokeLine(world.x, world.y, inner.x, inner.y);
        gc->SetBrush(wxBrush(wxColour(40, 80, 150)));
        gc->DrawEllipse(world.x - 3, world.y - 3, 6, 6);
    }

    gc->SetFont(wxFontInfo(9), *wxBLACK);
    wxDouble textWidth = 0;
    wxDouble textHeight = 0;
    gc->GetTextExtent(c.label, &textWidth, &textHeight);
    gc->DrawText(c.label,
                 c.pos.x - textWidth / 2,
                 c.pos.y - c.height / 2 - textHeight - 4);
}

} // namespace eda
