// Copyright (c) 2026 Chris Polewczuk
// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMutex>
#include <QMutexLocker>
#include <QMouseEvent>
#include <QPixmap>
#include <QScreen>
#include <QStringList>
#include <QTextEdit>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWindow>
#include <QWidget>

/*!
    \class AgentBridge
    \brief Exposes a small, localhost-only JSON protocol for driving a Qt GUI application.

    AgentBridge is intended for debug and automation builds. It listens on the
    loopback interface and translates one JSON request per TCP connection into
    a Qt GUI operation. Supported operations include QWidget and QWindow tree
    inspection, semantic button activation, text assignment, keyboard and mouse
    event delivery, Qt diagnostic collection, and PNG screenshot capture.

    The bridge deliberately lives in the application's GUI thread. Qt GUI and
    Qt Widgets objects must be accessed from that thread, so requests are
    handled by the QTcpServer event handlers rather than by a worker thread.

    \warning This bridge grants local clients the ability to interact with the
    application. Do not enable it in production builds, and do not bind it to
    a non-loopback address without adding authentication and authorization.

    \internal
    This is an application-internal diagnostic facility, not a general-purpose
    Qt library API.
*/
class AgentBridge final : public QObject {
    Q_OBJECT

public:
    /*!
        Constructs a bridge that listens on the loopback interface.

        \a port is the TCP port used by the bridge. The default is \c 9999.
        \a parent owns the bridge according to normal QObject parent-child
        semantics. The bridge and its QTcpServer therefore share the thread
        affinity of the object that constructs it.

        The constructor starts listening immediately. The MCP c status command
        reports whether the server is listening and which port was selected.
    */
    explicit AgentBridge(quint16 port = 9999, QObject *parent = nullptr)
        : QObject(parent), server(new QTcpServer(this)) {
        connect(server, &QTcpServer::newConnection,
                this, &AgentBridge::onNewConnection);
        server->listen(QHostAddress::LocalHost, port);
        activeBridge = this;
        previousMessageHandler = qInstallMessageHandler(&AgentBridge::messageHandler);
    }

    ~AgentBridge() override {
        // Qt's message handler is process-global. Restore the handler that was
        // installed before this diagnostic bridge took ownership of it.
        if (activeBridge == this) {
            qInstallMessageHandler(previousMessageHandler);
            activeBridge = nullptr;
        }
    }

private slots:
    // Accept every connection currently queued by QTcpServer. Each client is
    // intentionally handled as a single request/response exchange: this keeps
    // the wire protocol simple and prevents one client from monopolising the
    // diagnostic socket with a stream of commands.
    void onNewConnection() {
        while (server->hasPendingConnections()) {
            QTcpSocket *client = server->nextPendingConnection();

            // TCP is a byte stream, not a message transport. A complete JSON
            // document can therefore be split across multiple readyRead()
            // emissions. Keep the bytes for this connection until Qt can parse
            // a complete JSON object.
            auto *request = new QByteArray;

            connect(client, &QTcpSocket::readyRead, this, [this, client, request] {
                request->append(client->readAll());

                QJsonParseError parseError;
                const QJsonDocument document =
                    QJsonDocument::fromJson(*request, &parseError);
                if (parseError.error != QJsonParseError::NoError ||
                    !document.isObject()) {
                    // Qt reports an incomplete JSON document with one of the
                    // Unterminated* errors rather than a single generic
                    // "unexpected end" value. Wait for another TCP packet in
                    // those cases; other parse errors are terminal.
                    if (isIncompleteJsonError(parseError.error))
                        return;

                    sendResponse(client, QJsonObject{{"error", "Invalid JSON request"}});
                    return;
                }

                // All widget work occurs here, in the bridge object's thread.
                // That thread is the GUI thread when AgentBridge is constructed
                // next to QApplication in main().
                sendResponse(client, handleCommand(document.object()));
            });

            // The request buffer is owned by this connection. QObject invokes
            // the cleanup lambda when the socket disconnects, including after
            // sendResponse() calls disconnectFromHost().
            connect(client, &QTcpSocket::disconnected, client, [client, request] {
                delete request;
                client->deleteLater();
            });
        }
    }

private:
    QTcpServer *server;
    QtMessageHandler previousMessageHandler = nullptr;
    QVector<QJsonObject> logEntries;
    mutable QMutex logMutex;

    inline static AgentBridge *activeBridge = nullptr;
    inline static constexpr int maximumLogEntries = 200;

    static QString messageTypeName(QtMsgType type) {
        switch (type) {
        case QtDebugMsg:
            return QStringLiteral("debug");
        case QtInfoMsg:
            return QStringLiteral("info");
        case QtWarningMsg:
            return QStringLiteral("warning");
        case QtCriticalMsg:
            return QStringLiteral("critical");
        case QtFatalMsg:
            return QStringLiteral("fatal");
        }
        return QStringLiteral("unknown");
    }

    // Capture Qt diagnostics in a bounded in-memory buffer. The previous
    // handler is still called so attaching the bridge does not suppress the
    // application's normal debugger/console diagnostics.
    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &message) {
        AgentBridge *bridge = activeBridge;
        if (bridge) {
            QJsonObject entry;
            entry["timestamp"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
            entry["type"] = messageTypeName(type);
            entry["category"] = QString::fromUtf8(context.category);
            entry["message"] = message;
            if (context.file)
                entry["file"] = QString::fromUtf8(context.file);
            if (context.line > 0)
                entry["line"] = context.line;
            if (context.function)
                entry["function"] = QString::fromUtf8(context.function);

            QMutexLocker locker(&bridge->logMutex);
            bridge->logEntries.append(entry);
            while (bridge->logEntries.size() > maximumLogEntries)
                bridge->logEntries.removeFirst();
        }

        if (bridge && bridge->previousMessageHandler)
            bridge->previousMessageHandler(type, context, message);
    }

    QJsonArray logEntriesJson() const {
        QMutexLocker locker(&logMutex);
        QJsonArray entries;
        for (const QJsonObject &entry : logEntries)
            entries.append(entry);
        return entries;
    }

    // QJsonDocument parses a byte array only when it has a complete document.
    // These errors can legitimately occur when the final JSON token has not
    // arrived yet because TCP split the request across multiple packets.
    static bool isIncompleteJsonError(QJsonParseError::ParseError error) {
        switch (error) {
        case QJsonParseError::UnterminatedObject:
        case QJsonParseError::UnterminatedArray:
        case QJsonParseError::UnterminatedString:
        case QJsonParseError::MissingObject:
            return true;
        default:
            return false;
        }
    }

    // Serialize a compact response and close the connection. QTcpSocket keeps
    // queued output until disconnectFromHost() has completed its graceful
    // close, so callers do not need to manage a second write-completion path.
    static void sendResponse(QTcpSocket *client, const QJsonObject &response) {
        client->write(QJsonDocument(response).toJson(QJsonDocument::Compact));
        client->flush();
        client->disconnectFromHost();
    }

    // Convert only the state that is useful to an automation client. The
    // generic "text" property is intentionally optional in Qt: many widgets
    // do not expose one, in which case QVariant::toString() yields an empty
    // string. Child widgets are represented recursively in visual/object-tree
    // order as supplied by QObject::children(). The explicit uiType field lets
    // an agent distinguish a QWidget hierarchy from a native Qt GUI window.
    static QJsonObject serializeWidget(QWidget *widget) {
        QJsonObject object;
        object["uiType"] = QStringLiteral("widget");
        object["objectName"] = widget->objectName();
        object["className"] = widget->metaObject()->className();
        object["visible"] = widget->isVisible();
        object["enabled"] = widget->isEnabled();
        object["hasFocus"] = widget->hasFocus();
        object["accessibleName"] = widget->accessibleName();
        const QRect geometry = widget->geometry();
        object["geometry"] = QJsonObject{
            {"x", geometry.x()},
            {"y", geometry.y()},
            {"width", geometry.width()},
            {"height", geometry.height()}};
        if (const auto *lineEdit = qobject_cast<const QLineEdit *>(widget))
            object["text"] = lineEdit->text();
        else if (const auto *textEdit = qobject_cast<const QTextEdit *>(widget))
            object["text"] = textEdit->toPlainText();
        else
            object["text"] = widget->property("text").toString();

        QJsonArray children;
        for (QObject *child : widget->children()) {
            if (auto *childWidget = qobject_cast<QWidget *>(child))
                children.append(serializeWidget(childWidget));
        }
        if (!children.isEmpty())
            object["children"] = children;

        QJsonArray actions;
        for (QAction *action : widget->actions()) {
            QJsonObject actionObject;
            actionObject["uiType"] = QStringLiteral("action");
            actionObject["objectName"] = action->objectName();
            actionObject["className"] = action->metaObject()->className();
            actionObject["text"] = action->text();
            actionObject["visible"] = action->isVisible();
            actionObject["enabled"] = action->isEnabled();
            actionObject["checkable"] = action->isCheckable();
            actionObject["checked"] = action->isChecked();
            actionObject["separator"] = action->isSeparator();
            actionObject["shortcut"] = action->shortcut().toString();
            actions.append(actionObject);
        }
        if (!actions.isEmpty())
            object["actions"] = actions;

        return object;
    }

    // QWindow is the Qt GUI module's window abstraction. It is used directly
    // by Qt Quick, OpenGL, multimedia, native-surface, and custom-rendering
    // applications, so it cannot be treated as a QWidget. Keep its metadata
    // separate and intentionally small: a QWindow does not expose QWidget's
    // child-widget hierarchy or convenience text properties.
    static QJsonObject serializeWindow(QWindow *window) {
        QJsonObject object;
        object["uiType"] = QStringLiteral("window");
        object["objectName"] = window->objectName();
        object["className"] = window->metaObject()->className();
        object["title"] = window->title();
        object["visible"] = window->isVisible();
        object["active"] = window->isActive();
        object["exposed"] = window->isExposed();
        const QRect geometry = window->geometry();
        object["geometry"] = QJsonObject{
            {"x", geometry.x()},
            {"y", geometry.y()},
            {"width", geometry.width()},
            {"height", geometry.height()}};

        QJsonArray children;
        for (QObject *child : window->children()) {
            if (auto *childWindow = qobject_cast<QWindow *>(child))
                children.append(serializeWindow(childWindow));
        }
        if (!children.isEmpty())
            object["children"] = children;

        return object;
    }

    // objectName is the stable targeting contract between the application and
    // the MCP client. findChild() returns the first matching descendant, so
    // applications should assign unique names to interactive widgets.
    static QWidget *findWidgetByName(const QString &name) {
        if (name.isEmpty())
            return nullptr;

        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            if (topLevel->objectName() == name)
                return topLevel;
            if (auto *found = topLevel->findChild<QWidget *>(name))
                return found;
        }
        return nullptr;
    }

    static QWindow *findWindowByName(const QString &name) {
        if (name.isEmpty())
            return nullptr;

        for (QWindow *window : QGuiApplication::allWindows()) {
            if (window->objectName() == name)
                return window;
        }
        return nullptr;
    }

    // Search QWidget objects first to preserve the original bridge's targeting
    // behavior when an application happens to reuse an objectName. Standalone
    // QWindow objects are then resolved from QGuiApplication's window list.
    static QObject *findUiObjectByName(const QString &name) {
        if (QWidget *widget = findWidgetByName(name))
            return widget;
        return findWindowByName(name);
    }

    // QAction is not a QWidget, so it does not appear in QApplication's widget
    // list. It is nevertheless owned by a menu, menu bar, or toolbar QWidget;
    // searching each widget's action collection gives menu and toolbar actions
    // the same stable objectName targeting contract as visible controls.
    static QAction *findActionByName(const QString &name) {
        if (name.isEmpty())
            return nullptr;

        for (QWidget *widget : QApplication::allWidgets()) {
            for (QAction *action : widget->actions()) {
                if (action->objectName() == name)
                    return action;
            }
        }
        return nullptr;
    }

    // A top-level QWidget owns a QWindow handle internally. It is already
    // represented by the QWidget tree, so exposing that handle as a second
    // root would make screenshots and tree assertions ambiguous. This helper
    // identifies only genuinely standalone QWindow roots.
    static bool isWidgetWindowHandle(const QWindow *window) {
        for (QWidget *topLevel : QApplication::topLevelWidgets()) {
            if (topLevel->windowHandle() == window)
                return true;
        }
        return false;
    }

    static QJsonArray standaloneWindowTree() {
        QJsonArray windows;
        for (QWindow *window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && !isWidgetWindowHandle(window))
                windows.append(serializeWindow(window));
        }
        return windows;
    }

    // QMessageBox is the standard Qt Widgets error-popup type. Keep the
    // severity mapping in one place so the MCP client receives stable strings
    // instead of depending on Qt's enum values.
    static QString messageBoxSeverity(QMessageBox::Icon icon) {
        switch (icon) {
        case QMessageBox::Critical:
            return QStringLiteral("critical");
        case QMessageBox::Warning:
            return QStringLiteral("warning");
        case QMessageBox::Information:
            return QStringLiteral("information");
        case QMessageBox::Question:
            return QStringLiteral("question");
        case QMessageBox::NoIcon:
            return QStringLiteral("none");
        }
        return QStringLiteral("unknown");
    }

    // Return the user-facing content of every visible QMessageBox. A normal
    // application window is intentionally excluded: this command is designed
    // for checking transient error and warning popups after an action.
    static QJsonObject serializeMessageBox(const QMessageBox *messageBox) {
        QJsonObject object;
        object["objectName"] = messageBox->objectName();
        object["className"] = messageBox->metaObject()->className();
        object["title"] = messageBox->windowTitle();
        object["severity"] = messageBoxSeverity(messageBox->icon());
        object["text"] = messageBox->text();
        object["informativeText"] = messageBox->informativeText();
        object["detailedText"] = messageBox->detailedText();
        return object;
    }

    static Qt::KeyboardModifiers keyboardModifiers(const QJsonValue &value) {
        Qt::KeyboardModifiers modifiers = Qt::NoModifier;
        const QJsonArray names = value.isArray()
            ? value.toArray()
            : QJsonArray{value.toString()};
        for (const QJsonValue &nameValue : names) {
            const QString name = nameValue.toString().trimmed().toLower();
            if (name == "ctrl" || name == "control")
                modifiers |= Qt::ControlModifier;
            else if (name == "alt")
                modifiers |= Qt::AltModifier;
            else if (name == "shift")
                modifiers |= Qt::ShiftModifier;
            else if (name == "meta" || name == "win" || name == "command")
                modifiers |= Qt::MetaModifier;
        }
        return modifiers;
    }

    static Qt::Key keyFromName(QString name, Qt::KeyboardModifiers *modifiers) {
        name = name.trimmed();
        const QStringList parts = name.split('+', Qt::SkipEmptyParts);
        if (parts.size() > 1) {
            QJsonArray embeddedModifiers;
            for (int index = 0; index < parts.size() - 1; ++index)
                embeddedModifiers.append(parts.at(index));
            *modifiers |= keyboardModifiers(embeddedModifiers);
            name = parts.constLast();
        }

        const QString normalized = name.toLower();
        const QHash<QString, Qt::Key> specialKeys{
            {"enter", Qt::Key_Return}, {"return", Qt::Key_Return},
            {"tab", Qt::Key_Tab}, {"backtab", Qt::Key_Backtab},
            {"escape", Qt::Key_Escape}, {"esc", Qt::Key_Escape},
            {"backspace", Qt::Key_Backspace}, {"delete", Qt::Key_Delete},
            {"insert", Qt::Key_Insert}, {"home", Qt::Key_Home},
            {"end", Qt::Key_End}, {"left", Qt::Key_Left},
            {"right", Qt::Key_Right}, {"up", Qt::Key_Up},
            {"down", Qt::Key_Down}, {"pageup", Qt::Key_PageUp},
            {"pagedown", Qt::Key_PageDown}, {"space", Qt::Key_Space},
            {"pause", Qt::Key_Pause}, {"print", Qt::Key_Print},
            {"menu", Qt::Key_Menu}
        };
        if (specialKeys.contains(normalized))
            return specialKeys.value(normalized);

        if (normalized.size() == 2 && normalized.at(0) == 'f' &&
            normalized.at(1).isDigit()) {
            const int functionNumber = normalized.at(1).digitValue();
            if (functionNumber >= 1 && functionNumber <= 9)
                return static_cast<Qt::Key>(Qt::Key_F1 + functionNumber - 1);
        }
        if (normalized.size() == 3 && normalized.at(0) == 'f' &&
            normalized.mid(1).toInt() >= 10 && normalized.mid(1).toInt() <= 12) {
            return static_cast<Qt::Key>(Qt::Key_F1 + normalized.mid(1).toInt() - 1);
        }
        if (name.size() == 1)
            return static_cast<Qt::Key>(name.at(0).toUpper().unicode());

        return Qt::Key_unknown;
    }

    static Qt::MouseButton mouseButtonFromName(const QString &name) {
        const QString normalized = name.trimmed().toLower();
        if (normalized == "right")
            return Qt::RightButton;
        if (normalized == "middle" || normalized == "middlebutton")
            return Qt::MiddleButton;
        return Qt::LeftButton;
    }

    static QJsonObject sendKeyEvent(const QJsonObject &request) {
        QObject *target = findUiObjectByName(request.value("target").toString());
        if (!target)
            target = QApplication::focusWidget();
        if (!target)
            target = QGuiApplication::focusWindow();
        if (!target)
            return QJsonObject{{"error", "Keyboard target not found"}};

        const auto isVisibleAndEnabled = [](QObject *object) {
            if (auto *widget = qobject_cast<QWidget *>(object))
                return widget->isVisible() && widget->isEnabled();
            if (auto *window = qobject_cast<QWindow *>(object))
                // QWindow has no QWidget-style enabled property. Visibility
                // is the meaningful readiness check for a native GUI window.
                return window->isVisible();
            return false;
        };
        if (!isVisibleAndEnabled(target))
            return QJsonObject{{"error", "Keyboard target is not visible or enabled"}};

        Qt::KeyboardModifiers modifiers = keyboardModifiers(request.value("modifiers"));
        const Qt::Key key = keyFromName(request.value("key").toString(), &modifiers);
        if (key == Qt::Key_unknown)
            return QJsonObject{{"error", "Unsupported key name"}};

        if (auto *widget = qobject_cast<QWidget *>(target))
            widget->setFocus(Qt::OtherFocusReason);
        else if (auto *window = qobject_cast<QWindow *>(target))
            window->requestActivate();
        QString text;
        if (request.value("key").toString().size() == 1)
            text = request.value("key").toString();
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(target, &press);
        QCoreApplication::sendEvent(target, &release);
        return QJsonObject{{"status", "key_sent"}, {"key", request.value("key")}};
    }

    static QJsonObject sendMouseEvent(const QJsonObject &request) {
        QObject *target = findUiObjectByName(request.value("target").toString());
        if (!target)
            return QJsonObject{{"error", "Mouse target not found"}};

        const auto isVisibleAndEnabled = [](QObject *object) {
            if (auto *widget = qobject_cast<QWidget *>(object))
                return widget->isVisible() && widget->isEnabled();
            if (auto *window = qobject_cast<QWindow *>(object))
                return window->isVisible();
            return false;
        };
        if (!isVisibleAndEnabled(target))
            return QJsonObject{{"error", "Mouse target is not visible or enabled"}};

        const QString action = request.value("action").toString("click").toLower();
        QRect targetRect;
        if (auto *widget = qobject_cast<QWidget *>(target))
            targetRect = widget->rect();
        else
            targetRect = QRect(QPoint(0, 0), qobject_cast<QWindow *>(target)->size());
        const QPoint position(
            request.contains("x") ? request.value("x").toInt() : targetRect.center().x(),
            request.contains("y") ? request.value("y").toInt() : targetRect.center().y());
        const Qt::MouseButton button = mouseButtonFromName(
            request.value("button").toString("left"));
        const Qt::MouseButtons buttons = button == Qt::NoButton
            ? Qt::NoButton
            : Qt::MouseButtons(button);
        const Qt::KeyboardModifiers modifiers = keyboardModifiers(request.value("modifiers"));

        QPoint globalPosition;
        if (auto *widget = qobject_cast<QWidget *>(target)) {
            widget->setFocus(Qt::OtherFocusReason);
            globalPosition = widget->mapToGlobal(position);
        } else {
            auto *window = qobject_cast<QWindow *>(target);
            window->requestActivate();
            globalPosition = window->mapToGlobal(position);
        }
        if (action == "move") {
            QMouseEvent move(QEvent::MouseMove, QPointF(position),
                             QPointF(globalPosition), Qt::NoButton,
                             buttons, modifiers);
            QCoreApplication::sendEvent(target, &move);
            return QJsonObject{{"status", "mouse_moved"}};
        }
        if (action != "click" && action != "double-click" && action != "doubleclick")
            return QJsonObject{{"error", "Unsupported mouse action"}};

        const QEvent::Type pressType = action == "click"
            ? QEvent::MouseButtonPress
            : QEvent::MouseButtonDblClick;
        QMouseEvent press(pressType, QPointF(position), QPointF(globalPosition),
                          button, buttons, modifiers);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(position),
                            QPointF(globalPosition), button, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(target, &press);
        QCoreApplication::sendEvent(target, &release);
        return QJsonObject{{"status", action == "click" ? "mouse_clicked" : "mouse_double_clicked"}};
    }

    static QJsonObject encodeScreenshot(const QPixmap &pixmap, const QObject *target) {
        if (pixmap.isNull())
            return QJsonObject{{"error", "Could not capture screenshot"}};

        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!pixmap.save(&buffer, "PNG"))
            return QJsonObject{{"error", "Could not encode screenshot as PNG"}};
        return QJsonObject{
            {"image_base64", QString::fromLatin1(bytes.toBase64())},
            {"objectName", target->objectName()},
            {"className", target->metaObject()->className()}};
    }

    // Dispatch one validated JSON object. This function is deliberately kept
    // synchronous: the command is complete when its response is written, and
    // all operations remain serialized by the GUI event loop.
    QJsonObject handleCommand(const QJsonObject &request) {
        const QString command = request.value("cmd").toString();

        if (command == "tree") {
            QJsonArray roots;
            for (QWidget *topLevel : QApplication::topLevelWidgets()) {
                if (topLevel->isVisible())
                    roots.append(serializeWidget(topLevel));
            }
            // Keep "widgets" for clients written against the original
            // protocol, and expose standalone QWindow roots separately so a
            // consumer can choose the representation it understands.
            return QJsonObject{
                {"widgets", roots},
                {"windows", standaloneWindowTree()}};
        }

        if (command == "status") {
            int visibleWindows = 0;
            for (QWindow *topLevel : QGuiApplication::topLevelWindows()) {
                if (topLevel->isVisible())
                    ++visibleWindows;
            }

            QJsonObject status;
            status["listening"] = server->isListening();
            status["port"] = static_cast<int>(server->serverPort());
            status["topLevelWidgetCount"] = QApplication::topLevelWidgets().size();
            status["topLevelWindowCount"] = QGuiApplication::topLevelWindows().size();
            status["visibleWindowCount"] = visibleWindows;
            status["activeWindow"] = QGuiApplication::focusWindow()
                ? QGuiApplication::focusWindow()->objectName()
                : QString();
            status["logCount"] = logEntriesJson().size();
            return status;
        }

        if (command == "idle") {
            const int milliseconds = qBound(0, request.value("milliseconds").toInt(50), 5000);
            QElapsedTimer timer;
            timer.start();
            do {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            } while (timer.elapsed() < milliseconds);

            return QJsonObject{{"processedMilliseconds", timer.elapsed()}};
        }

        if (command == "logs")
            return QJsonObject{{"logs", logEntriesJson()}};

        if (command == "clear_logs") {
            QMutexLocker locker(&logMutex);
            logEntries.clear();
            return QJsonObject{{"status", "logs_cleared"}};
        }

        if (command == "click") {
            // QAbstractButton::click() performs semantic activation and emits
            // the usual button signals. It is not intended to emulate pointer
            // motion, hover, or platform-specific mouse synthesis.
            QWidget *widget = findWidgetByName(request.value("target").toString());
            if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
                if (!button->isVisible())
                    return QJsonObject{{"error", "Button is not visible"}};
                if (!button->isEnabled())
                    return QJsonObject{{"error", "Button is disabled"}};
                button->click();
                return QJsonObject{{"status", "clicked"}};
            }
            return QJsonObject{{"error", "Button not found or not clickable"}};
        }

        if (command == "activate_action") {
            QAction *action = findActionByName(request.value("target").toString());
            if (!action)
                return QJsonObject{{"error", "Action not found"}};
            if (!action->isEnabled())
                return QJsonObject{{"error", "Action is disabled"}};
            if (!action->isVisible())
                return QJsonObject{{"error", "Action is not visible"}};

            // QAction::trigger() is the semantic equivalent of selecting the
            // command from either a menu or toolbar. It emits triggered() and
            // applies the normal checkable-action state transition without
            // requiring platform-specific pointer event synthesis.
            action->trigger();
            return QJsonObject{
                {"status", "activated"},
                {"objectName", action->objectName()},
                {"text", action->text()}};
        }

        if (command == "type") {
            // setText()/setPlainText() provide deterministic state assignment.
            // They do not simulate individual keyboard events, which is useful
            // for state-oriented automation but should be documented to callers.
            QWidget *widget = findWidgetByName(request.value("target").toString());
            const QString text = request.value("text").toString();
            if (auto *lineEdit = qobject_cast<QLineEdit *>(widget)) {
                lineEdit->setText(text);
                return QJsonObject{{"status", "typed"}};
            }
            if (auto *textEdit = qobject_cast<QTextEdit *>(widget)) {
                textEdit->setPlainText(text);
                return QJsonObject{{"status", "typed"}};
            }
            return QJsonObject{{"error", "Text input not found"}};
        }

        if (command == "key")
            return sendKeyEvent(request);

        if (command == "mouse")
            return sendMouseEvent(request);

        if (command == "popup_errors") {
            QJsonArray popups;
            for (QWidget *topLevel : QApplication::topLevelWidgets()) {
                if (!topLevel->isVisible())
                    continue;

                if (const auto *messageBox = qobject_cast<QMessageBox *>(topLevel))
                    popups.append(serializeMessageBox(messageBox));
            }
            return QJsonObject{{"popups", popups}};
        }

        if (command == "screenshot") {
            // A named target is useful for popup diagnostics. Without one,
            // prefer a visible QMessageBox and fall back to the first visible
            // QWidget or standalone QWindow. The target-specific path is
            // shared so a named QWindow can be captured just like a widget.
            const QString targetName = request.value("target").toString();
            QObject *target = targetName.isEmpty() ? nullptr : findUiObjectByName(targetName);
            if (!targetName.isEmpty() && !target)
                return QJsonObject{{"error", "Screenshot target not found"}};
            if (target && ((qobject_cast<QWidget *>(target) &&
                            !qobject_cast<QWidget *>(target)->isVisible()) ||
                           (qobject_cast<QWindow *>(target) &&
                            !qobject_cast<QWindow *>(target)->isVisible())))
                return QJsonObject{{"error", "Screenshot target is not visible"}};

            if (!target) {
                for (QWidget *topLevel : QApplication::topLevelWidgets()) {
                    if (topLevel->isVisible() &&
                        qobject_cast<QMessageBox *>(topLevel)) {
                        target = topLevel;
                        break;
                    }
                }
            }

            if (!target) {
                for (QWidget *topLevel : QApplication::topLevelWidgets()) {
                    if (topLevel->isVisible()) {
                        target = topLevel;
                        break;
                    }
                }
            }

            if (!target) {
                for (QWindow *window : QGuiApplication::topLevelWindows()) {
                    if (window->isVisible() && !isWidgetWindowHandle(window)) {
                        target = window;
                        break;
                    }
                }
            }

            if (!target)
                return QJsonObject{{"error", "No visible top-level Qt window"}};

            if (auto *widget = qobject_cast<QWidget *>(target))
                return encodeScreenshot(widget->grab(), target);

            auto *window = qobject_cast<QWindow *>(target);
            if (!window || !window->screen())
                return QJsonObject{{"error", "Screenshot target has no associated screen"}};
            return encodeScreenshot(window->screen()->grabWindow(window->winId()), target);
        }

        // Keep unknown commands explicit so clients do not mistake an empty
        // response for a successful no-op.
        return QJsonObject{{"error", "Unknown command"}};
    }
};
