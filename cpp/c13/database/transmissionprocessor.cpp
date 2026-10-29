#include "database/transmissionprocessor.h"

TransmissionProcessor::TransmissionProcessor(QObject *parent) : QObject{parent} {
}

bool TransmissionProcessor::init(Database *db, Settings *settings) {
    this->db = db;
    this->settings = settings;

    return true;
}

// On connect:
// Load the last 512 received messages from the previous session
// When the first message is received, check if it's in the hashset
// If it's in the hashset, don't show it yet.
// If the next message is equivalent to the messages after each previous match in the hashset, continue
// Until a message is received that doesn't match
// If at that point, the match pointer points to the last message received last session, exclude all those transmissions from being memoized
// Otherwise, push those withheld messages to the chat view

// Keep a list of up to 16 chat messages that have not been echoed by the server per session

void TransmissionProcessor::onWsMessageLogConnected(const QDateTime &time, qint64 sessionId) {}
void TransmissionProcessor::onWsMessageLogDscrTransmissionSent(const QDateTime &time, qint64 msgId, const DscrMessage &msg) {}
void TransmissionProcessor::onWsMessageLogDscrTransmissionReceived(const QDateTime &time, qint64 msgId, const DscrMessage &msg) {
    switch(msg.signifier) {
        case 'R':
            // Received Message
            if(msg.integers.size() >= 2) {
                // Nominally a message should be non-empty (>= 3) but we'll tolerate the empty case (== 2)
                emit this->dscrTransmissionReceived(time, DscrTransmission {
                    time,
                    msg.integers[0],
                    msg.integers[1],
                    msg.integers.mid(2)
                });
            }
            break;
        case 'C':
            // Call signs update
            emit this->callSignsChanged(time, msg.integers);
            break;
    }
}
