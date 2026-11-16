// Copyright (c) 2026 Chris Polewczuk
// SPDX-License-Identifier: MIT

#ifdef QT_GUI_TESTING_MCP_ENABLED
#    include "AgentBridge.h"
#endif

#include <QApplication>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

#ifdef QT_GUI_TESTING_MCP_ENABLED
    // Construct the bridge after QApplication so it inherits the GUI thread's
    // event loop and can safely inspect or mutate QWidget instances.
    AgentBridge bridge(9999);
#endif

    // The demo deliberately assigns object names to every interactive widget.
    // These names are the stable selectors used by the MCP tools; production
    // applications should follow the same convention for agent-facing UI.
    QWidget window;
    window.setObjectName("mainWindow");
    window.setWindowTitle("Qt Widgets MCP demo");

    auto *layout = new QVBoxLayout(&window);
    auto *form = new QFormLayout;

    auto *nameEdit = new QLineEdit;
    nameEdit->setObjectName("nameEdit");
    form->addRow("Name", nameEdit);
    layout->addLayout(form);

    auto *messageEdit = new QTextEdit;
    messageEdit->setObjectName("messageEdit");
    layout->addWidget(messageEdit);

    auto *helloButton = new QPushButton("Say hello");
    helloButton->setObjectName("helloButton");
    layout->addWidget(helloButton);

    QObject::connect(helloButton, &QPushButton::clicked, [&] {
        messageEdit->setPlainText(QStringLiteral("Hello, %1!").arg(nameEdit->text()));
    });

    window.show();
    return app.exec();
}
