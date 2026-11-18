#include <cmath>
#include <optional>
#include "protocol/dscrtypes.h"

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

static float asDecimalFraction(qint64 nonNegativeInt, qint64 leadingZeroes) {
    float scaleFactor = 1.0f;
    while(scaleFactor <= float(nonNegativeInt)) {
        scaleFactor *= 10.0f;
    }
    scaleFactor *= std::pow(10.0f, leadingZeroes);
    return float(nonNegativeInt) / scaleFactor;
}

static std::optional<float> consumeFraction(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;

    int leadingZeroes = 0;
    while(cur2 < symbols.size() && symbols[cur2] == 0) {
        leadingZeroes++;
        cur2++;
    }

    if(cur2 < symbols.size() && symbols[cur2] > 0) {
        cur = cur2 + 1;
        return asDecimalFraction(symbols[cur2], leadingZeroes);
    } else if(leadingZeroes > 0) {
        cur = cur2;
        return 0.0f;
    }

    return std::nullopt;
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
        // Radix (ZERO)* NONNEGATIVE_INTEGER
        if(auto fraction = consumeFraction(symbols, cur2); fraction.has_value()) {
            cur = cur2;
            return fraction;
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
            if(auto fraction = consumeFraction(symbols, cur2); fraction.has_value()) {
                // NONNEGATIVE_INTEGER Radix (ZERO)* NONNEGATIVE_INTEGER
                accumulator += *fraction;
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

static std::optional<DscrSpecialMusic::Note> consumeNote(
    QSpan<const qint64> symbols,
    qsizetype &cur
) {
    qsizetype cur2 = cur;
    DscrSpecialMusic::Note note;

    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialMusic::Symbol::NoteSym) {
        return std::nullopt;
    }
    cur2++;

    auto maybeStartTime = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        note.startTime = maybeStartTime.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeNoteDuration = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        note.noteDuration = maybeNoteDuration.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeFreq = consumeUnsignedFloat(symbols, cur2);
    note.freq = maybeFreq.value_or(0);

    cur = cur2;
    return note;
}

static std::optional<DscrSpecialMusic::Sequence> consumeGroup(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
);

static std::optional<qint64> consumeVar(
    QSpan<const qint64> symbols,
    qsizetype &cur
) {
    qsizetype cur2 = cur;

    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialMusic::Symbol::X) {
        return std::nullopt;
    }
    cur2++;

    if(cur2 >= symbols.size() || symbols[cur2] < 0) {
        return std::nullopt;
    } else {
        cur = cur2 + 1;
        return symbols[cur2];
    }
}

static std::optional<DscrSpecialMusic::Sequence> consumeVarDeclOrVarRefAsSequence(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
) {
    qsizetype cur2 = cur;

    if(auto var = consumeVar(symbols, cur2); var.has_value()) {
        // mildly context-sensitive
        // if a VAR is already declared, don't parse a following group if it exists
        if(auto it = vars.constFind(*var); it != vars.constEnd()) {
            cur = cur2;
            return it.value();
        } else if(auto group = consumeGroup(symbols, cur2, vars, recursionDepth); group.has_value()) {
            vars.insert(*var, *group);
            cur = cur2;
            return group;
        }
    }

    return std::nullopt;
}

static std::optional<DscrSpecialMusic::Item> consumeItem(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
) {
    qsizetype cur2 = cur;
    DscrSpecialMusic::Item item;

    if(auto note = consumeNote(symbols, cur2); note.has_value()) {
        item.item = *note;
    } else if (auto seq = consumeVarDeclOrVarRefAsSequence(symbols, cur2, vars, recursionDepth); seq.has_value()) {
        item.item = *seq;
    } else if (auto group = consumeGroup(symbols, cur2, vars, recursionDepth); group.has_value()) {
        item.item = *group;
    } else {
        return std::nullopt;
    }

    if(cur2 < symbols.size() && symbols[cur2] == DscrSpecialMusic::Symbol::Sep) {
        cur2++;
    }

    cur = cur2;
    return item;
}

static QVector<DscrSpecialMusic::Item> consumeChord(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
) {
    qsizetype cur2 = cur;
    QVector<DscrSpecialMusic::Item> items;

    while(true) {
        if(auto item = consumeItem(symbols, cur2, vars, recursionDepth); item.has_value()) {
            items.append(*item);
        } else {
            break;
        }
    }

    cur = cur2;
    return items;
}

static DscrSpecialMusic::Sequence consumeSequence(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
) {
    qsizetype cur2 = cur;
    QVector<DscrSpecialMusic::Item> items;

    items += consumeChord(symbols, cur2, vars, recursionDepth);

    while(cur2 < symbols.size() && symbols[cur2] == DscrSpecialMusic::Symbol::BeforeSym) {
        cur2++;
        items += { DscrSpecialMusic::Before{} };
        items += consumeChord(symbols, cur2, vars, recursionDepth);
    }

    cur = cur2;
    return { std::make_shared<const QVector<DscrSpecialMusic::Item>>(items) };
}

static std::optional<DscrSpecialMusic::Sequence> consumeGroup(
    QSpan<const qint64> symbols,
    qsizetype &cur,
    QHash<qint64, DscrSpecialMusic::Sequence> &vars,
    qsizetype recursionDepth
) {
    if(recursionDepth >= DscrSpecialMusic::RECURSION_LIMIT) {
        return std::nullopt;
    }
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialMusic::Symbol::Begin) {
        return std::nullopt;
    }
    cur2++;

    auto seq = consumeSequence(symbols, cur2, vars, recursionDepth + 1);

    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialMusic::Symbol::End) {
        return std::nullopt;
    }
    cur2++;

    cur = cur2;
    return seq;
}

std::optional<DscrSpecialMusic> DscrSpecialMusic::fromSymbols(QSpan<const qint64> symbols, bool endMustTerminate, qsizetype *endWritebackNullable) {
    // Grammar based off 3544's implementation
    // https://github.com/electraminer/relay3544/blob/main/src/spoilers/Song.ts
    // MUSIC := Music GROUP
    // GROUP := Begin SEQUENCE End
    // SEQUENCE := CHORD (Before CHORD)*
    // ITEM := (NOTE | VARDECL | VAR | GROUP) Sep?
    // CHORD := ITEM*
    // VARDECL := VAR GROUP
    // VAR := X Nonnegative
    // NOTE := Note UNUMBER? Sep UNUMBER? Sep UNUMBER?
    // UNUMBER := Nonnegative Radix?
    //          | Radix FRACTION
    //          | Nonnegative Radix FRACTION
    // FRACTION := (Zero)* Nonnegative
    // Nonnegative := ">=0"
    // Zero := "0"
    // Sep := "-3"
    // Radix := "-10"
    // X := "-11"
    // Begin := "-14"
    // End := "-15"
    // Before := "-122"
    // Music := "-577"
    // NoteSym := "-605003"

    if(symbols.size() < 1) {
        return std::nullopt;
    }

    if(symbols[0] != DscrSpecialMusic::Symbol::Music) {
        return std::nullopt;
    }

    QHash<qint64, DscrSpecialMusic::Sequence> vars;
    qsizetype cur = 1;
    if(auto music = consumeGroup(symbols, cur, vars, 0)) {
        if(endMustTerminate && cur != symbols.size()) {
            return std::nullopt;
        }
        if(endWritebackNullable != nullptr) {
            *endWritebackNullable = cur;
        }
        return DscrSpecialMusic { *music };
    }

    return std::nullopt;
}

static std::optional<DscrSpecialPlot::Orb> consumeOrb(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialPlot::Symbol::OrbSym) {
        return std::nullopt;
    }
    cur2++;

    DscrSpecialPlot::Orb orb;
    auto maybeX = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        orb.x = maybeX.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZ = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        orb.z = -maybeZ.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeY = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        orb.y = maybeY.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeD = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        orb.d = maybeD.value_or(0);
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        orb.colourIdx = symbols[cur2];
        cur2++;
    } else {
        orb.colourIdx = 0;
    }

    cur = cur2;
    return orb;
}

static std::optional<DscrSpecialPlot::Seg> consumeSeg(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialPlot::Symbol::SegSym) {
        return std::nullopt;
    }
    cur2++;

    DscrSpecialPlot::Seg seg;
    auto maybeX0 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.x0 = maybeX0.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZ0 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.z0 = -maybeZ0.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeY0 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.y0 = maybeY0.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeX1 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.x1 = maybeX1.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZ1 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.z1 = -maybeZ1.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeY1 = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        seg.y1 = maybeY1.value_or(0);
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        seg.colourIdx = symbols[cur2];
        cur2++;
    } else {
        seg.colourIdx = 0;
    }

    cur = cur2;
    return seg;
}

static std::optional<DscrSpecialPlot::Box> consumeRectPrism(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialPlot::Symbol::RectPrismSym) {
        return std::nullopt;
    }
    cur2++;

    DscrSpecialPlot::Box box;
    auto maybeX = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.x = maybeX.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZ = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.z = -maybeZ.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeY = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.y = maybeY.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeXN = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.xn = maybeXN.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZN = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.zn = maybeZN.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeYN = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.yn = maybeYN.value_or(0);
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        box.colourIdx = symbols[cur2];
        cur2++;
    } else {
        box.colourIdx = 0;
    }

    cur = cur2;
    return box;
}

static std::optional<DscrSpecialPlot::Box> consumeCube(QSpan<const qint64> symbols, qsizetype &cur) {
    qsizetype cur2 = cur;
    if(cur2 >= symbols.size() || symbols[cur2] != DscrSpecialPlot::Symbol::CubeSym) {
        return std::nullopt;
    }
    cur2++;

    DscrSpecialPlot::Box box;
    auto maybeX = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.x = maybeX.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeZ = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.z = -maybeZ.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeY = consumeSignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.y = maybeY.value_or(0);
    } else {
        return std::nullopt;
    }
    auto maybeN = consumeUnsignedFloat(symbols, cur2);
    if(consumeSep(symbols, cur2)) {
        box.xn = maybeN.value_or(0);
        box.zn = maybeN.value_or(0);
        box.yn = maybeN.value_or(0);
    } else {
        return std::nullopt;
    }
    if(cur2 < symbols.size() && symbols[cur2] >= 0) {
        box.colourIdx = symbols[cur2];
        cur2++;
    } else {
        box.colourIdx = 0;
    }

    cur = cur2;
    return box;
}

static bool consumeShape(QSpan<const qint64> symbols, qsizetype &cur, DscrSpecialPlot &dsp) {
    qsizetype cur2 = cur;
    if(auto orb = consumeOrb(symbols, cur2); orb.has_value()) {
        dsp.orbs.append(*orb);
        cur = cur2;
        return true;
    } else if(auto seg = consumeSeg(symbols, cur2); seg.has_value()) {
        dsp.segs.append(*seg);
        cur = cur2;
        return true;
    } else if(auto box = consumeRectPrism(symbols, cur2); box.has_value()) {
        dsp.boxes.append(*box);
        cur = cur2;
        return true;
    } else if(auto box = consumeCube(symbols, cur2); box.has_value()) {
        dsp.boxes.append(*box);
        cur = cur2;
        return true;
    }

    return false;
}

std::optional<DscrSpecialPlot> DscrSpecialPlot::fromSymbols(QSpan<const qint64> symbols, bool endMustTerminate, qsizetype *endWritebackNullable) {
    // Highly tolerant grammar to match some other client behaviours
    // PLOT := Plot Begin End
    //       | Plot Begin SHAPE (Sep? SHAPE)* Sep? End
    // SHAPE := ORB | SEG | RECTPRISM | CUBE
    // ORB := Orb SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep UNUMBER? Sep Nonnegative?
    // SEG := Orb SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep Nonnegative?
    // RECTPRISM := Orb SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep UNUMBER? Sep UNUMBER? Sep UNUMBER? Sep Nonnegative?
    // CUBE := Orb SNUMBER? Sep SNUMBER? Sep SNUMBER? Sep UNUMBER? Sep Nonnegative?
    // UNUMBER := Nonnegative Radix?
    //          | Radix FRACTION
    //          | Nonnegative Radix FRACTION
    // FRACTION := (Zero)* Nonnegative
    // SNUMBER := UnaryMinus? UNUMBER
    // Nonnegative := ">=0"
    // Zero := "0"
    // UnaryMinus := "-1"
    // Sep := "-3"
    // Radix := "-10"
    // Begin := "-14"
    // End := "-15"
    // Orb := "-52"
    // Plot := "-53"
    // SegSym = -326,
    // RectPrismSym = -157442,
    // CubeSym = -524000,

    // Additional primitives based off 1574's extensions
    // 223  1574
    // POINT IS SPHERE TABL ; ;
    // POINT XX , YY , ZZ , NVOL , COLOUR ; ;
    // lineseg IS SEGM TABL ; ;
    // lineseg XX 0, YY 0, ZZ 0, XX 1, YY 1, ZZ 1, COLOUR => SEGM ( VECT 0 ( XX 0, YY 0, ZZ 0 ) , VECT 1 ( XX 1, YY 1, ZZ 1 ) ) ; ;
    // cube IS cube ; ;
    // cube XX , YY , ZZ , XX NORM , COLOUR ; ;
    // rectprism IS rectprism ; ;
    // rectprism XX , YY , ZZ , XX NORM , YY NORM , ZZ NORM , COLOUR

    // |-49|-100|-403|-3|-50|-100|-418|-3|-51|-100|-419|-2|-2|-53|-14|-52|0|-3|0|-3|0|-3|1|-3|65|-3|-52|0|-3|0|-3|2|-3|1|-3|66|-3|-52|0|-3|0|-3|4|-3|1|-3|67|-3|-52|0|-3|0|-3|6|-3|1|-3|68|-3|-52|0|-3|2|-3|0|-3|1|-3|69|-3|-52|0|-3|2|-3|2|-3|1|-3|70|-3|-52|0|-3|2|-3|4|-3|1|-3|71|-3|-52|0|-3|2|-3|6|-3|1|-3|72|-3|-52|0|-3|4|-3|0|-3|1|-3|73|-3|-52|0|-3|4|-3|2|-3|1|-3|74|-3|-52|0|-3|4|-3|4|-3|1|-3|75|-3|-52|0|-3|4|-3|6|-3|1|-3|76|-3|-52|0|-3|6|-3|0|-3|1|-3|77|-3|-52|0|-3|6|-3|2|-3|1|-3|78|-3|-52|0|-3|6|-3|4|-3|1|-3|79|-3|-52|0|-3|6|-3|6|-3|1|-3|80|-3|-52|2|-3|0|-3|0|-3|1|-3|81|-3|-52|2|-3|0|-3|2|-3|1|-3|82|-3|-52|2|-3|0|-3|4|-3|1|-3|83|-3|-52|2|-3|0|-3|6|-3|1|-3|84|-3|-52|2|-3|2|-3|0|-3|1|-3|85|-3|-52|2|-3|2|-3|2|-3|1|-3|86|-3|-52|2|-3|2|-3|4|-3|1|-3|87|-3|-52|2|-3|2|-3|6|-3|1|-3|88|-3|-52|2|-3|4|-3|0|-3|1|-3|89|-3|-52|2|-3|4|-3|2|-3|1|-3|90|-3|-52|2|-3|4|-3|4|-3|1|-3|91|-3|-52|2|-3|4|-3|6|-3|1|-3|92|-3|-52|2|-3|6|-3|0|-3|1|-3|93|-3|-52|2|-3|6|-3|2|-3|1|-3|94|-3|-52|2|-3|6|-3|4|-3|1|-3|95|-3|-52|2|-3|6|-3|6|-3|1|-3|96|-3|-52|4|-3|0|-3|0|-3|1|-3|97|-3|-52|4|-3|0|-3|2|-3|1|-3|98|-3|-52|4|-3|0|-3|4|-3|1|-3|99|-3|-52|4|-3|0|-3|6|-3|1|-3|100|-3|-52|4|-3|2|-3|0|-3|1|-3|101|-3|-52|4|-3|2|-3|2|-3|1|-3|102|-3|-52|4|-3|2|-3|4|-3|1|-3|103|-3|-52|4|-3|2|-3|6|-3|1|-3|104|-3|-52|4|-3|4|-3|0|-3|1|-3|105|-3|-52|4|-3|4|-3|2|-3|1|-3|106|-3|-52|4|-3|4|-3|4|-3|1|-3|107|-3|-52|4|-3|4|-3|6|-3|1|-3|108|-3|-52|4|-3|6|-3|0|-3|1|-3|109|-3|-52|4|-3|6|-3|2|-3|1|-3|110|-3|-52|4|-3|6|-3|4|-3|1|-3|111|-3|-52|4|-3|6|-3|6|-3|1|-3|112|-3|-52|6|-3|0|-3|0|-3|1|-3|113|-3|-52|6|-3|0|-3|2|-3|1|-3|114|-3|-52|6|-3|0|-3|4|-3|1|-3|115|-3|-52|6|-3|0|-3|6|-3|1|-3|116|-3|-52|6|-3|2|-3|0|-3|1|-3|117|-3|-52|6|-3|2|-3|2|-3|1|-3|118|-3|-52|6|-3|2|-3|4|-3|1|-3|119|-3|-52|6|-3|2|-3|6|-3|1|-3|120|-3|-52|6|-3|4|-3|0|-3|1|-3|121|-3|-52|6|-3|4|-3|2|-3|1|-3|122|-3|-52|6|-3|4|-3|4|-3|1|-3|123|-3|-52|6|-3|4|-3|6|-3|1|-3|124|-3|-52|6|-3|6|-3|0|-3|1|-3|125|-3|-52|6|-3|6|-3|2|-3|1|-3|126|-3|-52|6|-3|6|-3|4|-3|1|-3|127|-3|-52|6|-3|6|-3|6|-3|1|-3|128|-15
    // |-53|-14|-524000|0|-3|0|-3|0|-3|1|-10|5|-3|64|-3|-157442|0|-3|0|-3|0|-3|1|-3|1|-3|3|-3|50|-3|-157442|0|-3|0|-3|0|-3|1|-3|3|-3|1|-3|50|-3|-157442|0|-3|0|-3|0|-3|3|-3|1|-3|1|-3|50|-3|-326|-1|1|-3|-1|1|-3|-1|1|-3|-1|1|-3|-1|1|-3|1|-3|0|-3|-326|-1|1|-3|-1|1|-3|-1|1|-3|-1|1|-3|1|-3|-1|1|-3|3|-3|-326|-1|1|-3|-1|1|-3|-1|1|-3|1|-3|-1|1|-3|-1|1|-3|6|-3|-326|1|-3|1|-3|1|-3|1|-3|1|-3|-1|1|-3|9|-3|-326|1|-3|1|-3|1|-3|1|-3|-1|1|-3|1|-3|12|-3|-326|1|-3|1|-3|1|-3|-1|1|-3|1|-3|1|-3|15|-3|-326|-1|1|-3|-1|1|-3|1|-3|-1|1|-3|1|-3|1|-3|17|-3|-326|-1|1|-3|-1|1|-3|1|-3|1|-3|-1|1|-3|1|-3|19|-3|-326|1|-3|-1|1|-3|1|-3|1|-3|-1|1|-3|-1|1|-3|21|-3|-326|1|-3|-1|1|-3|-1|1|-3|1|-3|1|-3|-1|1|-3|23|-3|-326|1|-3|1|-3|-1|1|-3|-1|1|-3|1|-3|-1|1|-3|25|-3|-326|-1|1|-3|1|-3|-1|1|-3|-1|1|-3|1|-3|1|-3|27|-15

    if(symbols.size() < 3) {
        return std::nullopt;
    }
    if(symbols[0] != DscrSpecialPlot::Symbol::Plot || symbols[1] != DscrSpecialPlot::Symbol::Begin) {
        return std::nullopt;
    }
    DscrSpecialPlot plot;
    qsizetype cur = 2;
    if(consumeShape(symbols, cur, plot)) {
        while(cur < symbols.size()) {
            if(symbols[cur] == DscrSpecialPlot::Symbol::Sep) {
                cur++;
            }
            if(cur < symbols.size() && symbols[cur] == DscrSpecialPlot::Symbol::End) {
                // allow trailing Sep (C/Python-like semantics)
                break;
            }
            if(!consumeShape(symbols, cur, plot)) {
                return std::nullopt;
            }
        }
    }
    bool endCondition = endMustTerminate ? cur != symbols.size() - 1 : cur >= symbols.size();
    if(endCondition || symbols[cur] != DscrSpecialPlot::Symbol::End) {
        return std::nullopt;
    }
    if(endWritebackNullable != nullptr) {
        *endWritebackNullable = cur + 1;
    }
    return plot;
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
        } else if(auto music = DscrSpecialMusic::fromSymbols(symbols.sliced(i), false, &end); music.has_value()) {
            specs.append(DscrSpecialSequence { i, i + end, std::move(*music) });
            i += end;
        } else {
            i++;
        }
    }

    return specs;
}
