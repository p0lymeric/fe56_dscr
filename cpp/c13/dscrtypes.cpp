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

std::optional<DscrSpecialEncrypted> DscrSpecialEncrypted::find(QSpan<const qint64> symbols) {
    if(symbols.size() >= 2 && symbols[0] == DscrSpecialEncrypted::Symbol::EncryptedMessage) {
        return DscrSpecialEncrypted { symbols[1] };
    }
    return std::nullopt;
}

static float asDecimalFraction(qint64 nonNegativeInt) {
    // Known issue: It's not possible to represent decimals with fractional components that start with 0,
    // due to how DSCR clients canonically pack numbers
    float scaleFactor = 1.0f;
    while(scaleFactor <= float(nonNegativeInt)) {
        scaleFactor *= 10.0f;
    }
    return float(nonNegativeInt) / scaleFactor;
}

static std::optional<float> consumeUnsignedFloat(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size()) {
        return std::nullopt;
    }

    if(symbols[cur2] == DscrSpecialPlot::Symbol::Radix) {
        // Radix
        cur2++;
        if(cur2 >= symbols.size()) {
            return std::nullopt;
        }
        // Radix NONNEGATIVE_INTEGER
        if(symbols[cur2] >= 0) {
            float accumulator = asDecimalFraction(symbols[cur2]);
            cur2++;
            cur = cur2;
            return accumulator;
        } else {
            return std::nullopt;
        }
    } else if(symbols[cur2] >= 0) {
        // NONNEGATIVE_INTEGER
        float accumulator = symbols[cur2];
        cur2++;
        if(cur2 >= symbols.size()) {
            cur = cur2;
            return accumulator;
        }
        // NONNEGATIVE_INTEGER Radix
        if(symbols[cur2] == DscrSpecialPlot::Symbol::Radix) {
            cur2++;
            if(cur2 >= symbols.size()) {
                cur = cur2;
                return accumulator;
            }
            if(symbols[cur2] >= 0) {
                // NONNEGATIVE_INTEGER Radix NONNEGATIVE_INTEGER
                accumulator += asDecimalFraction(symbols[cur2]);
                cur2++;
                cur = cur2;
                return accumulator;
            } else {
                cur = cur2;
                return accumulator;
            }
        } else {
            cur = cur2;
            return accumulator;
        }
    } else {
        return std::nullopt;
    }
}

static std::optional<float> consumeSignedFloat(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size()) {
        return std::nullopt;
    }

    bool negate = false;
    if(symbols[cur2] == DscrSpecialPlot::Symbol::UnaryMinus) {
        // UnaryMinus
        negate = true;
        cur2++;
        if(cur2 >= symbols.size()) {
            return std::nullopt;
        }
    }

    auto maybeUnsignedNumber = consumeUnsignedFloat(symbols, cur2);
    if(maybeUnsignedNumber.has_value()) {
        cur = cur2;
        return negate ? -*maybeUnsignedNumber : *maybeUnsignedNumber;
    }
    return std::nullopt;
}

static bool consumeSep(QSpan<const qint64> &symbols, qsizetype &cur) {
    if(cur >= symbols.size() || symbols[cur] != DscrSpecialPlot::Symbol::Sep) {
        return false;
    }

    cur++;
    return true;
}

static std::optional<DscrSpecialPlot::Orb> consumeOrb(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialPlot::Symbol::OrbSym) {
        return std::nullopt;
    }
    cur2++;

    DscrSpecialPlot::Orb orb;
    auto maybeX = consumeSignedFloat(symbols, cur2);
    if(maybeX.has_value() && consumeSep(symbols, cur2)) {
        orb.x = *maybeX;
    } else {
        return std::nullopt;
    }
    auto maybeZ = consumeSignedFloat(symbols, cur2);
    if(maybeZ.has_value() && consumeSep(symbols, cur2)) {
        orb.z = -*maybeZ;
    } else {
        return std::nullopt;
    }
    auto maybeY = consumeSignedFloat(symbols, cur2);
    if(maybeY.has_value() && consumeSep(symbols, cur2)) {
        orb.y = *maybeY;
    } else {
        return std::nullopt;
    }
    auto maybeR = consumeUnsignedFloat(symbols, cur2);
    if(maybeR.has_value() && consumeSep(symbols, cur2)) {
        orb.d = *maybeR;
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        orb.colourIdx = symbols[cur2];
        cur2++;
    } else {
        return std::nullopt;
    }

    cur = cur2;
    return orb;
}

std::optional<DscrSpecialPlot> DscrSpecialPlot::fromSymbols(QSpan<const qint64> symbols, bool endMustTerminate, qsizetype *endWritebackNullable) {
    // PLOT := Plot Begin End
    //       | Plot Begin ORB (Sep ORB)* Sep? End
    // ORB := Orb SNUMBER Sep SNUMBER Sep SNUMBER Sep UNUMBER Sep NONNEGATIVE_INTEGER
    // UNUMBER := NONNEGATIVE_INTEGER Radix?
    //          | Radix NONNEGATIVE_INTEGER
    //          | NONNEGATIVE_INTEGER Radix NONNEGATIVE_INTEGER
    // SNUMBER := UnaryMinus? UNUMBER

    if(symbols.size() < 3) {
        return std::nullopt;
    }
    if(symbols[0] != DscrSpecialPlot::Symbol::Plot || symbols[1] != DscrSpecialPlot::Symbol::Begin) {
        return std::nullopt;
    }
    QVector<DscrSpecialPlot::Orb> orbs;
    qsizetype cur = 2;
    if(auto maybeOrb = consumeOrb(symbols, cur); maybeOrb.has_value()) {
        orbs.append(*maybeOrb);
        while(cur < symbols.size() && symbols[cur] == DscrSpecialPlot::Symbol::Sep) {
            cur++;
            if(cur < symbols.size() && symbols[cur] == DscrSpecialPlot::Symbol::End) {
                // allow trailing Sep (C/Python-like semantics)
                break;
            }
            auto maybeOrb2 = consumeOrb(symbols, cur);
            if(!maybeOrb2.has_value()) {
                return std::nullopt;
            }
            orbs.append(*maybeOrb2);
        }
    }
    bool endCondition = endMustTerminate ? cur != symbols.size() - 1 : cur >= symbols.size();
    if(endCondition || symbols[cur] != DscrSpecialPlot::Symbol::End) {
        return std::nullopt;
    }
    if(endWritebackNullable != nullptr) {
        *endWritebackNullable = cur + 1;
    }
    return DscrSpecialPlot { orbs };
}

QVector<DscrSpecialPlot> DscrSpecialPlot::findAll(QSpan<const qint64> symbols) {
    // tricksy, tricksy hobbitses...
    QVector<DscrSpecialPlot> orbses;

    for(qsizetype i = 0; i < symbols.size(); i++) {
        qsizetype end = 0;
        if(auto orbs = DscrSpecialPlot::fromSymbols(symbols.sliced(i), false, &end); orbs.has_value()) {
            orbses.append(*orbs);
            i += end;
        } else {
            i++;
        }
    }

    return orbses;
}

QVector<DscrSpecialSequence> DscrSpecialSequence::findAll(QSpan<const qint64> symbols) {
    QVector<DscrSpecialSequence> specs;

    qsizetype i = 0;
    if(auto encrypted = DscrSpecialEncrypted::find(symbols); encrypted.has_value()) {
        specs.append(DscrSpecialSequence { 0, 2, *encrypted });
        // Don't match |-53 as the start of a plot if it immediately follows |-65535
        i = 2;
    }

    while(i < symbols.size()) {
        qsizetype end = 0;
        if(auto orbs = DscrSpecialPlot::fromSymbols(symbols.sliced(i), false, &end); orbs.has_value()) {
            specs.append(DscrSpecialSequence { i, i + end, std::move(*orbs) });
            i += end;
        } else {
            i++;
        }
    }

    return specs;
}
