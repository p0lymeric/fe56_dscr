#pragma once

#include "database/dictionary.h"
#include "database/transmissionprocessor.h"

#include <QObject>
#include <qdatetime.h>
#include <qspan.h>

struct EncKeySelect {
    bool showUnkeyed;
    bool showAllKeyed;
    bool showKeyed;
    qint64 encKey;
};

class ChatModel : public QObject {
    Q_OBJECT

public:
    explicit ChatModel(QObject *parent = nullptr);
    ~ChatModel();

    void init(TransmissionProcessor *tp, Dictionary *dictionary, EncKeySelect encKeySelect);

    QSpan<const DscrTransmissionDbLinked> getChatMessages() const;

public slots:
    void onTransmissionProcessorDscrTransmissionSent(const DscrTransmissionDbLinked &trns);
    // void onTransmissionProcessorDscrTransmissionRecievedSentEcho(qint64 msgId);
    void onTransmissionProcessorDscrTransmissionReceived(const DscrTransmissionDbLinked &trns);
    void onDictionaryDataChanged(qsizetype flatIdxStart, qsizetype flatIdxEndIncl);

signals:
    void appendChatMessage(const DscrTransmissionDbLinked &trns);
    // void removeChatMessage(qint64 msgId);
    void retranslateAll();

private:
    TransmissionProcessor *tp;
    Dictionary *dictionary;
    EncKeySelect encKeySelect;

    QVector<DscrTransmissionDbLinked> chatMessages;
};
