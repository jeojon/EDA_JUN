#include "EDA.h"

#include <algorithm>

#include <wx/tokenzr.h>

namespace eda {

namespace {

void RotateOffset(int x, int y, Rotation r, int& outX, int& outY) {
    switch (r) {
        case Rotation::Rot0:
            outX = x;
            outY = y;
            break;
        case Rotation::Rot90:
            outX = -y;
            outY = x;
            break;
        case Rotation::Rot180:
            outX = -x;
            outY = -y;
            break;
        case Rotation::Rot270:
            outX = y;
            outY = -x;
            break;
    }
}

} // namespace

void Schematic::Clear() {
    m_components.clear();
    m_wires.clear();
    m_selectedComponents.clear();
    m_selectedWire = 0;
    m_nextComponentId = 1;
    m_nextWireId = 1;
}

int Schematic::AddComponent(const Component& c) {
    Component copy = c;
    copy.id = m_nextComponentId++;
    m_components.push_back(copy);
    return copy.id;
}

int Schematic::AddWire(const Wire& w) {
    Wire copy = w;
    copy.id = m_nextWireId++;
    m_wires.push_back(copy);
    return copy.id;
}

bool Schematic::RemoveComponent(int id) {
    m_wires.erase(std::remove_if(m_wires.begin(), m_wires.end(),
        [id](const Wire& w) {
            return w.startComponent == id || w.endComponent == id;
        }), m_wires.end());

    auto it = std::find_if(m_components.begin(), m_components.end(),
        [id](const Component& c) { return c.id == id; });
    if (it == m_components.end()) return false;

    m_components.erase(it);
    m_selectedComponents.erase(
        std::remove(m_selectedComponents.begin(), m_selectedComponents.end(), id),
        m_selectedComponents.end());
    return true;
}

bool Schematic::RemoveWire(int id) {
    auto it = std::find_if(m_wires.begin(), m_wires.end(),
        [id](const Wire& w) { return w.id == id; });
    if (it == m_wires.end()) return false;

    m_wires.erase(it);
    if (m_selectedWire == id) m_selectedWire = 0;
    return true;
}

Component* Schematic::Find(int id) {
    for (auto& c : m_components) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

const Component* Schematic::Find(int id) const {
    for (const auto& c : m_components) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

Wire* Schematic::FindWire(int id) {
    for (auto& w : m_wires) {
        if (w.id == id) return &w;
    }
    return nullptr;
}

void Schematic::ClearSelection() {
    for (auto& c : m_components) c.selected = false;
    m_selectedComponents.clear();
    m_selectedWire = 0;
}

void Schematic::SelectComponent(int id, bool add) {
    if (!add) ClearSelection();
    Component* c = Find(id);
    if (!c) return;

    c->selected = true;
    if (std::find(m_selectedComponents.begin(), m_selectedComponents.end(), id) ==
        m_selectedComponents.end()) {
        m_selectedComponents.push_back(id);
    }
}

void Schematic::SelectWire(int id) {
    ClearSelection();
    m_selectedWire = id;
}

bool Schematic::IsSelected(int id) const {
    return id == m_selectedWire ||
           std::find(m_selectedComponents.begin(), m_selectedComponents.end(), id) !=
               m_selectedComponents.end();
}

int Schematic::SelectedCount() const {
    return static_cast<int>(m_selectedComponents.size()) + (m_selectedWire ? 1 : 0);
}

void Schematic::DeleteSelection() {
    const std::vector<int> ids = m_selectedComponents;
    for (int id : ids) RemoveComponent(id);
    if (m_selectedWire) RemoveWire(m_selectedWire);
    ClearSelection();
}

wxPoint Schematic::GetPinPos(const Component& c, int pinIndex) const {
    if (pinIndex < 0 || pinIndex >= static_cast<int>(c.pins.size())) return c.pos;

    const Pin& p = c.pins[pinIndex];
    int rx = 0;
    int ry = 0;
    RotateOffset(p.offset.x, p.offset.y, c.rotation, rx, ry);
    return wxPoint(c.pos.x + rx, c.pos.y + ry);
}

wxPoint Schematic::GetPinPos(int componentId, int pinIndex) const {
    const Component* c = Find(componentId);
    return c ? GetPinPos(*c, pinIndex) : wxPoint();
}

wxString Schematic::Serialize() const {
    wxString text;
    text << "EDA 1\n";

    for (const auto& c : m_components) {
        text << wxString::Format("COMP %d %s %d %d %d ",
            c.id,
            ShapeLibrary::Get().Find(c.type)->name,
            c.pos.x,
            c.pos.y,
            static_cast<int>(c.rotation));
        text << c.label << "\n";
    }

    for (const auto& w : m_wires) {
        text << wxString::Format("WIRE %d %d %d %d %d\n",
            w.id,
            w.startComponent,
            w.startPin,
            w.endComponent,
            w.endPin);
    }
    return text;
}

bool Schematic::Deserialize(const wxString& text, wxString* error) {
    Schematic result;
    int maxComponentId = 0;
    int maxWireId = 0;
    int badLines = 0;

    wxStringTokenizer lines(text, "\n");
    while (lines.HasMoreTokens()) {
        wxString line = lines.GetNextToken();
        line.Trim(true).Trim(false);
        if (line.IsEmpty() || line.StartsWith("#")) continue;
        if (line.StartsWith("EDA ")) continue;

        wxStringTokenizer parts(line, " \t");
        const wxString kind = parts.GetNextToken().Upper();

        if (kind == "COMP") {
            long id = 0;
            long x = 0;
            long y = 0;
            long rotation = 0;
            if (!parts.GetNextToken().ToLong(&id)) {
                ++badLines;
                continue;
            }

            const wxString typeName = parts.GetNextToken();
            if (!parts.GetNextToken().ToLong(&x) ||
                !parts.GetNextToken().ToLong(&y) ||
                !parts.GetNextToken().ToLong(&rotation)) {
                ++badLines;
                continue;
            }

            const ComponentSpec* spec = ShapeLibrary::Get().FindByName(typeName);
            if (!spec) {
                ++badLines;
                continue;
            }

            Component c = ShapeLibrary::Get().Create(
                spec->type, wxPoint(static_cast<int>(x), static_cast<int>(y)));
            c.id = static_cast<int>(id);
            c.rotation = static_cast<Rotation>(rotation % 4);
            c.label = parts.GetString();
            if (c.label.IsEmpty()) c.label = spec->name;
            result.m_components.push_back(c);
            maxComponentId = std::max(maxComponentId, c.id);
        } else if (kind == "WIRE") {
            long id = 0;
            long start = 0;
            long startPin = 0;
            long end = 0;
            long endPin = 0;
            if (!parts.GetNextToken().ToLong(&id) ||
                !parts.GetNextToken().ToLong(&start) ||
                !parts.GetNextToken().ToLong(&startPin) ||
                !parts.GetNextToken().ToLong(&end) ||
                !parts.GetNextToken().ToLong(&endPin)) {
                ++badLines;
                continue;
            }

            Wire w;
            w.id = static_cast<int>(id);
            w.startComponent = static_cast<int>(start);
            w.startPin = static_cast<int>(startPin);
            w.endComponent = static_cast<int>(end);
            w.endPin = static_cast<int>(endPin);
            result.m_wires.push_back(w);
            maxWireId = std::max(maxWireId, w.id);
        } else {
            ++badLines;
        }
    }

    result.m_nextComponentId = maxComponentId + 1;
    result.m_nextWireId = maxWireId + 1;
    *this = std::move(result);

    if (error && badLines > 0) {
<<<<<<< HEAD
        *error = wxString::Format("有 %d 行无法识别，已跳过。", badLines);
=======
        *error = wxString::Format(L"有 %d 行无法识别，已跳过。", badLines);
>>>>>>> faaed4b (wan_first)
    }
    return true;
}

} // namespace eda
