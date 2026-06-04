#include "SdlAudio.h"

#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL.h>

#include <algorithm>
#include <limits>
#include <string.h>

SdlAudio::SdlAudio()
    : m_sound(nullptr),
      m_stream(nullptr),
      m_sampleRate(44100u),
      m_sampleRemainder(0),
      m_captureEnabled(false),
      m_startedAudioSubSystem(false) {}

SdlAudio::~SdlAudio() { stop(); }

bool SdlAudio::start(bdm_sound_t *sound, unsigned sampleRate, bool captureEnabled) {
    stop();
    if (!sound) {
        m_error = QStringLiteral("No sound device");
        return false;
    }

    m_sound = sound;
    m_sampleRate = sampleRate ? sampleRate : 44100u;
    m_captureEnabled = captureEnabled;
    m_sampleRemainder = 0;
    m_capture.clear();
    m_error.clear();

    if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            m_error = QString::fromUtf8(SDL_GetError());
            m_sound = nullptr;
            return false;
        }
        m_startedAudioSubSystem = true;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.format = SDL_AUDIO_S16;
    want.channels = 1;
    want.freq = (int)m_sampleRate;

    SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want, nullptr, nullptr);
    if (!stream) {
        m_error = QString::fromUtf8(SDL_GetError());
        if (m_startedAudioSubSystem) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            m_startedAudioSubSystem = false;
        }
        m_sound = nullptr;
        return false;
    }

    bdm_sound_set_sample_rate(m_sound, m_sampleRate);
    m_stream = stream;
    if (!SDL_ResumeAudioStreamDevice(stream)) {
        m_error = QString::fromUtf8(SDL_GetError());
        stop();
        return false;
    }
    return true;
}

void SdlAudio::stop() {
    if (m_stream) {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(m_stream));
        m_stream = nullptr;
    }
    if (m_startedAudioSubSystem) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_startedAudioSubSystem = false;
    }
    m_sound = nullptr;
    m_sampleRemainder = 0;
}

int SdlAudio::appendCapture(const int16_t *samples, size_t frames) {
    if (!m_captureEnabled || !samples || !frames) return 0;
    qsizetype oldSize = m_capture.size();
    if (frames > (size_t)((std::numeric_limits<qsizetype>::max)() - oldSize)) return -1;
    m_capture.resize(oldSize + (qsizetype)frames);
    memcpy(m_capture.data() + oldSize, samples, frames * sizeof(*samples));
    return 0;
}

int SdlAudio::pump(unsigned fps) {
    SDL_AudioStream *stream = static_cast<SDL_AudioStream *>(m_stream);
    if (!stream || !m_sound || m_sampleRate == 0u || fps == 0u) return 0;

    int queuedBytes = SDL_GetAudioStreamQueued(stream);
    if (queuedBytes < 0) queuedBytes = 0;
    int targetBytes = (int)((m_sampleRate / 10u) * sizeof(int16_t));
    if (queuedBytes > targetBytes) return 0;

    m_sampleRemainder += m_sampleRate;
    size_t frames = (size_t)(m_sampleRemainder / fps);
    m_sampleRemainder %= fps;

    size_t extra = (size_t)((targetBytes - queuedBytes) / (int)sizeof(int16_t));
    if (extra > frames) frames = extra;
    if (frames == 0u) frames = 1u;

    int16_t buf[4096];
    while (frames) {
        size_t chunk = frames;
        if (chunk > sizeof(buf) / sizeof(buf[0])) chunk = sizeof(buf) / sizeof(buf[0]);
        bdm_sound_mix_s16(m_sound, buf, chunk, m_sampleRate);
        if (appendCapture(buf, chunk) != 0) return -1;
        if (!SDL_PutAudioStreamData(stream, buf, (int)(chunk * sizeof(buf[0])))) {
            m_error = QString::fromUtf8(SDL_GetError());
            return -1;
        }
        frames -= chunk;
    }
    return 0;
}

const int16_t *SdlAudio::capturedSamples(size_t *frames) const {
    if (frames) *frames = (size_t)m_capture.size();
    return m_capture.isEmpty() ? nullptr : m_capture.constData();
}
