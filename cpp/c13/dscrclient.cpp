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
static const int CLIENT_PING_CHECK_MS = 15000;
static const int CLIENT_PING_TIMEOUT_CYCLE = 2; // 30 seconds
static const int AUTOREGISTER_MAX_REATTEMPTS = 1; // 2 attempts
static const int AUTOREGISTER_RETRY_MS = 30000;

// public
DscrClient::DscrClient(QObject *parent) :
    QObject(parent)
{
    QObject::connect(&this->socket, &QWebSocket::aboutToClose, this, &DscrClient::onSocketAboutToClose);
    QObject::connect(&this->socket, &QWebSocket::pong, this, &DscrClient::onSocketPong);
    QObject::connect(&this->socket, &QWebSocket::stateChanged, this, &DscrClient::onSocketStateChanged);
    QObject::connect(&this->socket, &QWebSocket::textMessageReceived, this, &DscrClient::onSocketTextMessageReceived);
    QObject::connect(&this->socket, &QWebSocket::errorOccurred, this, &DscrClient::onSocketErrorOccurred);
    QObject::connect(&this->timer, &QTimer::timeout, this, &DscrClient::onTimerTimeout);
    QObject::connect(&this->clientPingWatchdogTimer, &QTimer::timeout, this, &DscrClient::onClientPingWatchdogTimeout);
    this->timer.setSingleShot(true);
}

void DscrClient::open(const QUrl &url, bool enableClientPingWatchdog) {
    qDebug() << "DscrClient::open" << url;
    if(this->clientState == DscrClient::ClientState::Disconnected) {
        this->connectionEnabled = true;
        this->clientPingWatchdogEnabled = enableClientPingWatchdog;
        this->url = url;
        this->numConnectFailures = 0;
        this->attemptConnect();
    }
}

void DscrClient::close() {
    qDebug() << "DscrClient::close";
    if(
        this->clientState != DscrClient::ClientState::Disconnected &&
        this->clientState != DscrClient::ClientState::Disconnecting
    ) {
        this->reset();
        this->clientState = DscrClient::ClientState::Disconnecting;
        this->socket.close(QWebSocketProtocol::CloseCodeNormal);
        this->timer.setInterval(MESSAGE_ACK_TIMEOUT_MS);
        this->timer.start();
        this->clientPingWatchdogTimer.stop();
    }
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
    if(this->clientState != DscrClient::ClientState::Registered) {
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

DscrClient::ClientState DscrClient::getClientState() const {
    return this->clientState;
}

const QUrl &DscrClient::getUrl() const {
    return this->url;
}

qint64 DscrClient::getRegisteredCallsign() const {
    return this->registeredCallSign;
}

// private slots
void DscrClient::onSocketAboutToClose() {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onSocketAboutToClose" << this->socket.closeCode() << this->socket.closeReason();
}

void DscrClient::onSocketPong(quint64 elapsedTime, const QByteArray &payload) {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onSocketPong" << elapsedTime << payload;

    this->clientPingWatchdogPongOutstandingCycles = 0;
}

void DscrClient::onSocketStateChanged(QAbstractSocket::SocketState state) {
    const QDateTime time = QDateTime::currentDateTime();

    switch(state) {
        case QAbstractSocket::UnconnectedState: {
            qDebug() << time << "DscrClient::onSocketStateChanged" << state;
            // triggered on disconnected
            if(this->connectionEnabled) {
                this->clientState = DscrClient::ClientState::BackingOff;
                this->timer.setInterval(connectBackoffMs(this->numConnectFailures));
                this->numConnectFailures++;
                this->timer.start();
            } else if(this->clientState == DscrClient::ClientState::Disconnecting) {
                // graceful disconnect from close()
                this->clientState = DscrClient::ClientState::Disconnected;
                this->timer.stop();
            }
            this->clientPingWatchdogTimer.stop();
            this->numRegistrationFailures = 0;
            break;
        }
        case QAbstractSocket::ConnectedState: {
            qDebug() << time << "DscrClient::onSocketStateChanged" << state << this->socket.localAddress() << this->socket.localPort() << this->socket.peerAddress() << this->socket.peerPort();
            // triggered on connected
            emit this->connected(time, this->url);
            this->clientState = DscrClient::ClientState::Unregistered;
            this->numConnectFailures = 0;
            this->timer.stop();
            if(this->registrationEnabled) {
                this->attemptRegister();
            }
            if(this->clientPingWatchdogEnabled) {
                this->clientPingWatchdogPongOutstandingCycles = 0;
                this->clientPingWatchdogTimer.setInterval(CLIENT_PING_CHECK_MS);
                this->clientPingWatchdogTimer.start();
            }
            break;
        }
        default:
            qDebug() << time << "DscrClient::onSocketStateChanged" << state;
            break;
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
            this->clientState = DscrClient::ClientState::Registered;
            if(integers.size() >= 1) {
                // TODO should verify that the ack'd callsign was our desired one
                this->registeredCallSign = integers[0];
            }
            this->everRegistered = true;
            this->numRegistrationFailures = 0;
            this->timer.stop();
            break;
        case 'E':
            // Reconnect OK
            this->clientState = DscrClient::ClientState::Registered;
            this->everRegistered = true;
            this->numRegistrationFailures = 0;
            this->timer.stop();
            break;
        case 'U':
            // Call sign in use
            this->timer.stop();
            if(this->numRegistrationFailures < AUTOREGISTER_MAX_REATTEMPTS) {
                this->clientState = DscrClient::ClientState::Unregistered;
                this->numRegistrationFailures++;
                this->timer.setInterval(AUTOREGISTER_RETRY_MS);
                this->timer.start();
            } else {
                this->clientState = DscrClient::ClientState::FailedAutoregister;
            }
            break;
        case 'C':
            // Call signs update
            emit this->callSignsChanged(time, integers);
            break;
    }
}

void DscrClient::onSocketErrorOccurred(QAbstractSocket::SocketError error) {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onSocketErrorOccurred" << error << this->socket.errorString();
}

void DscrClient::onTimerTimeout() {
    const QDateTime time = QDateTime::currentDateTime();
    qDebug() << time << "DscrClient::onTimerTimeout";

    switch(this->clientState) {
        case ClientState::Connecting: {
            // control should flow to onSocketStateChanged, where backoff is incremented
            this->socket.abort();
            break;
        }
        case ClientState::BackingOff: {
            // try again
            this->attemptConnect();
            break;
        }
        case ClientState::Unregistered: {
            // we received U but want to retry in case the server doesn't know we recently dropped
            this->attemptRegister();
            break;
        }
        case ClientState::Registering: {
            // the server did not acknowledge our connection message at all
            this->clientState = DscrClient::ClientState::FailedAutoregister;
            break;
        }
        case ClientState::Disconnecting: {
            // we tried to gracefully disconnect and it failed
            this->socket.abort();
            this->clientState = DscrClient::ClientState::Disconnected;
            break;
        }
        default:
            break;
    }
}

void DscrClient::onClientPingWatchdogTimeout() {
    const QDateTime time = QDateTime::currentDateTime();

    if(this->clientPingWatchdogPongOutstandingCycles == 0) {
        qDebug() << time << "DscrClient::onClientPingWatchdogTimeout" << "ping!";
        this->socket.ping();
    } else if(this->clientPingWatchdogPongOutstandingCycles < CLIENT_PING_TIMEOUT_CYCLE) {
        qDebug() << time << "DscrClient::onClientPingWatchdogTimeout" << "no pong" << this->clientPingWatchdogPongOutstandingCycles;
    } else {
        qDebug() << time << "DscrClient::onClientPingWatchdogTimeout" << "pong timeout!";
        this->socket.abort();
    }
    this->clientPingWatchdogPongOutstandingCycles++;
}

// private
void DscrClient::attemptConnect() {
    this->socket.open(this->url);
    this->clientState = DscrClient::ClientState::Connecting;
    this->timer.setInterval(CONNECT_TIMEOUT_MS);
    this->timer.start();
}

void DscrClient::attemptRegister() {
    if(
        this->clientState != DscrClient::ClientState::Unregistered &&
        this->clientState != DscrClient::ClientState::FailedAutoregister &&
        this->clientState != DscrClient::ClientState::Registered
    ) {
        return;
    }
    if(false /*this->fastReregistrationEnabled*/) {
        // TODO need to track time disconnected
        this->sendWsTextMessage(QString("S,%1,0").arg(this->desiredCallSign));
    } else {
        this->sendWsTextMessage(QString("S,%1").arg(this->desiredCallSign));
    }
    this->clientState = DscrClient::ClientState::Registering;
    this->timer.setInterval(MESSAGE_ACK_TIMEOUT_MS);
    this->timer.start();
}

void DscrClient::reset() {
    this->url.clear();
    this->connectionEnabled = false;
    this->registrationEnabled = false;
    this->fastReregistrationEnabled = false;
    this->clientPingWatchdogEnabled = false;
    this->desiredCallSign = 0;

    this->clientState = DscrClient::ClientState::Disconnected;
    this->registeredCallSign = 0;
    this->numConnectFailures = 0;
    this->everRegistered = false;
    this->clientPingWatchdogPongOutstandingCycles = 0;
    this->numRegistrationFailures = 0;
}
