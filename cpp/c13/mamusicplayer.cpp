#include "mamusicplayer.h"

MaMusicPlayer::MaMusicPlayer(size_t polyphony, QObject *parent) : QObject{parent}, polyphony(polyphony) {
    ma_result result;

    // Initialize engine
    this->engine.emplace();
    result = ma_engine_init(NULL, &*this->engine);
    if (result != MA_SUCCESS) {
        this->engine.reset();
        return;
    }

    // // Initialize waveforms
    // ma_waveform_config config = ma_waveform_config_init(
    //     ma_format_f32,
    //     ma_engine_get_channels(&*this->engine),
    //     ma_engine_get_sample_rate(&*this->engine),
    //     ma_waveform_type_triangle,
    //     1.0,
    //     440.0
    // );
    // this->waveforms.reserve(polyphony);
    // for(size_t i = 0; i < polyphony; i++) {
    //     ma_waveform &waveform = this->waveforms.emplace_back();
    //     result = ma_waveform_init(&config, &waveform);
    //     if (result != MA_SUCCESS) {
    //         this->waveforms.pop_back();
    //     }
    // }

    // // Initialize sounds
    // this->sounds.reserve(polyphony);
    // for(size_t i = 0; i < polyphony; i++) {
    //     ma_sound &sound = this->sounds.emplace_back();
    //     result = ma_sound_init_from_data_source(&*engine, &waveforms[i], 0, nullptr, &sound);
    //     if (result != MA_SUCCESS) {
    //         this->sounds.pop_back();
    //     }
    // }
}

MaMusicPlayer::~MaMusicPlayer() {
    for(auto &sound : this->sounds) {
        ma_sound_uninit(&sound);
    }
    for(auto &waveform : this->waveforms) {
        ma_waveform_uninit(&waveform);
    }
    if(this->engine.has_value()) {
        ma_engine_uninit(&*this->engine);
    }
}

static float flattenSequence(const DscrSpecialMusic::Sequence &seq, float timeOffsetSec, std::vector<NoteInstance> &song, size_t &itemLimit, bool *reachedLimitNullable) {
    float currentChordEndSec = timeOffsetSec;
    float currentChordStartSec = timeOffsetSec;
    for(auto &item : *seq.group) {
        if(itemLimit == 0) {
            if(reachedLimitNullable != nullptr) {
                *reachedLimitNullable = true;
            }
            return currentChordEndSec;
        }
        itemLimit--;
        if(std::holds_alternative<DscrSpecialMusic::Before>(item.item)) {
            currentChordStartSec = currentChordEndSec;
        } else if(auto note = std::get_if<DscrSpecialMusic::Note>(&item.item)) {
            if(note->freq > 0) {
                song.push_back({
                    currentChordStartSec + note->startTime * DscrSpecialMusic::SECOND_PER_HE6SECOND,
                    note->noteDuration * DscrSpecialMusic::SECOND_PER_HE6SECOND,
                    note->freq / DscrSpecialMusic::SECOND_PER_HE6SECOND,
                });
            }
            currentChordEndSec = std::max(currentChordEndSec, currentChordStartSec + (note->startTime + note->noteDuration) * DscrSpecialMusic::SECOND_PER_HE6SECOND);
        } else if(auto seq2 = std::get_if<DscrSpecialMusic::Sequence>(&item.item)) {
            currentChordEndSec = std::max(currentChordEndSec, flattenSequence(*seq2, currentChordStartSec, song, itemLimit, reachedLimitNullable));
        }
    }
    return currentChordEndSec;
}

bool MaMusicPlayer::load(const DscrSpecialMusic &music) {
    this->song.clear();
    size_t limit = 10000;
    bool limited = false;
    flattenSequence(music.music, 0.0f, this->song, limit, &limited);
    return !limited;
}

void MaMusicPlayer::play() {
    // FIXME lazy and s****y implementation
    // proper implementation should probably maintain a fixed size voice bank
    // and schedule notes as voices become available

    ma_result result;

    if(!this->engine.has_value()) {
        return;
    }
    for(auto &sound : this->sounds) {
        ma_sound_uninit(&sound);
    }
    for(auto &waveform : this->waveforms) {
        ma_waveform_uninit(&waveform);
    }

    this->waveforms.clear();
    this->sounds.clear();

    this->waveforms.reserve(this->song.size());
    this->sounds.reserve(this->song.size());

    ma_uint64 engineT0 = ma_engine_get_time_in_milliseconds(&*this->engine);
    for(auto &note : this->song) {
        ma_waveform_config config = ma_waveform_config_init(
            ma_format_f32,
            ma_engine_get_channels(&*this->engine),
            ma_engine_get_sample_rate(&*this->engine),
            ma_waveform_type_triangle,
            1.0,
            note.freqHz
        );

        ma_waveform &waveform = this->waveforms.emplace_back();
        result = ma_waveform_init(&config, &waveform);
        if (result != MA_SUCCESS) {
            this->waveforms.pop_back();
            return;
        }

        ma_sound &sound = this->sounds.emplace_back();
        result = ma_sound_init_from_data_source(&*engine, &waveform, 0, nullptr, &sound);
        if (result != MA_SUCCESS) {
            this->sounds.pop_back();
            return;
        }
        ma_sound_set_start_time_in_milliseconds(&sound, engineT0 + note.startSec * 1000);
        ma_sound_set_stop_time_with_fade_in_milliseconds(&sound, engineT0 + (note.startSec + note.durationSec) * 1000, 5);
    }

    for(auto &sound : this->sounds) {
         ma_sound_start(&sound);
    }
}

void MaMusicPlayer::stop() {
    for(auto &sound : this->sounds) {
        ma_sound_uninit(&sound);
    }
    for(auto &waveform : this->waveforms) {
        ma_waveform_uninit(&waveform);
    }

    this->waveforms.clear();
    this->sounds.clear();
}
