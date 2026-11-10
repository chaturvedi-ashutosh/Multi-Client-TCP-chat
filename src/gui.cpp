#include <QApplication>
#include <QAbstractSocket>
#include <QByteArray>
#include <QComboBox>
#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QStringList>
#include <QTextBrowser>
#include <QTcpSocket>
#include <QVBoxLayout>
#include <QWidget>

class CustomerChatWindow : public QMainWindow {
public:
    CustomerChatWindow(QTcpSocket* socket, const QString& customer_name)
        : socket_(socket) {
        setWindowTitle("Support Chat — Customer");
        resize(900, 650);
        setMinimumSize(680, 480);

        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(14);

        auto* heading = new QHBoxLayout;
        auto* title = new QLabel("Your support conversation", page);
        title->setObjectName("chatTitle");
        status_ = new QLabel("Joining the queue…", page);
        status_->setObjectName("chatStatus");
        heading->addWidget(title);
        heading->addStretch();
        heading->addWidget(status_);

        auto* identity = new QLabel("Customer: " + customer_name, page);
        identity->setObjectName("identity");
        technician_ = new QLabel("Technician: not assigned", page);
        technician_->setObjectName("identity");
        history_ = new QTextBrowser(page);
        history_->setObjectName("history");
        history_->setOpenLinks(false);
        history_->setPlaceholderText("Messages and notices will appear here.");

        auto* composer = new QHBoxLayout;
        message_ = new QLineEdit(page);
        message_->setPlaceholderText("Write a message to your technician…");
        message_->setEnabled(false);
        send_button_ = new QPushButton("Send", page);
        send_button_->setObjectName("sendButton");
        send_button_->setEnabled(false);
        composer->addWidget(message_, 1);
        composer->addWidget(send_button_);

        auto* footer = new QHBoxLayout;
        auto* privacy_note = new QLabel("Your messages go only to your assigned technician.", page);
        privacy_note->setObjectName("privacyNote");
        auto* disconnect_button = new QPushButton("Disconnect", page);
        footer->addWidget(privacy_note);
        footer->addStretch();
        footer->addWidget(disconnect_button);

        layout->addLayout(heading);
        layout->addWidget(identity);
        layout->addWidget(technician_);
        layout->addWidget(history_, 1);
        layout->addLayout(composer);
        layout->addLayout(footer);
        setCentralWidget(page);
        setStyleSheet(
            "QMainWindow { background: #f3f6fb; }"
            "QWidget { color: #182230; font: 10pt 'Segoe UI'; }"
            "QLabel#chatTitle { color: #13233f; font-size: 20pt; font-weight: 700; }"
            "QLabel#chatStatus { background: #e5edff; color: #315fcb;"
            " border-radius: 12px; padding: 7px 12px; font-weight: 600; }"
            "QLabel#identity, QLabel#privacyNote { color: #65748b; }"
            "QTextBrowser#history { background: white; border: 1px solid #d8e0eb;"
            " border-radius: 10px; padding: 12px; }"
            "QLineEdit { background: white; border: 1px solid #cbd5e1;"
            " border-radius: 8px; padding: 11px; }"
            "QLineEdit:focus { border: 2px solid #4678df; }"
            "QPushButton { border: 1px solid #cbd5e1; border-radius: 8px;"
            " padding: 10px 16px; background: white; }"
            "QPushButton#sendButton { background: #315fcb; color: white;"
            " border: 0; font-weight: 600; }"
            "QPushButton#sendButton:hover { background: #254fae; }"
            "QPushButton:disabled { color: #94a3b8; background: #e8edf4; }");

        connect(send_button_, &QPushButton::clicked, this,
                [this] { send_message(); });
        connect(message_, &QLineEdit::returnPressed, this,
                [this] { send_message(); });
        connect(disconnect_button, &QPushButton::clicked, this,
                [this] { close(); });
        append_message("Support", "You are connected. Waiting for a technician.", "#eaf0fb", "#315fcb");
    }

    void consume_buffer(const QByteArray& bytes) {
        pending_data_.append(bytes);
        while (true) {
            const qsizetype newline = pending_data_.indexOf('\n');
            if (newline < 0) break;
            if (newline > 1024) {
                set_connection_closed("Server sent a line exceeding the protocol limit.");
                socket_->disconnectFromHost();
                return;
            }
            QByteArray line = pending_data_.left(newline);
            pending_data_.remove(0, newline + 1);
            if (line.endsWith('\r')) line.chop(1);
            handle_server_line(QString::fromUtf8(line));
        }
        if (pending_data_.size() > 1024) {
            set_connection_closed("Server sent a line exceeding the protocol limit.");
            socket_->disconnectFromHost();
        }
    }

    void read_server_data() {
        consume_buffer(socket_->readAll());
    }

    void handle_server_line(const QString& line) {
        if (line.startsWith("WAITING|")) {
            const QString detail = line.mid(8);
            active_ = false;
            technician_name_.clear();
            technician_->setText("Technician: not assigned");
            update_composer();
            if (detail == "SESSION_ENDED") {
                set_status("Waiting for a technician", false);
                append_message("Support", "The session ended. You are back in the queue.", "#eaf0fb", "#315fcb");
            } else if (detail == "TECHNICIAN_DISCONNECTED") {
                set_status("Waiting for a technician", false);
                append_message("Support", "The technician disconnected. You are back at the front of the queue.", "#fff5df", "#92620b");
            } else if (detail == "ASSIGNMENT_FAILED") {
                set_status("Waiting for a technician", false);
                append_message("Support", "The assignment failed. Please wait for another request.", "#fff5df", "#92620b");
            } else {
                set_status("Waiting for a technician", false);
                append_message("Support", "You joined the queue. Customer ID: " + detail, "#eaf0fb", "#315fcb");
            }
        } else if (line.startsWith("ASSIGN|")) {
            const QStringList fields = line.split('|');
            if (fields.size() == 3) {
                active_ = true;
                technician_name_ = fields[2];
                technician_->setText("Technician: " + technician_name_);
                update_composer();
                set_status("Chatting with " + technician_name_, false);
                append_message("Support", "You are now chatting with " + technician_name_ + ".", "#e8f7ef", "#237a57");
            }
        } else if (line.startsWith("MESSAGE|")) {
            append_message(technician_name_.isEmpty() ? "Technician" : technician_name_,
                           line.mid(8), "#eaf0fb", "#315fcb");
        } else if (line.startsWith("BROADCAST|")) {
            append_message("Public notice", line.mid(10), "#fff5df", "#92620b");
        } else if (line.startsWith("ERROR|")) {
            append_message("Server error", line.mid(6), "#fff0ef", "#b42318");
            set_status("There was a server error", true);
        } else {
            append_message("Support", line, "#eaf0fb", "#315fcb");
        }
    }

    void handle_disconnected() {
        set_connection_closed("Disconnected from the server.");
    }

    void handle_socket_error(const QString& error) {
        set_connection_closed("Connection error: " + error);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            const QByteArray request = "DISCONNECT\n";
            if (socket_->write(request) == request.size()) socket_->disconnectFromHost();
            else socket_->abort();
        }
        event->accept();
    }

private:
    void set_status(const QString& text, bool is_error) {
        status_->setText(text);
        status_->setStyleSheet(is_error ? "background: #fff0ef; color: #b42318;"
                                         : "background: #e5edff; color: #315fcb;");
    }

    void set_connection_closed(const QString& reason) {
        active_ = false;
        technician_name_.clear();
        technician_->setText("Technician: not assigned");
        update_composer();
        set_status("Disconnected", true);
        append_message("Connection", reason, "#fff0ef", "#b42318");
    }

    void update_composer() {
        message_->setEnabled(active_);
        send_button_->setEnabled(active_);
        message_->setPlaceholderText(active_ ? "Write a message to your technician…"
                                             : "Waiting for a technician to join…");
    }

    void send_message() {
        const QString text = message_->text();
        if (!active_ || text.trimmed().isEmpty()) return;
        const QByteArray payload = text.toUtf8();
        if (payload.size() + 8 > 1024) {
            append_message("Message not sent", "Messages must fit within the 1024-byte protocol limit.", "#fff0ef", "#b42318");
            return;
        }
        const QByteArray frame = "MESSAGE|" + payload + "\n";
        if (socket_->write(frame) != frame.size()) {
            set_connection_closed("Could not send the message.");
            socket_->abort();
            return;
        }
        append_message("You", text, "#e8f7ef", "#237a57");
        message_->clear();
    }

    void append_message(const QString& label, const QString& text,
                        const QString& background, const QString& color) {
        const QString safe_text = text.toHtmlEscaped().replace('\n', "<br>");
        const QString html =
            "<div style='margin:8px 2px;padding:10px 12px;background:" + background +
            ";border-radius:8px;'><span style='font-weight:700;color:" + color + ";'>" +
            label.toHtmlEscaped() + "</span><br><span style='color:#243247;'>" + safe_text +
            "</span></div>";
        history_->append(html);
    }

    QTcpSocket* socket_;
    QLabel* status_ = nullptr;
    QLabel* technician_ = nullptr;
    QTextBrowser* history_ = nullptr;
    QLineEdit* message_ = nullptr;
    QPushButton* send_button_ = nullptr;
    QByteArray pending_data_;
    QString technician_name_;
    bool active_ = false;
};

class TechnicianChatWindow : public QMainWindow {
public:
    TechnicianChatWindow(QTcpSocket* socket, const QString& technician_name)
        : socket_(socket) {
        setWindowTitle("Support Chat — Technician");
        resize(960, 700);
        setMinimumSize(720, 540);

        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(28, 24, 28, 24);
        layout->setSpacing(14);

        auto* heading = new QHBoxLayout;
        auto* title = new QLabel("Technician workspace", page);
        title->setObjectName("chatTitle");
        status_ = new QLabel("Connecting…", page);
        status_->setObjectName("chatStatus");
        heading->addWidget(title);
        heading->addStretch();
        heading->addWidget(status_);

        technician_ = new QLabel("Technician: " + technician_name, page);
        technician_->setObjectName("identity");
        customer_ = new QLabel("No active customer", page);
        customer_->setObjectName("identity");
        history_ = new QTextBrowser(page);
        history_->setObjectName("history");

        auto* session_controls = new QHBoxLayout;
        next_button_ = new QPushButton("Next customer", page);
        next_button_->setObjectName("sendButton");
        end_button_ = new QPushButton("End session", page);
        end_button_->setEnabled(false);
        session_controls->addWidget(next_button_);
        session_controls->addWidget(end_button_);
        session_controls->addStretch();

        auto* private_composer = new QHBoxLayout;
        private_message_ = new QLineEdit(page);
        private_message_->setPlaceholderText("Private message to the active customer…");
        private_message_->setEnabled(false);
        private_send_button_ = new QPushButton("Send privately", page);
        private_send_button_->setEnabled(false);
        private_composer->addWidget(private_message_, 1);
        private_composer->addWidget(private_send_button_);

        auto* broadcast_title = new QLabel("PUBLIC ANNOUNCEMENT", page);
        broadcast_title->setObjectName("broadcastTitle");
        auto* broadcast_composer = new QHBoxLayout;
        broadcast_message_ = new QLineEdit(page);
        broadcast_message_->setPlaceholderText("Send a notice to every connected customer…");
        broadcast_button_ = new QPushButton("Broadcast", page);
        broadcast_button_->setObjectName("broadcastButton");
        broadcast_composer->addWidget(broadcast_message_, 1);
        broadcast_composer->addWidget(broadcast_button_);

        auto* footer = new QHBoxLayout;
        auto* privacy_note = new QLabel("Private messages go only to the active customer.", page);
        privacy_note->setObjectName("privacyNote");
        auto* disconnect_button = new QPushButton("Disconnect", page);
        footer->addWidget(privacy_note);
        footer->addStretch();
        footer->addWidget(disconnect_button);

        layout->addLayout(heading);
        layout->addWidget(technician_);
        layout->addWidget(customer_);
        layout->addWidget(history_, 1);
        layout->addLayout(session_controls);
        layout->addLayout(private_composer);
        layout->addWidget(broadcast_title);
        layout->addLayout(broadcast_composer);
        layout->addLayout(footer);
        setCentralWidget(page);
        setStyleSheet(
            "QMainWindow { background: #f3f6fb; }"
            "QWidget { color: #182230; font: 10pt 'Segoe UI'; }"
            "QLabel#chatTitle { color: #13233f; font-size: 20pt; font-weight: 700; }"
            "QLabel#chatStatus { background: #e5edff; color: #315fcb;"
            " border-radius: 12px; padding: 7px 12px; font-weight: 600; }"
            "QLabel#identity, QLabel#privacyNote { color: #65748b; }"
            "QLabel#broadcastTitle { color: #92620b; font-size: 9pt; font-weight: 700; }"
            "QTextBrowser#history { background: white; border: 1px solid #d8e0eb;"
            " border-radius: 10px; padding: 12px; }"
            "QLineEdit { background: white; border: 1px solid #cbd5e1;"
            " border-radius: 8px; padding: 10px; }"
            "QLineEdit:focus { border: 2px solid #4678df; }"
            "QPushButton { border: 1px solid #cbd5e1; border-radius: 8px;"
            " padding: 9px 14px; background: white; }"
            "QPushButton#sendButton { background: #315fcb; color: white;"
            " border: 0; font-weight: 600; }"
            "QPushButton#broadcastButton { background: #a56b08; color: white;"
            " border: 0; font-weight: 600; }"
            "QPushButton:disabled { color: #94a3b8; background: #e8edf4; }");

        connect(next_button_, &QPushButton::clicked, this,
                [this] { send_command("REQUEST_NEXT"); });
        connect(end_button_, &QPushButton::clicked, this,
                [this] { send_command("END_SESSION"); });
        connect(private_send_button_, &QPushButton::clicked, this,
                [this] { send_private_message(); });
        connect(private_message_, &QLineEdit::returnPressed, this,
                [this] { send_private_message(); });
        connect(broadcast_button_, &QPushButton::clicked, this,
                [this] { send_broadcast(); });
        connect(broadcast_message_, &QLineEdit::returnPressed, this,
                [this] { send_broadcast(); });
        connect(disconnect_button, &QPushButton::clicked, this,
                [this] { close(); });
        append_message("Support", "You are connected. Request the next customer when ready.", "#eaf0fb", "#315fcb");
    }

    void consume_buffer(const QByteArray& bytes) {
        pending_data_.append(bytes);
        while (true) {
            const qsizetype newline = pending_data_.indexOf('\n');
            if (newline < 0) break;
            if (newline > 1024) {
                set_connection_closed("Server sent a line exceeding the protocol limit.");
                socket_->disconnectFromHost();
                return;
            }
            QByteArray line = pending_data_.left(newline);
            pending_data_.remove(0, newline + 1);
            if (line.endsWith('\r')) line.chop(1);
            handle_server_line(QString::fromUtf8(line));
        }
        if (pending_data_.size() > 1024) {
            set_connection_closed("Server sent a line exceeding the protocol limit.");
            socket_->disconnectFromHost();
        }
    }

    void read_server_data() { consume_buffer(socket_->readAll()); }

    void handle_server_line(const QString& line) {
        if (line == "TECHNICIAN_READY") {
            set_status("Available for customers", false);
        } else if (line.startsWith("ASSIGN|")) {
            const QStringList fields = line.split('|');
            if (fields.size() == 3) {
                active_ = true;
                customer_name_ = fields[2];
                customer_->setText("Active customer: " + customer_name_ + "  ·  ID " + fields[1]);
                update_controls();
                set_status("Customer assigned", false);
                append_message("Support", "Private session started with " + customer_name_ + ".", "#e8f7ef", "#237a57");
            }
        } else if (line == "QUEUE_EMPTY") {
            active_ = false;
            customer_name_.clear();
            customer_->setText("No active customer");
            update_controls();
            set_status("Queue is empty", false);
            append_message("Support", "There are no customers waiting right now.", "#eaf0fb", "#315fcb");
        } else if (line == "BUSY") {
            active_ = true;
            update_controls();
            set_status("A session is already active", false);
        } else if (line == "SESSION_ENDED" || line.startsWith("SESSION_ENDED|")) {
            active_ = false;
            customer_name_.clear();
            customer_->setText("No active customer");
            update_controls();
            set_status("Available for customers", false);
            append_message("Support", line == "SESSION_ENDED" ? "Session ended; customer returned to the queue."
                                                                  : "The customer disconnected. Session ended.",
                           "#eaf0fb", "#315fcb");
        } else if (line.startsWith("MESSAGE|")) {
            append_message(customer_name_.isEmpty() ? "Customer" : customer_name_,
                           line.mid(8), "#eaf0fb", "#315fcb");
        } else if (line.startsWith("BROADCAST_SENT|")) {
            append_message("Broadcast", "Notice sent to " + line.mid(15) + " connected customer(s).",
                           "#fff5df", "#92620b");
        } else if (line.startsWith("ERROR|")) {
            const QString error = line.mid(6);
            if (error.contains("assignment failed")) {
                active_ = false;
                customer_name_.clear();
                customer_->setText("No active customer");
                update_controls();
            }
            set_status("Server error", true);
            append_message("Server error", error, "#fff0ef", "#b42318");
        } else {
            append_message("Support", line, "#eaf0fb", "#315fcb");
        }
    }

    void handle_disconnected() {
        set_connection_closed("Disconnected from the server.");
    }

    void handle_socket_error(const QString& error) {
        set_connection_closed("Connection error: " + error);
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            const QByteArray request = "DISCONNECT\n";
            if (socket_->write(request) == request.size()) socket_->disconnectFromHost();
            else socket_->abort();
        }
        event->accept();
    }

private:
    void set_status(const QString& text, bool is_error) {
        status_->setText(text);
        status_->setStyleSheet(is_error ? "background: #fff0ef; color: #b42318;"
                                         : "background: #e5edff; color: #315fcb;");
    }

    void set_connection_closed(const QString& reason) {
        connected_ = false;
        active_ = false;
        update_controls();
        set_status("Disconnected", true);
        append_message("Connection", reason, "#fff0ef", "#b42318");
    }

    void update_controls() {
        next_button_->setEnabled(connected_ && !active_);
        end_button_->setEnabled(connected_ && active_);
        private_message_->setEnabled(connected_ && active_);
        private_send_button_->setEnabled(connected_ && active_);
        broadcast_message_->setEnabled(connected_);
        broadcast_button_->setEnabled(connected_);
        private_message_->setPlaceholderText(active_ ? "Private message to " + customer_name_ + "…"
                                                     : "Private message to the active customer…");
    }

    bool send_frame(const QByteArray& frame) {
        if (socket_->state() != QAbstractSocket::ConnectedState || socket_->write(frame) != frame.size()) {
            set_connection_closed("Could not send the command.");
            socket_->abort();
            return false;
        }
        return true;
    }

    void send_command(const QByteArray& command) {
        send_frame(command + "\n");
    }

    void send_private_message() {
        const QString text = private_message_->text();
        if (!active_ || text.trimmed().isEmpty()) return;
        const QByteArray payload = text.toUtf8();
        if (payload.size() + 8 > 1024) {
            append_message("Message not sent", "Messages must fit within the 1024-byte protocol limit.", "#fff0ef", "#b42318");
            return;
        }
        if (send_frame("MESSAGE|" + payload + "\n")) {
            append_message("You", text, "#e8f7ef", "#237a57");
            private_message_->clear();
        }
    }

    void send_broadcast() {
        const QString text = broadcast_message_->text();
        if (text.trimmed().isEmpty()) return;
        const QByteArray payload = text.toUtf8();
        if (payload.size() + 10 > 1024) {
            append_message("Broadcast not sent", "Notices must fit within the 1024-byte protocol limit.", "#fff0ef", "#b42318");
            return;
        }
        if (send_frame("BROADCAST|" + payload + "\n")) {
            append_message("You · public notice", text, "#fff5df", "#92620b");
            broadcast_message_->clear();
        }
    }

    void append_message(const QString& label, const QString& text,
                        const QString& background, const QString& color) {
        const QString safe_text = text.toHtmlEscaped().replace('\n', "<br>");
        const QString html =
            "<div style='margin:8px 2px;padding:10px 12px;background:" + background +
            ";border-radius:8px;'><span style='font-weight:700;color:" + color + ";'>" +
            label.toHtmlEscaped() + "</span><br><span style='color:#243247;'>" + safe_text +
            "</span></div>";
        history_->append(html);
    }

    QTcpSocket* socket_;
    QLabel* status_ = nullptr;
    QLabel* technician_ = nullptr;
    QLabel* customer_ = nullptr;
    QTextBrowser* history_ = nullptr;
    QLineEdit* private_message_ = nullptr;
    QPushButton* private_send_button_ = nullptr;
    QPushButton* next_button_ = nullptr;
    QPushButton* end_button_ = nullptr;
    QLineEdit* broadcast_message_ = nullptr;
    QPushButton* broadcast_button_ = nullptr;
    QByteArray pending_data_;
    QString customer_name_;
    bool connected_ = true;
    bool active_ = false;
};

class ConnectionWindow : public QMainWindow {
public:
    explicit ConnectionWindow(QTcpSocket* socket) : socket_(socket) {
        setWindowTitle("Support Chat");
        setMinimumSize(480, 440);

        auto* page = new QWidget(this);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(36, 30, 36, 30);
        layout->setSpacing(16);

        auto* title = new QLabel("Support Chat", page);
        title->setObjectName("title");
        auto* subtitle = new QLabel("Connect to the customer support server", page);
        subtitle->setObjectName("subtitle");

        role_ = new QComboBox(page);
        role_->addItem("Customer", "CUSTOMER");
        role_->addItem("Technician", "TECHNICIAN");
        name_ = new QLineEdit("Customer", page);
        name_->setMaxLength(64);
        host_ = new QLineEdit("127.0.0.1", page);
        port_ = new QLineEdit("5555", page);
        port_->setValidator(new QIntValidator(1, 65535, port_));

        auto* form = new QFormLayout;
        form->setSpacing(12);
        form->addRow("Connect as", role_);
        form->addRow("Display name", name_);
        form->addRow("Server address", host_);
        form->addRow("Port", port_);

        status_ = new QLabel("Not connected", page);
        status_->setObjectName("status");
        connect_button_ = new QPushButton("Connect", page);
        connect_button_->setObjectName("connectButton");
        disconnect_button_ = new QPushButton("Disconnect", page);
        disconnect_button_->setEnabled(false);

        layout->addWidget(title);
        layout->addWidget(subtitle);
        layout->addSpacing(8);
        layout->addLayout(form);
        layout->addSpacing(4);
        layout->addWidget(status_);
        layout->addWidget(connect_button_);
        layout->addWidget(disconnect_button_);
        layout->addStretch();

        setCentralWidget(page);
        setStyleSheet(
            "QMainWindow { background: #f3f6fb; }"
            "QWidget { color: #182230; font: 10pt 'Segoe UI'; }"
            "QLabel#title { font-size: 25pt; font-weight: 700; color: #13233f; }"
            "QLabel#subtitle { color: #65748b; font-size: 11pt; }"
            "QLineEdit, QComboBox { background: white; border: 1px solid #cbd5e1;"
            " border-radius: 7px; padding: 9px; min-height: 20px; }"
            "QLineEdit:focus, QComboBox:focus { border: 2px solid #4678df; }"
            "QLabel#status { background: white; border-radius: 7px; padding: 11px;"
            " color: #40536f; }"
            "QPushButton { border: 1px solid #cbd5e1; border-radius: 7px;"
            " padding: 10px; background: white; }"
            "QPushButton#connectButton { background: #315fcb; color: white;"
            " border: 0; font-weight: 600; }"
            "QPushButton#connectButton:hover { background: #254fae; }"
            "QPushButton:disabled { color: #94a3b8; background: #e8edf4; }");

        connect(connect_button_, &QPushButton::clicked, this,
                [this] { start_connection(); });
        connect(disconnect_button_, &QPushButton::clicked, socket_,
                &QTcpSocket::disconnectFromHost);
        connect(role_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this](int index) {
                    name_->setText(index == 0 ? "Customer" : "Technician");
                    role_->hidePopup();
                });
        connect(socket_, &QTcpSocket::connected, this,
                [this] { send_login(); });
        connect(socket_, &QTcpSocket::readyRead, this,
                [this] { read_server_data(); });
        connect(socket_, &QTcpSocket::disconnected, this,
                [this] {
                    connect_button_->setEnabled(true);
                    disconnect_button_->setEnabled(false);
                    if (status_->text().startsWith("Connecting")) {
                        set_status("Connection closed before login completed.", true);
                    } else if (!status_->text().startsWith("Connection error:") &&
                               !status_->text().startsWith("Server:")) {
                        set_status("Disconnected", false);
                    }
                });
        connect(socket_, &QTcpSocket::errorOccurred, this,
                [this](QAbstractSocket::SocketError) {
                    set_status("Connection error: " + socket_->errorString(), true);
                    connect_button_->setEnabled(true);
                    disconnect_button_->setEnabled(socket_->state() != QAbstractSocket::UnconnectedState);
                });
    }

private:
    void set_status(const QString& message, bool is_error) {
        status_->setText(message);
        status_->setStyleSheet(is_error ? "color: #b42318;" : "color: #237a57;");
    }

    void start_connection() {
        const QByteArray name_bytes = name_->text().toUtf8();
        if (name_bytes.isEmpty() || name_bytes.size() > 64 ||
            name_->text().contains('|') || name_->text().contains('\n') ||
            name_->text().contains('\r')) {
            set_status("Enter a name of 1 to 64 bytes without | or line breaks.", true);
            return;
        }
        bool port_ok = false;
        const uint port_number = port_->text().toUInt(&port_ok);
        if (host_->text().trimmed().isEmpty() || !port_ok || port_number == 0 || port_number > 65535) {
            set_status("Enter a server address and a port from 1 to 65535.", true);
            return;
        }

        pending_data_.clear();
        connect_button_->setEnabled(false);
        disconnect_button_->setEnabled(true);
        set_status("Connecting to " + host_->text().trimmed() + ":" + QString::number(port_number) + "...", false);
        socket_->connectToHost(host_->text().trimmed(), static_cast<quint16>(port_number));
    }

    void send_login() {
        const QString role = role_->currentData().toString();
        const QByteArray request = "LOGIN|" + role.toUtf8() + "|" + name_->text().toUtf8() + "\n";
        if (socket_->write(request) != request.size()) {
            set_status("Could not send login to the server.", true);
            socket_->disconnectFromHost();
            return;
        }
        set_status("Connected. Waiting for the server response...", false);
    }

    void read_server_data() {
        pending_data_.append(socket_->readAll());
        while (true) {
            const qsizetype newline = pending_data_.indexOf('\n');
            if (newline < 0) break;
            if (newline > 1024) {
                set_status("Server sent a line exceeding the protocol limit.", true);
                socket_->disconnectFromHost();
                return;
            }
            QByteArray line = pending_data_.left(newline);
            pending_data_.remove(0, newline + 1);
            if (line.endsWith('\r')) line.chop(1);
            if (show_server_response(QString::fromUtf8(line))) {
                if (customer_chat_) customer_chat_->consume_buffer(pending_data_);
                if (technician_chat_) technician_chat_->consume_buffer(pending_data_);
                pending_data_.clear();
                return;
            }
        }
        if (pending_data_.size() > 1024) {
            set_status("Server sent a line exceeding the protocol limit.", true);
            socket_->disconnectFromHost();
        }
    }

    bool show_server_response(const QString& line) {
        if (line == "TECHNICIAN_READY") {
            start_technician_chat(line);
            return true;
        } else if (line.startsWith("WAITING|") && role_->currentData().toString() == "CUSTOMER") {
            start_customer_chat(line);
            return true;
        } else if (line.startsWith("WAITING|")) {
            set_status("Connected as customer. Waiting in the support queue. " + line.mid(8), false);
        } else if (line.startsWith("ERROR|")) {
            set_status("Server: " + line.mid(6), true);
        } else {
            set_status("Connected. Server: " + line, false);
        }
        return false;
    }

    void start_customer_chat(const QString& first_line) {
        customer_chat_ = new CustomerChatWindow(socket_, name_->text());
        customer_chat_->setAttribute(Qt::WA_DeleteOnClose);
        QObject::disconnect(socket_, nullptr, this, nullptr);
        connect(socket_, &QTcpSocket::readyRead, customer_chat_,
                [this] { customer_chat_->read_server_data(); });
        connect(socket_, &QTcpSocket::disconnected, customer_chat_,
                [this] { customer_chat_->handle_disconnected(); });
        connect(socket_, &QTcpSocket::errorOccurred, customer_chat_,
                [this](QAbstractSocket::SocketError) {
                    customer_chat_->handle_socket_error(socket_->errorString());
                });
        customer_chat_->show();
        customer_chat_->handle_server_line(first_line);
        close();
    }

    void start_technician_chat(const QString& first_line) {
        technician_chat_ = new TechnicianChatWindow(socket_, name_->text());
        technician_chat_->setAttribute(Qt::WA_DeleteOnClose);
        QObject::disconnect(socket_, nullptr, this, nullptr);
        connect(socket_, &QTcpSocket::readyRead, technician_chat_,
                [this] { technician_chat_->read_server_data(); });
        connect(socket_, &QTcpSocket::disconnected, technician_chat_,
                [this] { technician_chat_->handle_disconnected(); });
        connect(socket_, &QTcpSocket::errorOccurred, technician_chat_,
                [this](QAbstractSocket::SocketError) {
                    technician_chat_->handle_socket_error(socket_->errorString());
                });
        technician_chat_->show();
        technician_chat_->handle_server_line(first_line);
        close();
    }

    QComboBox* role_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* host_ = nullptr;
    QLineEdit* port_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* connect_button_ = nullptr;
    QPushButton* disconnect_button_ = nullptr;
    QTcpSocket* socket_;
    QByteArray pending_data_;
    CustomerChatWindow* customer_chat_ = nullptr;
    TechnicianChatWindow* technician_chat_ = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    auto* socket = new QTcpSocket(&application);
    ConnectionWindow window(socket);
    window.show();
    return application.exec();
}
