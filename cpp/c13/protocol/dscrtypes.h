#pragma once

#include <optional>
#include <variant>
#include <QDateTime>
#include <QSpan>
#include <QVector>

struct DscrMessage {
    char signifier;
    QVector<qint64> integers;

    QString encodeWsMessage();
    static std::optional<DscrMessage> decodeWsMessage(const QString &text);

    QByteArray encodePacked();
    static std::optional<DscrMessage> decodePacked(const QByteArray &pack);

    static std::optional<DscrMessage> decode(int kind, const QVariant &msg);
};

struct DscrTransmission {
    qint64 from;
    std::optional<qint64> sequenceNum;
    QVector<qint64> symbols;

    static std::optional<DscrTransmission> fromDscrRMessage(const DscrMessage &msg);
    static std::optional<DscrTransmission> fromDscrMMessage(const DscrMessage &msg, qint64 from);
};

struct DscrSpecialEncrypted {
    enum Symbol {
        // A client "should not" issue a message beginning with AddKey/RemoveKey,
        // and "should" use them to control the display of messages beginning with EncryptedMessage.
        // The server does not treat these symbols specially.
        AddKey = -65534,
        RemoveKey = -65533,
        EncryptedMessage = -65535,
        SkeletonKey = -65536,
    };

    qint64 encKey;

    static std::optional<DscrSpecialEncrypted> find(QSpan<const qint64> symbols);
};

struct DscrSpecialMusic {
    enum Symbol {
        Sep = -3,
        Radix = -10,
        X = -11,
        Begin = -14,
        End = -15,
        BeforeSym = -122,
        Music = -577,
        NoteSym = -605003,
    };

    static constexpr qsizetype RECURSION_LIMIT = 50;
    static constexpr float SECOND_PER_HE6SECOND = 0.8069224;

    struct Item;

    struct Note {
        // Time units are in He-6 seconds
        float startTime;
        float noteDuration;
        float freq;
    };

    struct Sequence {
        std::shared_ptr<const QVector<Item>> group;
    };

    // Barrier to inform a player that it should finish playing the
    // current notes before proceeding to the next item
    struct Before {};

    struct Item {
        std::variant<Note, Sequence, Before> item;
    };

    Sequence music;

    static std::optional<DscrSpecialMusic> fromSymbols(QSpan<const qint64> symbols, bool endMustTerminate, qsizetype *endWritebackNullable);
};

struct DscrSpecialPlot {
    enum Symbol {
        // Orb plots, as seen in the game
        UnaryMinus = -1,
        Sep = -3,
        Radix = -10,
        Begin = -14,
        End = -15,
        OrbSym = -52,
        Plot = -53,
    };

    struct Orb {
        float x;
        float z;
        float y;
        float d;
        int colourIdx;
    };

    QVector<Orb> orbs;

    static std::optional<DscrSpecialPlot> fromSymbols(QSpan<const qint64> symbols, bool endMustTerminate, qsizetype *endWritebackNullable);
};

struct DscrSpecialSequence {
    qsizetype start;
    qsizetype end;
    std::variant<DscrSpecialEncrypted, DscrSpecialPlot, DscrSpecialMusic> attachment;

    static QVector<DscrSpecialSequence> findAll(QSpan<const qint64> symbols);
};

struct DscrTransmissionDbLinked {
    QDateTime time;
    qint64 msgId;
    std::optional<qint64> encKey;
    DscrTransmission msg;
    QVector<DscrSpecialSequence> specSequences;
};

struct EncKeySelect {
    bool showUnkeyed;
    bool showAllKeyed;
    bool showKeyed;
    qint64 encKey;
};
