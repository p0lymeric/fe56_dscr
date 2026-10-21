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
    enum class TransportStatus { Disconnected, Connecting, BackingOff, Connected };
    Q_ENUM(TransportStatus)
    enum class RegistrationStatus { Unregistered, Registering, FailedAutoregister, Registered };
    Q_ENUM(RegistrationStatus)

    explicit DscrClient(QObject *parent = nullptr);

    void open(const QUrl &url);
    void close();

    void register_(qint64 callSign, bool tryFastReregister);
    void sendWsTextMessage(const QString &text);
    void sendDscrMessage(const QVector<qint64> &symbols);

    TransportStatus getTransportStatus() const;
    RegistrationStatus getRegistrationStatus() const;
    bool isConnectedAndRegistered() const;

    qint64 getRegisteredCallsign() const;

signals:
    void stateChanged();
    void dscrMessageReceived(const QDateTime &time, qint64 from, qint64 sequenceNum, const QVector<qint64> &symbols);
    void callSignsChanged(const QDateTime &time, const QVector<qint64> &callSigns);
    void textMessageReceived(const QDateTime &time, const QString &text);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketTextMessageReceived(const QString &text);
    void onSocketErrorOccurred(QAbstractSocket::SocketError error);
    void onTimerTimeout();

private:
    void attemptConnect();
    void attemptRegister();

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
    TransportStatus transportStatus = TransportStatus::Disconnected;
    RegistrationStatus registrationStatus = RegistrationStatus::Unregistered;
    qint64 registeredCallSign = 0;
    quint32 numConnectFailures = 0;
    bool everRegistered = false;
};
