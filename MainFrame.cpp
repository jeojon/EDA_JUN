#include "EDA.h"

#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>

namespace eda {

namespace {

enum {
    ID_SAVE = wxID_HIGHEST + 1,
    ID_SAVE_AS,
    ID_DELETE,
    ID_ROTATE,
    ID_ZOOM_IN,
    ID_ZOOM_OUT,
    ID_ZOOM_RESET,
    ID_FIT,
    ID_GRID
};

} // namespace

MainFrame::MainFrame(wxWindow* parent)
    : wxFrame(parent, wxID_ANY, L"电路原理图编辑器") {
    m_canvas = new Canvas(this);

    wxPanel* sidebar = new wxPanel(this);
    auto* title = new wxStaticText(sidebar, wxID_ANY, L"元件库");
    m_palette = new wxListBox(sidebar, wxID_ANY, wxDefaultPosition,
                              wxSize(150, -1));
    m_palette->Append(L"选择 / 移动");
    for (const auto& spec : ShapeLibrary::Get().All()) {
        m_palette->Append(spec.displayName);
    }
    m_palette->SetSelection(0);

    auto* sidebarSizer = new wxBoxSizer(wxVERTICAL);
    sidebarSizer->Add(title, 0, wxALL, 8);
    sidebarSizer->Add(m_palette, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* highLegend = new wxStaticText(sidebar, wxID_ANY, L"1  高电平");
    highLegend->SetForegroundColour(LogicHighColour());
    auto* lowLegend = new wxStaticText(sidebar, wxID_ANY, L"0  低电平");
    lowLegend->SetForegroundColour(LogicLowColour());
    sidebarSizer->Add(new wxStaticText(sidebar, wxID_ANY, L"信号"), 0,
                      wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sidebarSizer->Add(highLegend, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sidebarSizer->Add(lowLegend, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    sidebar->SetSizer(sidebarSizer);

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(sidebar, 0, wxEXPAND | wxALL, 4);
    sizer->Add(m_canvas, 1, wxEXPAND | wxALL, 4);
    SetSizer(sizer);

    auto* fileMenu = new wxMenu;
    fileMenu->Append(wxID_NEW, L"新建(&N)\tCtrl+N");
    fileMenu->Append(wxID_OPEN, L"打开(&O)...\tCtrl+O");
    fileMenu->Append(ID_SAVE, L"保存(&S)\tCtrl+S");
    fileMenu->Append(ID_SAVE_AS, L"另存为(&A)...");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, L"退出(&Q)");

    auto* editMenu = new wxMenu;
    editMenu->Append(ID_DELETE, L"删除选中\tDel");
    editMenu->Append(ID_ROTATE, L"顺时针旋转\tR");

    auto* viewMenu = new wxMenu;
    viewMenu->Append(ID_ZOOM_IN, L"放大");
    viewMenu->Append(ID_ZOOM_OUT, L"缩小");
    viewMenu->Append(ID_ZOOM_RESET, L"实际大小");
    viewMenu->Append(ID_FIT, L"适应窗口");
    viewMenu->AppendCheckItem(ID_GRID, L"显示网格");
    viewMenu->Check(ID_GRID, true);

    auto* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, L"关于(&A)");

    auto* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, L"文件(&F)");
    menuBar->Append(editMenu, L"编辑(&E)");
    menuBar->Append(viewMenu, L"视图(&V)");
    menuBar->Append(helpMenu, L"帮助(&H)");
    SetMenuBar(menuBar);

    CreateStatusBar(2);
    const int statusWidths[] = { -1, 390 };
    SetStatusWidths(2, statusWidths);

    Bind(wxEVT_MENU, &MainFrame::OnNew, this, wxID_NEW);
    Bind(wxEVT_MENU, &MainFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave, this, ID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, ID_SAVE_AS);
    Bind(wxEVT_MENU, &MainFrame::OnExit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::OnDelete, this, ID_DELETE);
    Bind(wxEVT_MENU, &MainFrame::OnRotate, this, ID_ROTATE);
    Bind(wxEVT_MENU, &MainFrame::OnZoomIn, this, ID_ZOOM_IN);
    Bind(wxEVT_MENU, &MainFrame::OnZoomOut, this, ID_ZOOM_OUT);
    Bind(wxEVT_MENU, &MainFrame::OnZoomReset, this, ID_ZOOM_RESET);
    Bind(wxEVT_MENU, &MainFrame::OnFit, this, ID_FIT);
    Bind(wxEVT_MENU, &MainFrame::OnToggleGrid, this, ID_GRID);
    Bind(wxEVT_MENU, &MainFrame::OnAbout, this, wxID_ABOUT);
    Bind(wxEVT_LISTBOX, &MainFrame::OnPalette, this, m_palette->GetId());
    Bind(wxEVT_TEXT, &MainFrame::OnCanvasChanged, this, kCanvasChangedId);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);

    SetSize(FromDIP(wxSize(1000, 680)));
    Centre();
    SyncUi();
}

void MainFrame::OnNew(wxCommandEvent&) {
    if (m_canvas->IsDirty()) {
        const int answer = wxMessageBox(L"当前图纸尚未保存，是否新建？",
                                        L"提示", wxYES_NO | wxICON_QUESTION, this);
        if (answer != wxYES) return;
    }
    m_canvas->NewFile();
    m_palette->SetSelection(0);
    SyncUi();
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dialog(this, L"打开原理图", wxEmptyString, wxEmptyString,
                        L"EDA 文件 (*.eda)|*.eda|所有文件 (*.*)|*.*",
                        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK) return;

    wxString error;
    if (!m_canvas->OpenFile(dialog.GetPath(), &error)) {
        wxMessageBox(L"打开失败：" + error, L"错误", wxOK | wxICON_ERROR, this);
        return;
    }
    m_palette->SetSelection(0);
    SyncUi();
}

void MainFrame::OnSave(wxCommandEvent&) {
    if (m_canvas->FilePath().IsEmpty()) {
        wxCommandEvent event;
        OnSaveAs(event);
        return;
    }
    SaveTo(m_canvas->FilePath());
}

void MainFrame::OnSaveAs(wxCommandEvent&) {
    wxFileDialog dialog(this, L"保存原理图", wxEmptyString, L"未命名.eda",
                        L"EDA 文件 (*.eda)|*.eda|所有文件 (*.*)|*.*",
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() != wxID_OK) return;

    wxString path = dialog.GetPath();
    if (!path.Lower().EndsWith(L".eda")) path += L".eda";
    SaveTo(path);
}

bool MainFrame::SaveTo(const wxString& path) {
    wxString error;
    if (!m_canvas->SaveFile(path, &error)) {
        wxMessageBox(L"保存失败：" + error, L"错误", wxOK | wxICON_ERROR, this);
        return false;
    }
    SyncUi();
    return true;
}

void MainFrame::OnExit(wxCommandEvent&) {
    Close();
}

void MainFrame::OnDelete(wxCommandEvent&) {
    m_canvas->DeleteSelected();
}

void MainFrame::OnRotate(wxCommandEvent&) {
    m_canvas->RotateSelected();
}

void MainFrame::OnZoomIn(wxCommandEvent&) {
    m_canvas->ZoomIn();
}

void MainFrame::OnZoomOut(wxCommandEvent&) {
    m_canvas->ZoomOut();
}

void MainFrame::OnZoomReset(wxCommandEvent&) {
    m_canvas->ZoomReset();
}

void MainFrame::OnFit(wxCommandEvent&) {
    m_canvas->FitToWindow();
}

void MainFrame::OnToggleGrid(wxCommandEvent& event) {
    m_canvas->ToggleGrid();
    if (wxMenuBar* menuBar = GetMenuBar()) {
        if (wxMenuItem* item = menuBar->FindItem(ID_GRID)) {
            item->Check(event.IsChecked());
        }
    }
}

void MainFrame::OnAbout(wxCommandEvent&) {
    wxMessageBox(L"电路原理图编辑器\n"
                 L"支持放置逻辑门、连接导线并实时计算组合逻辑。\n"
                 L"单击输入或输出元件可切换 0/1，红色表示高电平。",
                 L"关于", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnPalette(wxCommandEvent& event) {
    const int index = event.GetSelection();
    if (index <= 0) {
        m_canvas->CancelMode();
        return;
    }
    m_canvas->StartPlace(static_cast<ComponentType>(index - 1));
}

void MainFrame::OnCanvasChanged(wxCommandEvent&) {
    SyncUi();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (m_canvas->IsDirty()) {
        const int answer = wxMessageBox(L"当前图纸尚未保存，是否退出？",
                                        L"提示",
                                        wxYES_NO | wxCANCEL | wxICON_QUESTION,
                                        this);
        if (answer == wxCANCEL) {
            event.Veto();
            return;
        }
        if (answer == wxYES) {
            if (m_canvas->FilePath().IsEmpty()) {
                wxCommandEvent saveAsEvent;
                OnSaveAs(saveAsEvent);
                if (m_canvas->IsDirty()) {
                    event.Veto();
                    return;
                }
            } else if (!SaveTo(m_canvas->FilePath())) {
                event.Veto();
                return;
            }
        }
    }
    event.Skip();
}

void MainFrame::SyncUi() {
    wxString title = m_canvas->FilePath().IsEmpty()
                         ? wxString(L"未命名")
                         : wxFileName(m_canvas->FilePath()).GetFullName();
    if (m_canvas->IsDirty()) title = L"*" + title;
    SetTitle(title + L" - 电路原理图编辑器");

    SetStatusText(m_canvas->StatusText(), 0);

    const Schematic& model = m_canvas->Model();
    int inputs = 0;
    int highInputs = 0;
    int outputs = 0;
    int highOutputs = 0;
    for (const Component& c : model.Components()) {
        if (c.type == ComponentType::INPUT) {
            ++inputs;
            if (c.value) ++highInputs;
        } else if (c.type == ComponentType::OUTPUT) {
            ++outputs;
            if (c.value) ++highOutputs;
        }
    }

    SetStatusText(wxString::Format(
                      L"输入 %d/%d 为 1   输出 %d/%d 为 1   元件 %d   导线 %d",
                      highInputs, inputs, highOutputs, outputs,
                      static_cast<int>(model.Components().size()),
                      static_cast<int>(model.Wires().size())),
                  1);
}

} // namespace eda
