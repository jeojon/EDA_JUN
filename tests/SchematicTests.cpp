#include "EDA.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using namespace eda;
int checks = 0;

void Check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        throw std::runtime_error(std::string("line ") + std::to_string(line) +
                                 ": " + expression);
    }
}

#define CHECK(condition) Check(static_cast<bool>(condition), #condition, __LINE__)

int Add(Schematic& model, ComponentType type, bool value = false,
        wxPoint position = wxPoint()) {
    Component c = ShapeLibrary::Get().Create(type, position);
    c.value = value;
    return model.AddComponent(c);
}

int Connect(Schematic& model, int from, int fromPin, int to, int toPin) {
    Wire w;
    w.startComponent = from;
    w.startPin = fromPin;
    w.endComponent = to;
    w.endPin = toPin;
    return model.AddWire(w);
}

void TruthTables() {
    const ComponentType types[] = {ComponentType::AND, ComponentType::OR,
        ComponentType::NOT, ComponentType::NAND, ComponentType::NOR, ComponentType::XOR};
    // 各行依次对应输入 00、01、10、11；NOT 只使用第一个输入。
    const bool expected[][4] = {{0, 0, 0, 1}, {0, 1, 1, 1}, {1, 1, 0, 0},
                               {1, 1, 1, 0}, {1, 0, 0, 0}, {0, 1, 1, 0}};
    for (size_t type = 0; type < 6; ++type) {
        for (int bits = 0; bits < 4; ++bits) {
            Schematic model;
            // 故意先创建下游，验证计算不依赖元件的插入顺序。
            const int output = Add(model, ComponentType::OUTPUT);
            const int gate = Add(model, types[type]);
            const int a = Add(model, ComponentType::INPUT, (bits & 2) != 0);
            const int b = Add(model, ComponentType::INPUT, (bits & 1) != 0);
            const int outputPin = types[type] == ComponentType::NOT ? 1 : 2;
            CHECK(Connect(model, output, 0, gate, outputPin) != 0);
            CHECK(Connect(model, a, 0, gate, 0) != 0);
            if (types[type] != ComponentType::NOT) {
                CHECK(Connect(model, gate, 1, b, 0) != 0);
            }
            CHECK(model.Find(gate)->value == expected[type][bits]);
            CHECK(model.Find(output)->value == expected[type][bits]);
        }
    }
}

void PropagationAndToggle() {
    Schematic model;
    const int outA = Add(model, ComponentType::OUTPUT);
    const int outB = Add(model, ComponentType::OUTPUT);
    const int inverse = Add(model, ComponentType::NOT);
    const int gate = Add(model, ComponentType::XOR);
    const int a = Add(model, ComponentType::INPUT);
    const int b = Add(model, ComponentType::INPUT);
    CHECK(Connect(model, gate, 2, inverse, 0));
    const int wireA = Connect(model, outA, 0, inverse, 1);
    CHECK(wireA);
    CHECK(Connect(model, inverse, 1, outB, 0));
    CHECK(Connect(model, a, 0, gate, 0));
    CHECK(Connect(model, b, 0, gate, 1));
    CHECK(model.Find(outA)->value && model.Find(outB)->value);
    CHECK(model.ToggleValue(a));
    CHECK(!model.Find(outA)->value && !model.Find(outB)->value);
    CHECK(model.ToggleValue(b));
    CHECK(model.Find(outA)->value && model.Find(outB)->value);
    CHECK(!model.ToggleValue(gate));
    CHECK(!model.ToggleValue(999));

    CHECK(model.ToggleValue(outA));
    CHECK(!model.Find(outA)->value && model.Find(outB)->value);
    CHECK(model.Evaluate());
    CHECK(model.Find(outA)->value);
    CHECK(model.RemoveWire(wireA));
    CHECK(!model.Find(outA)->value && model.Find(outB)->value);
    CHECK(model.RemoveComponent(inverse));
    CHECK(!model.Find(outB)->value);
    CHECK(!model.RemoveComponent(inverse));

    // 同一输出允许扇出到同一逻辑门的两个不同输入引脚。
    const int both = Add(model, ComponentType::AND);
    CHECK(Connect(model, a, 0, both, 0));
    CHECK(Connect(model, a, 0, both, 1));
    CHECK(model.Find(both)->value);
    model.SelectComponent(a);
    model.DeleteSelection();
    CHECK(!model.Find(both)->value);
    CHECK(model.SelectedCount() == 0);

    model.Clear();
    CHECK(model.LogicError().IsEmpty());
    CHECK(model.Evaluate());
    CHECK(Add(model, ComponentType::INPUT) == 1);
}

void InvalidConnections() {
    Schematic model;
    const int input = Add(model, ComponentType::INPUT, true);
    const int other = Add(model, ComponentType::INPUT);
    const int output = Add(model, ComponentType::OUTPUT);
    const int otherOutput = Add(model, ComponentType::OUTPUT);
    const int first = Add(model, ComponentType::NOT);
    const int second = Add(model, ComponentType::NOT);
    CHECK(Connect(model, input, 0, output, 0));
    const auto reject = [&](int from, int fromPin, int to, int toPin) {
        const wxString before = model.Serialize();
        CHECK(Connect(model, from, fromPin, to, toPin) == 0);
        CHECK(model.Serialize() == before);
        CHECK(!model.LogicError().IsEmpty());
        CHECK(model.LogicError().Find(wxUniChar(0xfffd)) == wxNOT_FOUND);
    };
    reject(input, 0, 999, 0);
    reject(input, -1, otherOutput, 0);
    reject(input, 0, otherOutput, 99);
    reject(input, 0, other, 0);
    reject(output, 0, otherOutput, 0);
    reject(other, 0, output, 0);
    reject(input, 0, output, 0);
    reject(first, 1, first, 0);
    CHECK(model.LogicError().Contains(L"环路"));
    CHECK(Connect(model, first, 1, second, 0) == 2);
    reject(second, 1, first, 0);
    CHECK(model.LogicError().Contains(L"环路"));
    CHECK(model.Evaluate());
    CHECK(model.LogicError().IsEmpty());

    // 即使调用方通过可写集合绕过 AddWire，求值失败也不能部分覆盖结果。
    Wire invalid;
    invalid.id = 100;
    invalid.startComponent = second;
    invalid.startPin = 1;
    invalid.endComponent = first;
    invalid.endPin = 0;
    model.Wires().push_back(invalid);
    const wxString before = model.Serialize();
    wxString error;
    CHECK(!model.Evaluate(&error));
    CHECK(error.Contains(L"环路"));
    CHECK(model.Serialize() == before);
    CHECK(!model.ToggleValue(input));
    CHECK(model.Serialize() == before);
    CHECK(model.RemoveWire(100));
    CHECK(model.LogicError().IsEmpty());
}

void Serialization() {
    Schematic model;
    wxString error = L"旧错误";
    const wxString legacy = L"EDA 1\nCOMP 10 INPUT 100 100 0 输入 A\n"
                            L"COMP 20 OUTPUT 300 100 0 输出 B\nWIRE 7 20 0 10 0\n";
    CHECK(model.Deserialize(legacy, &error));
    CHECK(error.IsEmpty());
    CHECK(!model.Find(10)->value && !model.Find(20)->value);
    CHECK(model.ToggleValue(10));
    CHECK(model.Find(20)->value);
    const wxString text = model.Serialize();
    CHECK(text.Contains(L"VALUE 10 1\n"));
    CHECK(text.Contains(L"VALUE 20 1\n"));
    const wxScopedCharBuffer bytes = text.utf8_str();
    Schematic restored;
    CHECK(restored.Deserialize(wxString::FromUTF8(bytes.data(), bytes.length()), &error));
    CHECK(restored.Serialize() == text);
    CHECK(restored.Find(10)->label == L"输入 A");
    CHECK(restored.Find(20)->label == L"输出 B");
    CHECK(Add(restored, ComponentType::OUTPUT) == 21);
    CHECK(Connect(restored, 10, 0, 21, 0) == 8);

    // VALUE 可以出现在元件定义之前，已连接输出的存储值不覆盖上游结果。
    CHECK(restored.Deserialize(L"VALUE 10 1\nVALUE 20 0\n" + legacy, &error));
    CHECK(restored.Find(10)->value && restored.Find(20)->value);
    CHECK(restored.ToggleValue(20));
    CHECK(!restored.Find(20)->value);
    CHECK(restored.Deserialize(restored.Serialize(), &error));
    CHECK(restored.Find(20)->value);

    const wxString before = model.Serialize();
    const wxString badFiles[] = {
        legacy + L"VALUE 10 2\n", legacy + L"VALUE 10 -1\n",
        legacy + L"VALUE 10 1 extra\n", legacy + L"VALUE 999 1\n",
        legacy + L"VALUE 10 1\nVALUE 10 0\n",
        legacy + L"COMP 10 INPUT 0 0 0 duplicate\n",
        legacy + L"WIRE 8 999 0 20 0\n", legacy + L"WIRE 8 10 99 20 0\n",
        legacy + L"WIRE 8 10 0 20 0\n", legacy + L"WIRE 7 10 0 20 0\n",
        L"COMP 1 NOT 0 0 0 A\nCOMP 2 NOT 100 0 0 B\n"
        L"WIRE 1 1 1 2 0\nWIRE 2 2 1 1 0\n",
        L"COMP 1 AND 0 0 0 A\nVALUE 1 1\n"
    };
    for (const auto& bad : badFiles) {
        CHECK(!model.Deserialize(bad, &error));
        CHECK(!error.IsEmpty());
        CHECK(model.Serialize() == before);
    }
    CHECK(model.Deserialize(legacy + L"UNRECOGNIZED\n", &error));
    CHECK(error == L"有 1 行无法识别，已跳过。");
    CHECK(model.Evaluate(&error));
    CHECK(error.IsEmpty());
}

void Mouse(Canvas& canvas, wxEventType type, wxPoint position,
           bool leftDown = false, bool shift = false) {
    wxMouseEvent event(type);
    event.SetPosition(position);
    event.SetLeftDown(leftDown);
    event.SetShiftDown(shift);
    canvas.GetEventHandler()->ProcessEvent(event);
}

void CanvasClicks() {
    // 使用未显示的窗口直接投递事件，验证画布接线，不打开可见测试窗口。
    auto parent = std::make_unique<wxFrame>(nullptr, wxID_ANY, L"测试");
    auto* canvas = new Canvas(parent.get());
    const auto reset = [&]() {
        canvas->NewFile();
        Add(canvas->Model(), ComponentType::INPUT, false, wxPoint(100, 100));
        Add(canvas->Model(), ComponentType::OUTPUT, false, wxPoint(300, 100));
    };
    const auto click = [&](wxPoint point, bool shift = false) {
        Mouse(*canvas, wxEVT_LEFT_DOWN, point, true, shift);
        Mouse(*canvas, wxEVT_LEFT_UP, point, false, shift);
    };
    reset();
    CHECK(Connect(canvas->Model(), 1, 0, 2, 0));
    click(wxPoint(100, 100));
    CHECK(canvas->Model().Find(1)->value && canvas->Model().Find(2)->value);
    CHECK(canvas->IsDirty());
    click(wxPoint(100, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->Model().Find(2)->value);
    click(wxPoint(300, 100));
    CHECK(canvas->Model().Find(2)->value && !canvas->Model().Find(1)->value);

    reset();
    click(wxPoint(100, 100), true);
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true, true);
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(100, 100));
    CHECK(!canvas->Model().Find(1)->value);

    reset();
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true);
    Mouse(*canvas, wxEVT_MOTION, wxPoint(120, 100), true);
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(120, 100));
    CHECK(canvas->Model().Find(1)->pos == wxPoint(120, 100));
    CHECK(!canvas->Model().Find(1)->value && canvas->IsDirty());

    reset();
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true);
    Mouse(*canvas, wxEVT_MOTION, wxPoint(105, 100), true);
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(100, 100));
    CHECK(canvas->Model().Find(1)->pos == wxPoint(100, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true);
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(300, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());

    reset();
    click(wxPoint(135, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());
    click(wxPoint(265, 100));
    CHECK(canvas->Model().Wires().size() == 1 && canvas->IsDirty());
    CHECK(!canvas->Model().Find(1)->value);

    reset();
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true);
    canvas->CancelMode();
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(100, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());
    Mouse(*canvas, wxEVT_LEFT_DOWN, wxPoint(100, 100), true);
    if (canvas->HasCapture()) canvas->ReleaseMouse();
    wxMouseCaptureLostEvent lost(canvas->GetId());
    canvas->GetEventHandler()->ProcessEvent(lost);
    Mouse(*canvas, wxEVT_LEFT_UP, wxPoint(100, 100));
    CHECK(!canvas->Model().Find(1)->value && !canvas->IsDirty());
}

} // namespace

class TestApp : public wxApp {
public:
    bool OnInit() override { return true; }
};

wxIMPLEMENT_APP_NO_MAIN(TestApp);

int main(int argc, char** argv) {
    const bool canvas = argc > 1 && std::string(argv[1]) == "--canvas";
    if (canvas && !wxEntryStart(argc, argv)) {
        std::cerr << "Could not initialize wxWidgets\n";
        return 1;
    }
    int status = 0;
    try {
        if (canvas) {
            CHECK(wxTheApp->CallOnInit());
            CanvasClicks();
        } else {
            TruthTables();
            PropagationAndToggle();
            InvalidConnections();
            Serialization();
        }
        std::cout << "PASS " << (canvas ? "canvas" : "schematic") << ": "
                  << checks << " checks\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        status = 1;
    }
    if (canvas) {
        wxTheApp->OnExit();
        wxEntryCleanup();
    }
    return status;
}
