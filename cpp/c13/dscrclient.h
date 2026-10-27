#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QWebSocket>

struct DscrMessage {
    QDateTime time;
    qint64 from;
    qint64 sequenceNum;
    QVector<qint64> symbols;
};

class DscrClient : public QObject {
    Q_OBJECT

public:
    enum class ClientState { Disconnected, Connecting, BackingOff, Unregistered, Registering, FailedAutoregister, Registered, Disconnecting };
    Q_ENUM(ClientState)

    explicit DscrClient(QObject *parent = nullptr);

    void open(const QUrl &url);
    void close();


    void register_(qint64 callSign, bool tryFastReregister);
    void sendWsTextMessage(const QString &text);
    void sendDscrMessage(const QVector<qint64> &symbols);

    ClientState getClientState() const;
    const QUrl &getUrl() const;

    qint64 getRegisteredCallsign() const;

signals:
    void connected(const QDateTime &time, const QUrl &url);
    void dscrMessageReceived(const QDateTime &time, const DscrMessage &msg);
    void callSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);
    void textMessageSent(const QDateTime &time, const QString &text);
    void textMessageReceived(const QDateTime &time, const QString &text);

private slots:
    void onSocketPong(quint64 elapsedTime, const QByteArray &payload);
    void onSocketStateChanged(QAbstractSocket::SocketState state);
    void onSocketTextMessageReceived(const QString &text);
    void onSocketErrorOccurred(QAbstractSocket::SocketError error);
    void onTimerTimeout();

private:
    void attemptConnect();
    void attemptRegister();
    void reset();

    // Signal generators
    QWebSocket socket;
    QTimer timer;

    // Configuration
    QUrl url;
    bool connectionEnabled = false;
    bool registrationEnabled = false;
    bool fastReregistrationEnabled = false;
    qint64 desiredCallSign = 0;

    // States
    ClientState clientState = ClientState::Disconnected;
    qint64 registeredCallSign = 0;
    quint32 numConnectFailures = 0;
    bool everRegistered = false;
};
