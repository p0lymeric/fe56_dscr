#include "models/chatmodel.h"
#include "database/transmissionprocessor.h"

ChatModel::ChatModel(QObject *parent) : QObject(parent) {
}

ChatModel::~ChatModel() {}

void ChatModel::init(TransmissionProcessor *tp, Dictionary *dictionary, EncKeySelect encKeySelect) {
    this->tp = tp;
    this->dictionary = dictionary;
    this->encKeySelect = encKeySelect;

    QObject::connect(this->tp, &TransmissionProcessor::dscrTransmissionSent, this, &ChatModel::onTransmissionProcessorDscrTransmissionSent);
    // QObject::connect(this->tp, &TransmissionProcessor::recievedSentEcho, this, &ChatModel::onTransmissionProcessorDscrTransmissionRecievedSentEcho);
    QObject::connect(this->tp, &TransmissionProcessor::dscrTransmissionReceived, this, &ChatModel::onTransmissionProcessorDscrTransmissionReceived);
    QObject::connect(this->dictionary, &Dictionary::dataChanged, this, &ChatModel::onDictionaryDataChanged);
}

QSpan<const DscrTransmissionDbLinked> ChatModel::getChatMessages() const {
    return this->chatMessages;
}

void ChatModel::onTransmissionProcessorDscrTransmissionSent(const DscrTransmissionDbLinked &trns) {
}

// void ChatModel::onTransmissionProcessorDscrTransmissionRecievedSentEcho(qint64 msgId) {}

void ChatModel::onTransmissionProcessorDscrTransmissionReceived(const DscrTransmissionDbLinked &trns) {
    if(
        (this->encKeySelect.showUnkeyed && !trns.encKey.has_value()) ||
        (this->encKeySelect.showAllKeyed && trns.encKey.has_value()) ||
        (this->encKeySelect.showKeyed && trns.encKey.has_value() && encKeySelect.encKey == *trns.encKey)
    ) {
        emit this->appendChatMessage(trns);
    }
}

void ChatModel::onDictionaryDataChanged(qsizetype /*flatIdxStart*/, qsizetype /*flatIdxEndIncl*/) {
    emit this->retranslateAll();
}
