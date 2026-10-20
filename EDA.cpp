#include "EDA.h"

#include <algorithm>
#include <limits>
#include <set>

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
    m_logicError.clear();
}

int Schematic::AddComponent(const Component& c) {
    Component copy = c;
    copy.id = m_nextComponentId++;
    m_components.push_back(copy);
    Evaluate();
    return copy.id;
}

int Schematic::AddWire(const Wire& w) {
    Wire copy = w;
    copy.id = m_nextWireId;
    m_wires.push_back(copy);
    // 校验和求值成功才接受连线；失败时保留原电路及其计算结果。
    if (!Evaluate()) {
        m_wires.pop_back();
        return 0;
    }
    ++m_nextWireId;
    return copy.id;
}

bool Schematic::RemoveComponent(int id) {
    if (!Find(id)) return false;
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
    Evaluate();
    return true;
}

bool Schematic::RemoveWire(int id) {
    auto it = std::find_if(m_wires.begin(), m_wires.end(),
        [id](const Wire& w) { return w.id == id; });
    if (it == m_wires.end()) return false;

    m_wires.erase(it);
    if (m_selectedWire == id) m_selectedWire = 0;
    Evaluate();
    return true;
}

bool Schematic::ToggleValue(int componentId) {
    Component* c = Find(componentId);
    if (!c || (c->type != ComponentType::INPUT && c->type != ComponentType::OUTPUT)) {
        return false;
    }

    c->value = !c->value;
    // 输出元件允许临时手动反转；下次重算时由上游信号重新赋值。
    if (c->type == ComponentType::INPUT && !Evaluate()) {
        c->value = !c->value;
        return false;
    }
    return true;
}

bool Schematic::Evaluate(wxString* error) {
    m_logicError.clear();
    if (error) error->clear();
    const auto fail = [&](const wxString& message) {
        m_logicError = message;
        if (error) *error = message;
        return false;
    };

    const size_t count = m_components.size();
    std::map<int, size_t> indices;
    std::vector<std::vector<size_t>> sources(count);
    std::vector<std::vector<size_t>> downstream(count);
    std::vector<size_t> pending(count, 0);
    std::set<int> wireIds;
    for (size_t i = 0; i < count; ++i) {
        const Component& c = m_components[i];
        if (c.id <= 0 || !indices.emplace(c.id, i).second) {
            return fail(L"元件编号必须为正数且不能重复。");
        }
        const ComponentSpec* spec = ShapeLibrary::Get().Find(c.type);
        if (!spec || c.pins.size() != spec->pins.size()) {
            return fail(L"元件类型或引脚数量无效。");
        }
        for (size_t pin = 0; pin < c.pins.size(); ++pin) {
            if (c.pins[pin].output != spec->pins[pin].output) {
                return fail(L"元件引脚方向无效。");
            }
        }
        sources[i].assign(c.pins.size(), count);
    }

    for (const Wire& w : m_wires) {
        if (w.id <= 0 || !wireIds.insert(w.id).second) {
            return fail(L"导线编号必须为正数且不能重复。");
        }
        const auto start = indices.find(w.startComponent);
        const auto end = indices.find(w.endComponent);
        if (start == indices.end() || end == indices.end()) {
            return fail(L"导线连接的元件不存在。");
        }
        size_t from = start->second;
        size_t to = end->second;
        int fromPin = w.startPin;
        int toPin = w.endPin;
        if (fromPin < 0 || toPin < 0 ||
            static_cast<size_t>(fromPin) >= m_components[from].pins.size() ||
            static_cast<size_t>(toPin) >= m_components[to].pins.size()) {
            return fail(L"导线的引脚序号无效。");
        }
        const bool startOutput = m_components[from].pins[fromPin].output;
        if (startOutput == m_components[to].pins[toPin].output) {
            return fail(L"导线必须连接一个输出引脚和一个输入引脚。");
        }
        // 用户可从任意一端开始连线，信号方向只由引脚定义决定。
        if (!startOutput) {
            std::swap(from, to);
            std::swap(fromPin, toPin);
        }
        if (sources[to][toPin] != count) {
            return fail(L"同一个输入引脚不能连接多根导线。");
        }
        sources[to][toPin] = from;
        downstream[from].push_back(to);
        ++pending[to];
    }

    std::vector<size_t> ready;
    for (size_t i = 0; i < count; ++i) {
        if (pending[i] == 0) ready.push_back(i);
    }
    std::vector<bool> values(count, false);
    for (size_t next = 0; next < ready.size(); ++next) {
        const size_t i = ready[next];
        const Component& c = m_components[i];
        bool inputs[2] = {false, false};
        size_t input = 0;
        for (size_t pin = 0; pin < c.pins.size(); ++pin) {
            if (!c.pins[pin].output) {
                const size_t source = sources[i][pin];
                inputs[input++] = source != count && values[source];
            }
        }
        const bool a = inputs[0];
        const bool b = inputs[1];
        switch (c.type) {
            case ComponentType::INPUT: values[i] = c.value; break;
            case ComponentType::OUTPUT: values[i] = a; break;
            case ComponentType::AND: values[i] = a && b; break;
            case ComponentType::OR: values[i] = a || b; break;
            case ComponentType::NOT: values[i] = !a; break;
            case ComponentType::NAND: values[i] = !(a && b); break;
            case ComponentType::NOR: values[i] = !(a || b); break;
            case ComponentType::XOR: values[i] = a != b; break;
            case ComponentType::COUNT: return fail(L"元件类型无效。");
        }
        for (size_t target : downstream[i]) {
            if (--pending[target] == 0) ready.push_back(target);
        }
    }
    if (ready.size() != count) {
        return fail(L"电路存在组合逻辑环路，无法计算。");
    }
    // 全图校验成功后统一写回，避免错误电路留下部分更新的结果。
    for (size_t i = 0; i < count; ++i) m_components[i].value = values[i];
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


// 元件选择与导线选择互斥。
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
        if (c.type == ComponentType::INPUT || c.type == ComponentType::OUTPUT) {
            text << wxString::Format(L"VALUE %d %d\n", c.id, c.value ? 1 : 0);
        }
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
    if (error) error->clear();
    Schematic result;
    std::map<int, bool> storedValues;
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
            if (!parts.GetNextToken().ToLong(&id) || id <= 0 ||
                id >= std::numeric_limits<int>::max()) {
                ++badLines;
                continue;
            }

            const wxString typeName = parts.GetNextToken();
            if (!parts.GetNextToken().ToLong(&x) ||
                !parts.GetNextToken().ToLong(&y) ||
                !parts.GetNextToken().ToLong(&rotation) || rotation < 0 || rotation > 3) {
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
        } else if (kind == "VALUE") {
            long id = 0;
            long value = 0;
            if (!parts.GetNextToken().ToLong(&id) || id <= 0 ||
                id >= std::numeric_limits<int>::max() ||
                !parts.GetNextToken().ToLong(&value) || (value != 0 && value != 1) ||
                parts.HasMoreTokens() ||
                !storedValues.emplace(static_cast<int>(id), value != 0).second) {
                if (error) *error = L"元件值记录无效，必须为不重复的元件编号和 0/1。";
                return false;
            }
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
                !parts.GetNextToken().ToLong(&endPin) || id <= 0 ||
                id >= std::numeric_limits<int>::max()) {
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

    for (const auto& entry : storedValues) {
        Component* c = result.Find(entry.first);
        if (!c || (c->type != ComponentType::INPUT && c->type != ComponentType::OUTPUT)) {
            if (error) *error = L"元件值记录必须引用已有的输入或输出元件。";
            return false;
        }
        c->value = entry.second;
    }
    // 旧文件无 VALUE 记录，输入默认为 0；输出始终按当前电路重新计算。
    if (!result.Evaluate(error)) return false;
    result.m_nextComponentId = maxComponentId + 1;
    result.m_nextWireId = maxWireId + 1;
    *this = std::move(result);

    if (error && badLines > 0) {
        *error = wxString::Format(L"有 %d 行无法识别，已跳过。", badLines);
    }
    return true;
}

} // namespace eda
