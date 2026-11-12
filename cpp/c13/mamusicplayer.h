#pragma once

#include "dscrtypes.h"

#include "miniaudio.h"

#include <cstddef>
#include <optional>
#include <vector>
#include <QObject>

struct NoteInstance {
    float startSec;
    float durationSec;
    float freqHz;
};

class MaMusicPlayer : public QObject {
    Q_OBJECT

public:
    explicit MaMusicPlayer(size_t polyphony, QObject *parent = nullptr);
    ~MaMusicPlayer();

    bool load(const DscrSpecialMusic &music);
    void play();
    void stop();

signals:

private:
    std::vector<NoteInstance> song;

    size_t polyphony;
    std::optional<ma_engine> engine;
    // THESE VECTORS MUST BE PREALLOCATED!
    // MINIAUDIO OBJECTS CANNOT BE MOVED!
    std::vector<ma_waveform> waveforms;
    std::vector<ma_sound> sounds;
};
