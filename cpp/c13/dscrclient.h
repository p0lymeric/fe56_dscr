#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QWebSocket>

class DscrClient : public QObject {
    Q_OBJECT

public:
    enum class ClientState {
        Disconnected,
        Connecting,
        BackingOff,
        Unregistered,
        Registering,
        FailedAutoregister,
        Registered,
        Disconnecting
    };
    Q_ENUM(ClientState)

    explicit DscrClient(QObject *parent = nullptr);

    void open(const QUrl &url, bool enableClientPingWatchdog);
    void close();

    void register_(qint64 callSign, bool tryFastReregister);
    void sendDscrTransmission(const QVector<qint64> &symbols);

    ClientState getClientState() const;
    const QUrl &getUrl() const;

    qint64 getRegisteredCallsign() const;

signals:
    void connected(const QDateTime &time, const QUrl &url);
    void textMessageSent(const QDateTime &time, const QString &text);
    void textMessageReceived(const QDateTime &time, const QString &text);

private slots:
    void onSocketAboutToClose();
    void onSocketPong(quint64 elapsedTime, const QByteArray &payload);
    void onSocketStateChanged(QAbstractSocket::SocketState state);
    void onSocketTextMessageReceived(const QString &text);
    void onSocketErrorOccurred(QAbstractSocket::SocketError error);
    void onTimerTimeout();
    void onClientPingWatchdogTimeout();

private:
    void attemptConnect();
    void attemptRegister();
    void reset();

    void sendWsTextMessage(const QString &text);

    // Signal generators
    QWebSocket socket;
    QTimer timer;
    QTimer clientPingWatchdogTimer;

    // Configuration
    QUrl url;
    bool connectionEnabled = false;
    bool registrationEnabled = false;
    bool fastReregistrationEnabled = false;
    bool clientPingWatchdogEnabled = false;
    qint64 desiredCallSign = 0;

    // States
    ClientState clientState = ClientState::Disconnected;
    qint64 registeredCallSign = 0;
    quint32 numConnectFailures = 0;
    bool everRegistered = false;
    quint32 clientPingWatchdogPongOutstandingCycles = 0;
    quint32 numRegistrationFailures = 0;
};
