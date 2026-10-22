#pragma once

#include <wx/wx.h>
#include <wx/graphics.h>

#include <map>
#include <vector>

namespace eda {

constexpr int kGridSize = 20;
constexpr int kCanvasChangedId = wxID_HIGHEST + 350;

inline wxColour LogicHighColour() {
    return wxColour(196, 61, 55);
}

inline wxColour LogicLowColour() {
    return wxColour(74, 104, 136);
}

enum class ComponentType {
    AND,
    OR,
    NOT,
    NAND,
    NOR,
    XOR,
    INPUT,
    OUTPUT,
    COUNT
};

enum class Rotation {
    Rot0,
    Rot90,
    Rot180,
    Rot270
};

inline Rotation RotateCW(Rotation r) {
    return static_cast<Rotation>((static_cast<int>(r) + 1) % 4);
}

inline int RotationDegrees(Rotation r) {
    return static_cast<int>(r) * 90;
}

struct Pin {
    wxString name;
    wxPoint offset;
    bool output = false;
};

struct Component {
    int id = 0;
    ComponentType type = ComponentType::AND;
    wxPoint pos;
    Rotation rotation = Rotation::Rot0;
    wxString label;
    int width = 60;
    int height = 60;
    std::vector<Pin> pins;
    bool selected = false;
    bool value = false;  // 输入元件的设定值，或逻辑门、输出元件的计算结果（0/1）。
};

struct Wire {
    int id = 0;
    int startComponent = 0;
    int startPin = 0;
    int endComponent = 0;
    int endPin = 0;
};

class Schematic {
public:
    const std::vector<Component>& Components() const { return m_components; }
    std::vector<Component>& Components() { return m_components; }

    const std::vector<Wire>& Wires() const { return m_wires; }
    std::vector<Wire>& Wires() { return m_wires; }

    void Clear();
    int AddComponent(const Component& c);
    int AddWire(const Wire& w);
    bool RemoveComponent(int id);
    bool RemoveWire(int id);

    bool ToggleValue(int componentId);
    bool Evaluate(wxString* error = nullptr);
    const wxString& LogicError() const { return m_logicError; }

    Component* Find(int id);
    const Component* Find(int id) const;
    Wire* FindWire(int id);

    void ClearSelection();
    void SelectComponent(int id, bool add = false);
    void SelectWire(int id);
    bool IsSelected(int id) const;
    const std::vector<int>& Selection() const { return m_selectedComponents; }
    int SelectedCount() const;
    void DeleteSelection();

    wxPoint GetPinPos(const Component& c, int pinIndex) const;
    wxPoint GetPinPos(int componentId, int pinIndex) const;
    wxString Serialize() const;
    bool Deserialize(const wxString& text, wxString* error = nullptr);

private:
    std::vector<Component> m_components;
    std::vector<Wire> m_wires;
    std::vector<int> m_selectedComponents;
    int m_selectedWire = 0;
    int m_nextComponentId = 1;
    int m_nextWireId = 1;
    wxString m_logicError;
};

struct ComponentSpec {
    ComponentType type = ComponentType::AND;
    const wchar_t* name = L"AND";
    const wchar_t* displayName = L"与门";
    int width = 60;
    int height = 60;
    std::vector<Pin> pins;
};

class ShapeLibrary {
public:
    static ShapeLibrary& Get();

    const std::vector<ComponentSpec>& All() const { return m_specs; }
    const ComponentSpec* Find(ComponentType type) const;
    const ComponentSpec* FindByName(const wxString& name) const;

    Component Create(ComponentType type, const wxPoint& pos) const;
    void Draw(wxGraphicsContext* gc, const Component& c) const;
    wxRect Bounds(const Component& c) const;

private:
    ShapeLibrary();
    std::vector<ComponentSpec> m_specs;
};

class Canvas : public wxPanel {
public:
    explicit Canvas(wxWindow* parent);

    Schematic& Model() { return m_model; }
    const wxString& FilePath() const { return m_filePath; }
    bool IsDirty() const { return m_dirty; }
    wxString StatusText() const;

    void StartPlace(ComponentType type);
    void CancelMode();
    void RotateSelected();
    void DeleteSelected();
    void SelectAll();
    void ZoomIn();
    void ZoomOut();
    void ZoomReset();
    void FitToWindow();
    void ToggleGrid();

    void NewFile();
    bool OpenFile(const wxString& path, wxString* error = nullptr);
    bool SaveFile(const wxString& path, wxString* error = nullptr);

private:
    enum class Mode {
        Select,
        Place,
        Wire
    };

    struct Hit {
        int componentId = 0;
        int pinIndex = -1;
        int wireId = 0;
    };

    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnMouseWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);
    void OnMiddleDown(wxMouseEvent& event);
    void OnMiddleUp(wxMouseEvent& event);
    void OnCaptureLost(wxMouseCaptureLostEvent& event);

    wxPoint ScreenToWorld(const wxPoint& screen) const;
    wxPoint Snap(const wxPoint& world) const;
    Hit HitTest(const wxPoint& world) const;
    bool HitBody(const Component& c, const wxPoint& world) const;
    void DrawGrid(wxGraphicsContext* gc, const wxSize& size) const;
    void DrawWires(wxGraphicsContext* gc) const;
    void DrawComponents(wxGraphicsContext* gc) const;
    void DrawPreview(wxGraphicsContext* gc) const;
    void RefreshAndNotify();
    void SetDirty(bool dirty = true);

    Schematic m_model;
    wxString m_filePath;
    bool m_dirty = false;
    Mode m_mode = Mode::Select;
    ComponentType m_placeType = ComponentType::AND;

    double m_zoom = 1.0;
    double m_originX = 0.0;
    double m_originY = 0.0;
    bool m_showGrid = true;
    bool m_snap = true;

    wxPoint m_mouseWorld;
    bool m_dragging = false;
    wxPoint m_dragLast;
    bool m_movedWhileDragging = false;
    int m_clickComponent = 0;
    wxPoint m_clickScreen;
    int m_wireStartComponent = 0;
    int m_wireStartPin = -1;
    bool m_middlePanning = false;
};

class MainFrame : public wxFrame {
public:
    explicit MainFrame(wxWindow* parent = nullptr);

private:
    void OnNew(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnSaveAs(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnDelete(wxCommandEvent& event);
    void OnRotate(wxCommandEvent& event);
    void OnZoomIn(wxCommandEvent& event);
    void OnZoomOut(wxCommandEvent& event);
    void OnZoomReset(wxCommandEvent& event);
    void OnFit(wxCommandEvent& event);
    void OnToggleGrid(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnPalette(wxCommandEvent& event);
    void OnCanvasChanged(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);

    bool SaveTo(const wxString& path);
    void SyncUi();

    Canvas* m_canvas = nullptr;
    wxListBox* m_palette = nullptr;
};

} // namespace eda
