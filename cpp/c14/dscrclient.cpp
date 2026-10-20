#include "dscrclient.h"

#include <QDebug>

// helpers
static constexpr int connectBackoffMs(quint32 numConnectFailures) {
    switch(numConnectFailures) {
    case 0:
        return 0;
    case 1:
        return 100;
    case 2:
        return 200;
    case 3:
        return 400;
    case 4:
        return 800;
    case 5:
        return 1600;
    case 6:
        return 3200;
    default:
        return 6400;
    }
}
static const int CONNECT_TIMEOUT_MS = 5000;
static const int MESSAGE_ACK_TIMEOUT_MS = 5000;

// public
DscrClient::DscrClient(QObject *parent) : QObject(parent) {
    QObject::connect(&this->socket, &QWebSocket::connected, this, &DscrClient::onSocketConnected);
    QObject::connect(&this->socket, &QWebSocket::disconnected, this, &DscrClient::onSocketDisconnected);
    QObject::connect(&this->socket, &QWebSocket::textMessageReceived, this, &DscrClient::onSocketTextMessageReceived);
    // QObject::connect(&this->socket, &QWebSocket::errorOccurred, this, &DscrClient::onSocketErrorOccurred);
    QObject::connect(&this->timer, &QTimer::timeout, this, &DscrClient::onTimerTimeout);
    this->timer.setSingleShot(true);
}

void DscrClient::open(const QUrl &url) {
    qDebug() << "DscrClient::open" << url;
    this->connectionEnabled = true;
    this->url = url;
    this->numConnectFailures = 0;
    this->attemptConnect();
}

void DscrClient::close() {
    qDebug() << "DscrClient::close";
    this->socket.close(QWebSocketProtocol::CloseCodeNormal);
    this->socket.abort();
    this->timer.stop();
    this->connectionEnabled = false;
    this->registrationEnabled = false;
    this->everRegistered = false;
    this->transportStatus = DscrClient::TransportStatus::Disconnected;
    this->registrationStatus = DscrClient::RegistrationStatus::Unregistered;
}

void DscrClient::register_(qint64 callSign, bool tryFastReregister) {
    qDebug() << "DscrClient::register" << callSign << tryFastReregister;
    this->desiredCallSign = callSign;
    this->registrationEnabled = true;
    this->fastReregistrationEnabled = tryFastReregister;
    this->attemptRegister();
}

void DscrClient::sendWsTextMessage(const QString &text) {
    this->socket.sendTextMessage(text);
}

void DscrClient::sendDscrMessage(const QVector<qint64> &symbols) {
    if(!this->isConnectedAndRegistered()) {
        return;
    }
    if(symbols.length() > 0) {
        QString message = "M";
        for(auto symbol : symbols) {
            message += QString(",%1").arg(symbol);
        }
        this->socket.sendTextMessage(message);
    }
}

DscrClient::TransportStatus DscrClient::getTransportStatus() const {
    return this->transportStatus;
}
DscrClient::RegistrationStatus DscrClient::getRegistrationStatus() const {
    return this->registrationStatus;
}
bool DscrClient::isConnectedAndRegistered() const {
    return this->transportStatus == TransportStatus::Connected && this->registrationStatus == RegistrationStatus::Registered;
}

qint64 DscrClient::getRegisteredCallsign() const {
    return this->registeredCallSign;
}

// private slots
void DscrClient::onSocketConnected() {
    this->transportStatus = TransportStatus::Connected;
    this->numConnectFailures = 0;
    this->timer.stop();
    if(this->registrationEnabled) {
        this->attemptRegister();
    }
}
void DscrClient::onSocketDisconnected() {
    this->transportStatus = TransportStatus::Disconnected;
    this->registrationStatus = DscrClient::RegistrationStatus::Unregistered;
    this->numConnectFailures = 0;
    if(this->connectionEnabled) {
        this->attemptConnect();
    }
}
void DscrClient::onSocketTextMessageReceived(const QString &text) {
    const QDateTime time = QDateTime::currentDateTimeUtc();
    qDebug() << time << "DscrClient::onSocketTextMessageReceived" << text;
    emit this->textMessageReceived(text);

    const QStringList messageContents = text.split(",");
    QVector<qint64> integers;
    integers.reserve(messageContents.size() - 1);
    for(qsizetype i = 1; i < messageContents.size(); i++) {
        bool ok = false;
        qint64 val = messageContents[i].toLongLong(&ok);
        if(ok) {
            integers.append(val);
        } else {
            // don't continue parsing the message
            return;
        }
    }

    if(messageContents[0] == "R") {
        if(integers.size() >= 3) {
            emit this->dscrMessageReceived(time, integers[0], integers[1], integers.mid(2));
        }
    } else if(messageContents[0] == "K") {
        // TODO registration
    } else if(messageContents[0] == "E") {
        // TODO registration
    } else if(messageContents[0] == "U") {
        // TODO registration
    } else if(messageContents[0] == "C") {
        emit this->callSignsChanged(time, integers);
    }
}
// void DscrClient::onSocketErrorOccurred(QAbstractSocket::SocketError error);
void DscrClient::onTimerTimeout() {
    if(this->transportStatus == DscrClient::TransportStatus::Connecting) {
        this->socket.abort();
        this->transportStatus = DscrClient::TransportStatus::BackingOff;
        this->timer.setInterval(connectBackoffMs(this->numConnectFailures));
        this->timer.start();
    } else if(this->transportStatus == DscrClient::TransportStatus::BackingOff) {
        this->attemptConnect();
    }
}

// private
void DscrClient::attemptConnect() {
    this->socket.open(this->url);
    this->transportStatus = DscrClient::TransportStatus::Connecting;
    this->timer.setInterval(CONNECT_TIMEOUT_MS);
    this->timer.start();
}

void DscrClient::attemptRegister() {
    if(this->transportStatus != TransportStatus::Connected) {
        return;
    }
    if(false /*this->fastReregistrationEnabled*/) {
        // TODO need to track time disconnected
        this->socket.sendTextMessage(QString("S,%1,0").arg(this->desiredCallSign));
    } else {
        this->socket.sendTextMessage(QString("S,%1").arg(this->desiredCallSign));
    }
    this->registrationStatus = RegistrationStatus::Registering;
    // this->timer.setInterval(MESSAGE_ACK_TIMEOUT_MS);
    // this->timer.start();
}
