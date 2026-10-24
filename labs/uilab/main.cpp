// curvyUiLab - a throwaway app for trying curvy's pen+keyboard interaction (rings, Space, shape drawing) before it goes into the editor.
//
//   curvyUiLab.exe [--automation <pipe>]

#ifdef CURVY_GUI_AUTOMATION
#include "newui/automation.h"
#endif
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/rootview.h"

#include "labcanvas.h"

#include <cstdio>
#include <string>

extern void registerReflectionData();
extern void registerLabReflection();

int main(int argc, char** argv) {
    registerReflectionData();
    registerLabReflection();
    std::string automationPipe;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--automation" && i + 1 < argc) {
            automationPipe = argv[++i];
        }
    }

    newui::Frame frame;
    newui::Application& app = newui::Application::instance();
    app.setName("curvyUiLab");
    app.setFrame(&frame);
    frame.setName("lab");
    if (!newui::Bundle::instance().loadFrame(frame)) {
        std::printf("curvyUiLab: could not load Resources/lab.newui\n");
        return 1;
    }

    newui::RootView& root = frame.rootView();
    auto* canvas = dynamic_cast<lab::LabCanvas*>(root.findView("canvas"));
    auto* tool = dynamic_cast<newui::Label*>(root.findView("hintTool"));
    auto* keys = dynamic_cast<newui::Label*>(root.findView("hintKeys"));
    auto* info = dynamic_cast<newui::Label*>(root.findView("hintInfo"));
    auto* document = dynamic_cast<newui::Label*>(root.findView("documentName"));
    if (canvas == nullptr || tool == nullptr || keys == nullptr || info == nullptr || document == nullptr) {
        std::printf("curvyUiLab: lab.newui is missing a view\n");
        return 1;
    }
    canvas->onHint.add([=](lab::LabCanvas&, const std::string& t, const std::string& k, const std::string& i) {
        tool->setText(t);
        keys->setText(k);
        info->setText(i);
        return newui::SyncReturn::Handled;
    });
    canvas->onDocumentName.add([=](lab::LabCanvas&, const std::string& name) {
        document->setText(name);
        return newui::SyncReturn::Handled;
    });
    root.setFocusedSubView(canvas);

#ifdef CURVY_GUI_AUTOMATION
    if (!automationPipe.empty()) {
        newui::Automation::expose(root);
        newui::Automation::start(automationPipe, app.runLoop());
    }
#endif
    app.run();
#ifdef CURVY_GUI_AUTOMATION
    newui::Automation::stop();
#endif
    return 0;
}
