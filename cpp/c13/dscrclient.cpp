#include "dscrclient.h"

#include <QDebug>

// https://github.com/dixonary/mfds-server/tree/main

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
DscrClient::DscrClient(QObject *parent) :
    QObject(parent)
{
    QObject::connect(&this->socket, &QWebSocket::connected, this, &DscrClient::onSocketConnected);
    QObject::connect(&this->socket, &QWebSocket::disconnected, this, &DscrClient::onSocketDisconnected);
    QObject::connect(&this->socket, &QWebSocket::textMessageReceived, this, &DscrClient::onSocketTextMessageReceived);
    QObject::connect(&this->socket, &QWebSocket::errorOccurred, this, &DscrClient::onSocketErrorOccurred);
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
    this->connectionEnabled = false;
    this->registrationEnabled = false;
    this->everRegistered = false;
    this->transportStatus = DscrClient::TransportStatus::Disconnected;
    this->registrationStatus = DscrClient::RegistrationStatus::Unregistered;
    this->socket.close(QWebSocketProtocol::CloseCodeNormal);
    this->socket.abort();
    this->timer.stop();
}

void DscrClient::register_(qint64 callSign, bool tryFastReregister) {
    qDebug() << "DscrClient::register_" << callSign << tryFastReregister;
    this->desiredCallSign = callSign;
    this->registrationEnabled = true;
    this->fastReregistrationEnabled = tryFastReregister;
    this->attemptRegister();
}

void DscrClient::sendWsTextMessage(const QString &text) {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::sendWsTextMessage" << text;
    emit this->textMessageSent(time, text);
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
        this->sendWsTextMessage(message);
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

const QUrl &DscrClient::getUrl() const {
    return this->url;
}

qint64 DscrClient::getRegisteredCallsign() const {
    return this->registeredCallSign;
}

// private slots
void DscrClient::onSocketConnected() {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onSocketConnected";
    emit this->connected(time, this->url);
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
    if(this->connectionEnabled) {
        this->attemptConnect();
    }
}
void DscrClient::onSocketTextMessageReceived(const QString &text) {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onSocketTextMessageReceived" << text;

    emit this->textMessageReceived(time, text);

    const QStringList messageContents = text.split(",");

    QChar signifier;
    if(messageContents[0].length() == 1) {
        signifier = messageContents[0][0];
    } else {
        // abort due to non-character signifier
        return;
    }

    QVector<qint64> integers;
    integers.reserve(messageContents.size() - 1);
    for(qsizetype i = 1; i < messageContents.size(); i++) {
        bool ok = false;
        qint64 val = messageContents[i].toLongLong(&ok);
        if(ok) {
            integers.append(val);
        } else {
            // abort due to non-integer
            return;
        }
    }

    switch(signifier.unicode()) {
        case 'R':
            // Received Message
            if(integers.size() >= 2) {
                // Nominally a message should be non-empty (>= 3) but we'll tolerate the empty case (== 2)
                emit this->dscrMessageReceived(time, DscrMessage {
                    time,
                    integers[0],
                    integers[1],
                    integers.mid(2)
                });
            }
            break;
        case 'K':
            // Call sign OK
            this->registrationStatus = RegistrationStatus::Registered;
            if(integers.size() >= 1) {
                // TODO should verify that the ack'd callsign was our desired one
                this->registeredCallSign = integers[0];
            }
            this->everRegistered = true;
            this->timer.stop();
            break;
        case 'E':
            // Reconnect OK
            this->registrationStatus = RegistrationStatus::Registered;
            this->everRegistered = true;
            this->timer.stop();
            break;
        case 'U':
            // Call sign in use
            this->registrationStatus = RegistrationStatus::FailedAutoregister;
            this->timer.stop();
            break;
        case 'C':
            // Call signs update
            emit this->callSignsChanged(time, integers);
            break;
    }
}

void DscrClient::onSocketErrorOccurred(QAbstractSocket::SocketError error) {
    qDebug() << "DscrClient::onSocketErrorOccurred" << error;
    this->socket.abort();
    this->transportStatus = DscrClient::TransportStatus::BackingOff;
    this->timer.setInterval(connectBackoffMs(this->numConnectFailures));
    this->numConnectFailures++;
    this->timer.start();
}

void DscrClient::onTimerTimeout() {
    if(this->transportStatus == DscrClient::TransportStatus::Connecting) {
        this->socket.abort();
        this->transportStatus = DscrClient::TransportStatus::BackingOff;
        this->timer.setInterval(connectBackoffMs(this->numConnectFailures));
        this->numConnectFailures++;
        this->timer.start();
    } else if(this->transportStatus == DscrClient::TransportStatus::BackingOff) {
        this->attemptConnect();
    } else if(this->registrationStatus == DscrClient::RegistrationStatus::Registering) {
        this->registrationStatus = RegistrationStatus::FailedAutoregister;
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
        this->sendWsTextMessage(QString("S,%1,0").arg(this->desiredCallSign));
    } else {
        this->sendWsTextMessage(QString("S,%1").arg(this->desiredCallSign));
    }
    this->registrationStatus = RegistrationStatus::Registering;
    this->timer.setInterval(MESSAGE_ACK_TIMEOUT_MS);
    this->timer.start();
}
