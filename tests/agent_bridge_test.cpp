// Copyright (c) 2026 Chris Polewczuk
// SPDX-License-Identifier: MIT

#include "../src/AgentBridge.h"

#include <QApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QImage>
#include <QKeyEvent>
#include <QKeySequence>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QTextEdit>
#include <QToolBar>
#include <QTcpSocket>
#include <QTimer>
#include <QWindow>
#include <QtTest>

#include <memory>

namespace {

// Keep the test port outside the bridge's application default. A dedicated
// port makes it obvious when a test accidentally talks to a manually launched
// demo application instead of the bridge created by this test process.
constexpr quint16 testPort = 19999;

QJsonObject sendPayload(const QList<QByteArray> &chunks)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, testPort);
    if (!socket.waitForConnected(1000)) {
        qWarning() << "Could not connect to AgentBridge:" << socket.errorString();
        return {};
    }

    // The server and the test client share the GUI thread. A blocking
    // waitForReadyRead() here would prevent the server from receiving and
    // handling the request, so use a small nested event loop instead.
    QEventLoop responseLoop;
    QTimer timeout;
    timeout.setSingleShot(true);

    QObject::connect(&socket, &QTcpSocket::readyRead,
                     &responseLoop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::disconnected,
                     &responseLoop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout,
                     &responseLoop, &QEventLoop::quit);

    for (qsizetype index = 0; index < chunks.size(); ++index) {
        socket.write(chunks.at(index));
        socket.flush();
        if (index + 1 < chunks.size())
            QTest::qWait(10);
    }
    timeout.start(1000);
    responseLoop.exec();

    const QByteArray responseBytes = socket.readAll();
    if (responseBytes.isEmpty()) {
        qWarning() << "AgentBridge returned no response";
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument response =
        QJsonDocument::fromJson(responseBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !response.isObject()) {
        qWarning() << "AgentBridge returned invalid JSON:" << parseError.errorString();
        return {};
    }

    return response.object();
}

QJsonObject sendCommand(const QJsonObject &command)
{
    return sendPayload({QJsonDocument(command).toJson(QJsonDocument::Compact)});
}

bool treeContainsObject(const QJsonArray &widgets, const QString &objectName)
{
    for (const QJsonValue &value : widgets) {
        const QJsonObject widget = value.toObject();
        if (widget.value("objectName").toString() == objectName)
            return true;

        if (treeContainsObject(widget.value("children").toArray(), objectName))
            return true;
    }
    return false;
}

bool treeContainsAction(const QJsonArray &widgets, const QString &objectName)
{
    for (const QJsonValue &value : widgets) {
        const QJsonObject widget = value.toObject();
        for (const QJsonValue &actionValue : widget.value("actions").toArray()) {
            if (actionValue.toObject().value("objectName").toString() == objectName)
                return true;
        }
        if (treeContainsAction(widget.value("children").toArray(), objectName))
            return true;
    }
    return false;
}

QJsonObject findActionInTree(const QJsonArray &widgets, const QString &objectName)
{
    for (const QJsonValue &value : widgets) {
        const QJsonObject widget = value.toObject();
        for (const QJsonValue &actionValue : widget.value("actions").toArray()) {
            const QJsonObject action = actionValue.toObject();
            if (action.value("objectName").toString() == objectName)
                return action;
        }
        const QJsonObject childAction =
            findActionInTree(widget.value("children").toArray(), objectName);
        if (!childAction.isEmpty())
            return childAction;
    }
    return {};
}

// A real QWindow test object makes the GUI-module coverage meaningful. The
// bridge must dispatch events to the QWindow itself; routing them through a
// QWidget would not cover Qt Quick, OpenGL, or other native-surface clients.
class RecordingWindow final : public QWindow {
public:
    bool keyReceived = false;
    bool mouseReceived = false;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        keyReceived = true;
        QWindow::keyPressEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        mouseReceived = true;
        QWindow::mousePressEvent(event);
    }
};

} // namespace

class AgentBridgeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        bridge = std::make_unique<AgentBridge>(testPort);

        window.setObjectName("testWindow");
        window.setWindowTitle("AgentBridge test window");

        auto *centralWidget = new QWidget(&window);
        centralWidget->setObjectName("centralWidget");
        window.setCentralWidget(centralWidget);

        nameEdit = new QLineEdit(centralWidget);
        nameEdit->setObjectName("nameEdit");

        messageEdit = new QTextEdit(centralWidget);
        messageEdit->setObjectName("messageEdit");

        submitButton = new QPushButton("Submit", centralWidget);
        submitButton->setObjectName("submitButton");
        QObject::connect(submitButton, &QPushButton::clicked,
                         messageEdit, [this] {
                             messageEdit->setPlainText(nameEdit->text());
                         });

        auto *disabledButton = new QPushButton("Disabled", centralWidget);
        disabledButton->setObjectName("disabledButton");
        disabledButton->setEnabled(false);

        hiddenEdit = new QLineEdit(centralWidget);
        hiddenEdit->setObjectName("hiddenEdit");
        hiddenEdit->hide();

        auto *fileMenu = window.menuBar()->addMenu("File");
        fileMenu->setObjectName("fileMenu");
        openAction = fileMenu->addAction("Open");
        openAction->setObjectName("openAction");
        openAction->setShortcut(QKeySequence("Ctrl+O"));
        QObject::connect(openAction, &QAction::triggered,
                         messageEdit, [this] {
                             messageEdit->setPlainText("Open triggered");
                         });

        toggleAction = fileMenu->addAction("Toggle");
        toggleAction->setObjectName("toggleAction");
        toggleAction->setCheckable(true);

        disabledAction = fileMenu->addAction("Disabled action");
        disabledAction->setObjectName("disabledAction");
        disabledAction->setEnabled(false);

        auto *fileToolBar = window.addToolBar("File");
        fileToolBar->setObjectName("fileToolBar");
        fileToolBar->addAction(openAction);

        window.show();
        guiWindow.setObjectName("testGuiWindow");
        guiWindow.setTitle("AgentBridge test QWindow");
        guiWindow.resize(160, 100);
        guiWindow.show();
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        window.close();
        guiWindow.close();
        bridge.reset();
    }

    void widgetTreeContainsNamedWidgets()
    {
        const QJsonObject response = sendCommand(QJsonObject{{"cmd", "tree"}});

        QVERIFY2(!response.contains("error"),
                 qPrintable(response.value("error").toString()));
        QVERIFY(treeContainsObject(response.value("widgets").toArray(), "testWindow"));
        QVERIFY(treeContainsObject(response.value("widgets").toArray(), "nameEdit"));
        QVERIFY(treeContainsObject(response.value("widgets").toArray(), "messageEdit"));
        QVERIFY(treeContainsObject(response.value("widgets").toArray(), "submitButton"));
        QVERIFY(treeContainsAction(response.value("widgets").toArray(), "openAction"));
        const QJsonObject openActionObject =
            findActionInTree(response.value("widgets").toArray(), "openAction");
        QCOMPARE(openActionObject.value("uiType").toString(), QStringLiteral("action"));
        QCOMPARE(openActionObject.value("text").toString(), QStringLiteral("Open"));
        QCOMPARE(openActionObject.value("shortcut").toString(), QStringLiteral("Ctrl+O"));
        QVERIFY(openActionObject.value("enabled").toBool());
        QVERIFY(treeContainsObject(response.value("windows").toArray(), "testGuiWindow"));
        QCOMPARE(response.value("windows").toArray().at(0).toObject().value("uiType").toString(),
                 QStringLiteral("window"));
    }

    void statusIdleAndLogsCommandsWork()
    {
        const QJsonObject status = sendCommand(QJsonObject{{"cmd", "status"}});
        QVERIFY(status.value("listening").toBool());
        QCOMPARE(status.value("port").toInt(), static_cast<int>(testPort));
        QVERIFY(status.value("visibleWindowCount").toInt() >= 1);
        QVERIFY(status.value("topLevelWindowCount").toInt() >= 2);

        const QJsonObject idle = sendCommand(
            QJsonObject{{"cmd", "idle"}, {"milliseconds", 10}});
        QVERIFY(idle.value("processedMilliseconds").toInt() >= 10);

        qWarning("agent bridge test warning");
        const QJsonObject logs = sendCommand(QJsonObject{{"cmd", "logs"}});
        const QJsonArray entries = logs.value("logs").toArray();
        QVERIFY(!entries.isEmpty());

        const QJsonObject cleared =
            sendCommand(QJsonObject{{"cmd", "clear_logs"}});
        QCOMPARE(cleared.value("status").toString(), QStringLiteral("logs_cleared"));
    }

    void tcpProtocolHandlesFragmentedAndInvalidJson()
    {
        const QByteArray request =
            QJsonDocument(QJsonObject{{"cmd", "status"}})
                .toJson(QJsonDocument::Compact);
        const qsizetype split = request.size() / 2;
        const QJsonObject fragmentedResponse = sendPayload({
            request.left(split),
            request.mid(split),
        });
        QVERIFY(fragmentedResponse.value("listening").toBool());

        const QJsonObject invalidResponse = sendPayload({QByteArray("not-json")});
        QCOMPARE(invalidResponse.value("error").toString(),
                 QStringLiteral("Invalid JSON request"));
    }

    void typeCommandUpdatesLineEdit()
    {
        const QJsonObject response = sendCommand(
            QJsonObject{{"cmd", "type"}, {"target", "nameEdit"}, {"text", "Ada"}});

        QCOMPARE(response.value("status").toString(), QStringLiteral("typed"));
        QCOMPARE(nameEdit->text(), QStringLiteral("Ada"));
    }

    void clickCommandActivatesButton()
    {
        const QJsonObject response = sendCommand(
            QJsonObject{{"cmd", "click"}, {"target", "submitButton"}});

        QCOMPARE(response.value("status").toString(), QStringLiteral("clicked"));
        QCOMPARE(messageEdit->toPlainText(), nameEdit->text());
    }

    void activateActionTriggersMenuAndToolbarCommand()
    {
        const QJsonObject response = sendCommand(
            QJsonObject{{"cmd", "activate_action"}, {"target", "openAction"}});

        QCOMPARE(response.value("status").toString(), QStringLiteral("activated"));
        QCOMPARE(response.value("objectName").toString(), QStringLiteral("openAction"));
        QCOMPARE(messageEdit->toPlainText(), QStringLiteral("Open triggered"));

        const QJsonObject toggleResponse = sendCommand(
            QJsonObject{{"cmd", "activate_action"}, {"target", "toggleAction"}});
        QCOMPARE(toggleResponse.value("status").toString(), QStringLiteral("activated"));
        QVERIFY(toggleAction->isChecked());
    }

    void keyboardAndMouseEventsReachWidgets()
    {
        nameEdit->clear();
        const QJsonObject keyResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"target", "nameEdit"}, {"key", "A"}});
        QCOMPARE(keyResponse.value("status").toString(), QStringLiteral("key_sent"));
        QCOMPARE(nameEdit->text(), QStringLiteral("A"));

        const QJsonObject shortcutResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"target", "nameEdit"}, {"key", "Ctrl+A"}});
        QCOMPARE(shortcutResponse.value("status").toString(), QStringLiteral("key_sent"));

        const QJsonObject functionKeyResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"target", "nameEdit"}, {"key", "F12"}});
        QCOMPARE(functionKeyResponse.value("status").toString(), QStringLiteral("key_sent"));

        const QJsonObject focusedKeyResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"key", "B"}});
        QCOMPARE(focusedKeyResponse.value("status").toString(), QStringLiteral("key_sent"));

        const QJsonObject moveResponse = sendCommand(
            QJsonObject{{"cmd", "mouse"}, {"target", "submitButton"}, {"action", "move"}});
        QCOMPARE(moveResponse.value("status").toString(), QStringLiteral("mouse_moved"));

        const QJsonObject clickResponse = sendCommand(
            QJsonObject{{"cmd", "mouse"}, {"target", "submitButton"}, {"action", "click"}});
        QCOMPARE(clickResponse.value("status").toString(),
                 QStringLiteral("mouse_clicked"));
        QCOMPARE(messageEdit->toPlainText(), QStringLiteral("A"));

        const QJsonObject doubleClickResponse = sendCommand(
            QJsonObject{{"cmd", "mouse"}, {"target", "submitButton"},
                        {"action", "double-click"}, {"button", "right"}});
        QCOMPARE(doubleClickResponse.value("status").toString(),
                 QStringLiteral("mouse_double_clicked"));
    }

    void textEditAndValidationErrorsAreHandled()
    {
        const QJsonObject textResponse = sendCommand(
            QJsonObject{{"cmd", "type"}, {"target", "messageEdit"},
                        {"text", "Multiline\nmessage"}});
        QCOMPARE(textResponse.value("status").toString(), QStringLiteral("typed"));
        QCOMPARE(messageEdit->toPlainText(), QStringLiteral("Multiline\nmessage"));

        const QJsonObject disabledButtonResponse = sendCommand(
            QJsonObject{{"cmd", "click"}, {"target", "disabledButton"}});
        QCOMPARE(disabledButtonResponse.value("error").toString(),
                 QStringLiteral("Button is disabled"));

        const QJsonObject missingButtonResponse = sendCommand(
            QJsonObject{{"cmd", "click"}, {"target", "missingButton"}});
        QCOMPARE(missingButtonResponse.value("error").toString(),
                 QStringLiteral("Button not found or not clickable"));

        const QJsonObject disabledActionResponse = sendCommand(
            QJsonObject{{"cmd", "activate_action"}, {"target", "disabledAction"}});
        QCOMPARE(disabledActionResponse.value("error").toString(),
                 QStringLiteral("Action is disabled"));

        const QJsonObject missingActionResponse = sendCommand(
            QJsonObject{{"cmd", "activate_action"}, {"target", "missingAction"}});
        QCOMPARE(missingActionResponse.value("error").toString(),
                 QStringLiteral("Action not found"));

        const QJsonObject unsupportedKeyResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"target", "nameEdit"}, {"key", "NotAKey"}});
        QCOMPARE(unsupportedKeyResponse.value("error").toString(),
                 QStringLiteral("Unsupported key name"));

        const QJsonObject unsupportedMouseResponse = sendCommand(
            QJsonObject{{"cmd", "mouse"}, {"target", "submitButton"},
                        {"action", "drag"}});
        QCOMPARE(unsupportedMouseResponse.value("error").toString(),
                 QStringLiteral("Unsupported mouse action"));
    }

    void screenshotCommandReturnsPng()
    {
        const QJsonObject response = sendCommand(QJsonObject{{"cmd", "screenshot"}});

        QVERIFY2(!response.contains("error"),
                 qPrintable(response.value("error").toString()));
        const QByteArray imageBytes =
            QByteArray::fromBase64(response.value("image_base64").toString().toLatin1());
        QImage image;
        QVERIFY(image.loadFromData(imageBytes, "PNG"));
        QVERIFY(!image.isNull());
        QVERIFY(image.width() > 0);
        QVERIFY(image.height() > 0);
    }

    void qWindowReceivesGuiEventsAndCanBeCaptured()
    {
        const QJsonObject keyResponse = sendCommand(
            QJsonObject{{"cmd", "key"}, {"target", "testGuiWindow"}, {"key", "B"}});
        QCOMPARE(keyResponse.value("status").toString(), QStringLiteral("key_sent"));
        QVERIFY(guiWindow.keyReceived);

        const QJsonObject mouseResponse = sendCommand(
            QJsonObject{{"cmd", "mouse"}, {"target", "testGuiWindow"}, {"action", "click"}});
        QCOMPARE(mouseResponse.value("status").toString(), QStringLiteral("mouse_clicked"));
        QVERIFY(guiWindow.mouseReceived);

        const QJsonObject screenshot = sendCommand(
            QJsonObject{{"cmd", "screenshot"}, {"target", "testGuiWindow"}});
        if (screenshot.contains("error")) {
            // The offscreen QPA plugin does not expose a composited native
            // screen surface for QScreen::grabWindow(). The bridge reports
            // this honestly; desktop-backed platforms are covered by the
            // successful PNG path below.
            QCOMPARE(screenshot.value("error").toString(),
                     QStringLiteral("Could not capture screenshot"));
        } else {
            const QByteArray imageBytes = QByteArray::fromBase64(
                screenshot.value("image_base64").toString().toLatin1());
            QImage image;
            QVERIFY(image.loadFromData(imageBytes, "PNG"));
            QVERIFY(!image.isNull());
        }
    }

    void popupErrorsAreReportedAndTargetedScreenshotWorks()
    {
        QMessageBox errorBox(&window);
        errorBox.setObjectName("connectionError");
        errorBox.setWindowTitle("Connection error");
        errorBox.setIcon(QMessageBox::Critical);
        errorBox.setText("Could not connect to the service.");
        errorBox.setInformativeText("The service is not running.");
        errorBox.setDetailedText("Connection refused on 127.0.0.1:8080");
        errorBox.show();
        QTest::qWait(50);

        const QJsonObject response =
            sendCommand(QJsonObject{{"cmd", "popup_errors"}});
        const QJsonArray popups = response.value("popups").toArray();
        QCOMPARE(popups.size(), 1);

        const QJsonObject popup = popups.at(0).toObject();
        QCOMPARE(popup.value("objectName").toString(), QStringLiteral("connectionError"));
        QCOMPARE(popup.value("severity").toString(), QStringLiteral("critical"));
        QCOMPARE(popup.value("text").toString(),
                 QStringLiteral("Could not connect to the service."));
        QCOMPARE(popup.value("detailedText").toString(),
                 QStringLiteral("Connection refused on 127.0.0.1:8080"));

        const QJsonObject screenshot = sendCommand(
            QJsonObject{{"cmd", "screenshot"}, {"target", "connectionError"}});
        QVERIFY(screenshot.contains("image_base64"));
        QCOMPARE(screenshot.value("objectName").toString(),
                 QStringLiteral("connectionError"));

        errorBox.close();
    }

    void screenshotTargetValidationIsExplicit()
    {
        const QJsonObject missingTarget = sendCommand(
            QJsonObject{{"cmd", "screenshot"}, {"target", "missingWindow"}});
        QCOMPARE(missingTarget.value("error").toString(),
                 QStringLiteral("Screenshot target not found"));

        const QJsonObject hiddenTarget = sendCommand(
            QJsonObject{{"cmd", "screenshot"}, {"target", "hiddenEdit"}});
        QCOMPARE(hiddenTarget.value("error").toString(),
                 QStringLiteral("Screenshot target is not visible"));
    }

    void unknownCommandReturnsError()
    {
        const QJsonObject response =
            sendCommand(QJsonObject{{"cmd", "does-not-exist"}});

        QCOMPARE(response.value("error").toString(), QStringLiteral("Unknown command"));
    }

private:
    std::unique_ptr<AgentBridge> bridge;
    QMainWindow window;
    RecordingWindow guiWindow;
    QLineEdit *nameEdit = nullptr;
    QTextEdit *messageEdit = nullptr;
    QLineEdit *hiddenEdit = nullptr;
    QPushButton *submitButton = nullptr;
    QAction *openAction = nullptr;
    QAction *toggleAction = nullptr;
    QAction *disabledAction = nullptr;
};

QTEST_MAIN(AgentBridgeTest)

#include "agent_bridge_test.moc"
