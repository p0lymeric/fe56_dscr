#include "dscrtypes.h"

QString DscrMessage::encodeWsMessage() {
    QString message(this->signifier);
    for(qint64 integer : this->integers) {
        message += "," + QString::number(integer);
    }
    return message;
}

std::optional<DscrMessage> DscrMessage::decodeWsMessage(const QString &text) {
    const QStringList messageContents = text.split(",");

    char signifier;
    if(messageContents[0].length() == 1 && messageContents[0][0].unicode() < 0x80) {
        signifier = messageContents[0][0].unicode();
    } else {
        // abort due to non-character signifier
        return std::nullopt;
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
            return std::nullopt;
        }
    }

    return DscrMessage { signifier, integers };
}

// DscrMessagePacked
// Achieves about 0.4x compression on raw message text (a factor below 1x is a shrinkage)
// DSCR message strings normatively follow the pattern /^[A-Z](,-?[0-9]+)*$/.
// If a string were to match this pattern, we can compact it as follows:
// 1. Emit the signifier character as an ASCII byte (bit 7 zero and reserved)
// 2. For each integer in the message:
//    a) Represent the integer as a varint (following protobuf's sintN encoding)
//       (https://protobuf.dev/programming-guides/encoding/#varints)
//       (https://protobuf.dev/programming-guides/encoding/#signed-ints)
//       (https://lemire.me/blog/2022/11/25/making-all-your-integers-positive-with-zigzag-encoding/)
//       (https://en.wikipedia.org/wiki/LEB128#C-like_pseudocode)
//       n_varint = []
//       # n_zigzag = 2*abs(n) - (n < 0 ? 1 : 0)
//       n_zigzag = (n ASL 1) ^ (n ASR (N_WIDTH-1))
//       loop:
//           byte_i = n_zigzag & 0x7f
//           n_zigzag = n_zigzag LSR 7
//           if n_zigzag == 0:
//               n_varint += byte_i
//               break
//           else:
//               n_varint += byte_i | 0x80
//    b) Emit the varint form of the integer
QByteArray DscrMessage::encodePacked() {
    QByteArray packed;
    packed.append(this->signifier);
    for(qint64 integer : this->integers) {
        // integer is signed, so ASL and ASR
        // C++17 is stinky, so cast the left shifted term to a uint first
        quint64 zigzag = (static_cast<quint64>(integer) << 1) ^ (integer >> 63);
        while(true) {
            quint8 byte = zigzag & 0x7f;
            // zigzag is unsigned, so LSR
            zigzag >>= 7;
            if(zigzag == 0) {
                packed.append(byte);
                break;
            } else {
                packed.append(byte | 0x80);
            }
        }
    }
    return packed;
}

std::optional<DscrMessage> DscrMessage::decodePacked(const QByteArray &pack) {
    if(pack.isEmpty()) {
        return std::nullopt;
    }

    char signifier;
    QVector<qint64> integers;

    if(static_cast<quint8>(pack[0]) >= 0x80 || static_cast<quint8>(pack[0]) == ',') {
        // malformed signifier (outside of ASCII range or a comma)
        return std::nullopt;
    }

    signifier = pack[0];

    quint64 zigzag = 0;
    qsizetype runShift = 0;
    for(qsizetype i = 1; i < pack.size(); i++) {
        quint8 byte = static_cast<quint8>(pack[i]);
        if((runShift >= 63) && (byte > 1)) {
            // varint run too wide (abort if runShift == 63 and bits above 64b or the continuation bit are set)
            return std::nullopt;
        }
        zigzag |= static_cast<quint64>(byte & 0x7f) << runShift;
        runShift += 7;
        if((byte & 0x80) == 0) {
            // non-continuation
            qint64 unzigzag = static_cast<qint64>((zigzag >> 1) ^ -(zigzag & 1));
            integers.append(unzigzag);
            zigzag = 0;
            runShift = 0;
        }
    }
    if(runShift > 0) {
        // unterminated varint run
        return std::nullopt;
    }

    return DscrMessage { signifier, integers };
}

std::optional<DscrMessage> DscrMessage::decode(int kind, const QVariant &msg) {
    switch(kind) {
        case 0:
            if(msg.typeId() != QMetaType::QString) {
                return std::nullopt;
            }
            return DscrMessage::decodeWsMessage(msg.toString());
        case 1:
            if(msg.typeId() != QMetaType::QByteArray) {
                return std::nullopt;
            }
            return DscrMessage::decodePacked(msg.toByteArray());
        default:
            return std::nullopt;
    }
}

std::optional<DscrTransmission> DscrTransmission::fromDscrRMessage(const DscrMessage &msg) {
    if(msg.signifier != 'R' || msg.integers.size() < 2) {
        return std::nullopt;
    }
    // Nominally a message should be non-empty (>= 3) but we'll tolerate the empty case (== 2)
    return DscrTransmission {
        msg.integers[0],
        msg.integers[1],
        msg.integers.mid(2)
    };
}

std::optional<DscrTransmission> DscrTransmission::fromDscrMMessage(const DscrMessage &msg, qint64 from) {
    if(msg.signifier != 'M') {
        return std::nullopt;
    }
    return DscrTransmission {
        from,
        std::nullopt,
        msg.integers
    };
}
