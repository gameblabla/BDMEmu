#include "Engine.h"

#ifdef BDM_QT_SDL3_AUDIO
#include "SdlAudio.h"
#endif

#include <QCoreApplication>
#include <QFileInfo>

#include <inttypes.h>
#include <string.h>

Engine::Engine(QObject *parent) : QObject(parent),
#ifdef BDM_QT_SDL3_AUDIO
    m_audio(nullptr),
#endif
    m_running(false), m_stepRemainder(0) {
    bdm_fe_options_init(&m_opt);
    memset(&m_machine, 0, sizeof(m_machine));
    memset(&m_touch, 0, sizeof(m_touch));
    connect(&m_timer, &QTimer::timeout, this, &Engine::frameTick);
}

Engine::~Engine() { destroyMachine(); }

void Engine::destroyMachine() {
    stop();
    if (m_opt.dump_wav_path && m_machine.sound) {
        int wavRc = -1;
#ifdef BDM_QT_SDL3_AUDIO
        size_t capturedFrames = 0;
        const int16_t *captured = m_audio ? m_audio->capturedSamples(&capturedFrames) : nullptr;
        if (capturedFrames) wavRc = bdm_fe_dump_wav_samples(m_opt.dump_wav_path, captured, capturedFrames,
                                                            m_audio ? m_audio->sampleRate() : m_opt.sample_rate);
        else
#endif
        wavRc = bdm_fe_dump_wav(m_opt.dump_wav_path, m_machine.sound);
        emit statusChanged(wavRc == 0 ? QStringLiteral("Wrote WAV %1").arg(QString::fromUtf8(m_opt.dump_wav_path))
                                      : QStringLiteral("WAV dump failed"));
    }
#ifdef BDM_QT_SDL3_AUDIO
    delete m_audio;
    m_audio = nullptr;
#endif
    if (m_opt.save_state_path && m_machine.core) {
        QByteArray p(m_opt.save_state_path);
        (void)bdm_fe_save_state_file(p.constData(), m_machine.core);
    }
    if (m_opt.save_sram_path && m_machine.core) {
        (void)bdm_fe_save_sram_if_requested(m_opt.save_sram_path, m_machine.core);
    }
    bdm_fe_machine_destroy(&m_machine);
    memset(&m_touch, 0, sizeof(m_touch));
}

bool Engine::configureFromArgs(int argc, char **argv) {
    bdm_fe_options_t parsed;
    if (!bdm_fe_parse_args(argc, argv, &parsed, 0)) {
        bdm_fe_options_init(&m_opt);
        return false;
    }
    destroyMachine();
    m_opt = parsed;
    if (m_opt.cart_path) m_cartUtf8 = QByteArray(m_opt.cart_path);
    if (m_opt.media_path) m_mediaUtf8 = QByteArray(m_opt.media_path);
    if (m_opt.bios_path) m_biosUtf8 = QByteArray(m_opt.bios_path);
    m_opt.cart_path = m_cartUtf8.constData();
    m_opt.media_path = m_mediaUtf8.isEmpty() ? nullptr : m_mediaUtf8.constData();
    m_opt.bios_path = m_biosUtf8.isEmpty() ? nullptr : m_biosUtf8.constData();
    return initMachine();
}

bool Engine::load(const QString &cart, const QString &media, const QString &bios) {
    int keep_auto_calibrate = m_opt.auto_calibrate;
    destroyMachine();
    bdm_fe_options_init(&m_opt);
    m_opt.auto_calibrate = keep_auto_calibrate;
    m_cartUtf8 = QFileInfo(cart).absoluteFilePath().toUtf8();
    m_mediaUtf8 = media.isEmpty() ? QByteArray() : QFileInfo(media).absoluteFilePath().toUtf8();
    m_biosUtf8 = bios.isEmpty() ? QByteArray() : QFileInfo(bios).absoluteFilePath().toUtf8();
    m_opt.cart_path = m_cartUtf8.constData();
    m_opt.media_path = m_mediaUtf8.isEmpty() ? nullptr : m_mediaUtf8.constData();
    m_opt.bios_path = m_biosUtf8.isEmpty() ? nullptr : m_biosUtf8.constData();
    return initMachine();
}

bool Engine::initMachine() {
    if (bdm_fe_machine_init(&m_machine, &m_opt) != 0) {
        emit statusChanged(QStringLiteral("Machine initialization failed"));
        destroyMachine();
        return false;
    }
    memset(&m_touch, 0, sizeof(m_touch));
    m_touch.min_hold_steps = bdm_fe_ms_to_steps(m_opt.steps_per_second, m_opt.touch_hold_ms);
    m_touch.debug = m_opt.touch_debug;
    m_stepRemainder = 0;
#ifdef BDM_QT_SDL3_AUDIO
    delete m_audio;
    m_audio = nullptr;
    if (m_opt.enable_audio) {
        m_audio = new SdlAudio();
        if (!m_audio->start(m_machine.sound, m_opt.sample_rate, m_opt.dump_wav_path != nullptr)) {
            QString audioError = m_audio->errorString();
            delete m_audio;
            m_audio = nullptr;
            m_opt.enable_audio = 0;
            bdm_sound_enable_recording(m_machine.sound, m_opt.dump_wav_path != nullptr);
            emit statusChanged(QStringLiteral("SDL3 audio init failed: %1").arg(audioError));
        }
    }
#else
    if (m_opt.enable_audio) {
        m_opt.enable_audio = 0;
        bdm_sound_enable_recording(m_machine.sound, m_opt.dump_wav_path != nullptr);
        emit statusChanged(QStringLiteral("Qt6 frontend was built without SDL3 audio support"));
    }
#endif
    emit statusChanged(QStringLiteral("Loaded %1").arg(QString::fromUtf8(m_cartUtf8)));
    bdm_video_present_headless(m_machine.video);
    emit frameReady(m_machine.video);
    start();
    return true;
}

void Engine::start() {
    if (!m_machine.core) return;
    m_running = true;
    m_timer.start((int)(1000u / m_opt.fps));
}

void Engine::stop() {
    m_timer.stop();
    m_running = false;
}

void Engine::reset() {
    if (!m_machine.core) return;
    bdm_status_t rc = bdm_fe_soft_reset(m_machine.core, m_machine.input, &m_touch, &m_opt, 1);
    bdm_video_present_headless(m_machine.video);
    emit frameReady(m_machine.video);
    if (rc == BDM_OK) {
        emit statusChanged(m_opt.auto_calibrate ? QStringLiteral("Reset; auto calibration processed") : QStringLiteral("Reset"));
    } else {
        bdm_core_state_t st;
        bdm_core_get_state(m_machine.core, &st);
        emit statusChanged(QStringLiteral("Reset auto calibration failed: rc=%1 pc=%2 op=%3")
            .arg((int)rc).arg(st.pc, 4, 16, QLatin1Char('0')).arg(st.last_opcode, 4, 16, QLatin1Char('0')));
    }
}

void Engine::setAutoCalibrationEnabled(bool enabled) {
    m_opt.auto_calibrate = enabled ? 1 : 0;
    emit statusChanged(enabled ? QStringLiteral("Auto calibration enabled") : QStringLiteral("Auto calibration disabled"));
}

void Engine::frameTick() {
    if (!m_machine.core) return;
    m_stepRemainder += m_opt.steps_per_second;
    uint64_t frameSteps = m_stepRemainder / m_opt.fps;
    m_stepRemainder %= m_opt.fps;
    bdm_status_t rc = bdm_fe_run_checked(m_machine.core, frameSteps);
    bdm_fe_touch_tick_release(m_machine.input, &m_touch, m_machine.core);
    if (rc != BDM_OK) {
        bdm_core_state_t st;
        bdm_core_get_state(m_machine.core, &st);
        emit statusChanged(QStringLiteral("Stopped: rc=%1 pc=%2 op=%3 steps=%4")
            .arg((int)rc).arg(st.pc, 4, 16, QLatin1Char('0')).arg(st.last_opcode, 4, 16, QLatin1Char('0')).arg((qulonglong)st.steps));
        stop();
        emit stopped();
        return;
    }
#ifdef BDM_QT_SDL3_AUDIO
    if (m_audio && m_audio->pump(m_opt.fps) != 0) {
        QString audioError = m_audio->errorString();
        delete m_audio;
        m_audio = nullptr;
        m_opt.enable_audio = 0;
        bdm_sound_enable_recording(m_machine.sound, m_opt.dump_wav_path != nullptr);
        emit statusChanged(QStringLiteral("SDL3 audio queue failed: %1").arg(audioError));
    }
#endif
    bdm_video_present_headless(m_machine.video);
    emit frameReady(m_machine.video);
}

void Engine::penDown(float x, float y) {
    if (!m_machine.input) return;
    int32_t xfp = 0, yfp = 0;
    bdm_fe_logical_to_pen_fp(m_machine.video, x, y, m_opt.touch_offset_x, m_opt.touch_offset_y, &xfp, &yfp);
    bdm_fe_touch_prepare_down_for_video(&m_touch, m_machine.video,
                                        bdm_fe_ms_to_steps(m_opt.steps_per_second, m_opt.touch_hold_ms),
                                        bdm_fe_ms_to_steps(m_opt.steps_per_second, m_opt.calibration_touch_hold_ms));
    bdm_fe_touch_apply_down_fp(m_machine.input, &m_touch, m_machine.core, xfp, yfp);
}
void Engine::penMove(float x, float y) {
    if (!m_machine.input) return;
    int32_t xfp = 0, yfp = 0;
    bdm_fe_logical_to_pen_fp(m_machine.video, x, y, m_opt.touch_offset_x, m_opt.touch_offset_y, &xfp, &yfp);
    bdm_fe_touch_update_motion_fp(m_machine.input, &m_touch, xfp, yfp);
}
void Engine::penUp(float x, float y) {
    if (!m_machine.input) return;
    int32_t xfp = 0, yfp = 0;
    bdm_fe_logical_to_pen_fp(m_machine.video, x, y, m_opt.touch_offset_x, m_opt.touch_offset_y, &xfp, &yfp);
    bdm_fe_touch_request_up_fp(m_machine.input, &m_touch, m_machine.core, xfp, yfp);
}

void Engine::setButton(int button, bool down) {
    if (!m_machine.input) return;
    bdm_input_set_button(m_machine.input, (bdm_button_t)button, down);
}

bool Engine::saveState(const QString &path) {
    if (!m_machine.core) return false;
    QByteArray p = path.toUtf8();
    bool ok = bdm_fe_save_state_file(p.constData(), m_machine.core) == 0;
    emit statusChanged(ok ? QStringLiteral("Saved state %1").arg(path) : QStringLiteral("State save failed"));
    return ok;
}

bool Engine::loadState(const QString &path) {
    if (!m_machine.core) return false;
    QByteArray p = path.toUtf8();
    bool ok = bdm_fe_load_state_file(p.constData(), m_machine.core) == 0;
    emit statusChanged(ok ? QStringLiteral("Loaded state %1").arg(path) : QStringLiteral("State load failed"));
    return ok;
}
