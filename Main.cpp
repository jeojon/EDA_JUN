#include "Main.h"
#include "EDA.h"

bool MyApp::OnInit() {
    auto* frame = new eda::MainFrame();
    frame->Show(true);
    SetTopWindow(frame);
    return true;
}

wxIMPLEMENT_APP(MyApp);
