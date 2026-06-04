#ifndef BDM_QT_SDLAUDIO_H
#define BDM_QT_SDLAUDIO_H

#include <QVector>
#include <QString>

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "bdm_sound.h"
}

class SdlAudio {
public:
    SdlAudio();
    ~SdlAudio();

    bool start(bdm_sound_t *sound, unsigned sampleRate, bool captureEnabled);
    void stop();
    int pump(unsigned fps);

    bool isActive() const { return m_stream != nullptr; }
    QString errorString() const { return m_error; }
    unsigned sampleRate() const { return m_sampleRate; }
    const int16_t *capturedSamples(size_t *frames) const;

private:
    int appendCapture(const int16_t *samples, size_t frames);

    bdm_sound_t *m_sound;
    void *m_stream;
    unsigned m_sampleRate;
    uint64_t m_sampleRemainder;
    bool m_captureEnabled;
    bool m_startedAudioSubSystem;
    QVector<int16_t> m_capture;
    QString m_error;
};

#endif
