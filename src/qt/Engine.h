#ifndef BDM_QT_ENGINE_H
#define BDM_QT_ENGINE_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QTimer>

#ifdef BDM_QT_SDL3_AUDIO
class SdlAudio;
#endif

extern "C" {
#include "bdm_frontend.h"
}

class Engine : public QObject {
    Q_OBJECT
public:
    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    bool configureFromArgs(int argc, char **argv);
    bool load(const QString &cart, const QString &media = QString(), const QString &bios = QString());
    void start();
    void stop();
    bool isRunning() const { return m_running; }
    const bdm_video_t *video() const { return m_machine.video; }
    bdm_input_t *input() const { return m_machine.input; }
    bdm_core_t *core() const { return m_machine.core; }
    bdm_fe_options_t &options() { return m_opt; }
    bool autoCalibrationEnabled() const { return m_opt.auto_calibrate != 0; }

public slots:
    void reset();
    void setAutoCalibrationEnabled(bool enabled);
    void frameTick();
    void penDown(float x, float y);
    void penMove(float x, float y);
    void penUp(float x, float y);
    void setButton(int button, bool down);
    bool saveState(const QString &path);
    bool loadState(const QString &path);

signals:
    void frameReady(const bdm_video_t *video);
    void statusChanged(const QString &status);
    void stopped();

private:
    void destroyMachine();
    bool initMachine();
    QByteArray m_cartUtf8;
    QByteArray m_mediaUtf8;
    QByteArray m_biosUtf8;
    bdm_fe_options_t m_opt;
    bdm_fe_machine_t m_machine;
    bdm_fe_touch_state_t m_touch;
#ifdef BDM_QT_SDL3_AUDIO
    SdlAudio *m_audio;
#endif
    QTimer m_timer;
    bool m_running;
    uint64_t m_stepRemainder;
};

#endif
