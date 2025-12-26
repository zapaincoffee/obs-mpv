#pragma once

#include <QtWidgets/QDockWidget>
#include <QtCore/QTimer>
#include <obs.h>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QLineEdit;
class ObsMpvSource;
class MpvSubSettingsDialog;

class MpvControlDock : public QDockWidget {
    Q_OBJECT

public:
    MpvControlDock(QWidget *parent = nullptr);
    ~MpvControlDock();

    void onSceneItemSelectionChanged();

private slots:
    void onTimerTick();
    void onSourceChanged(int index);
    void onPlayClicked();
    void onStopClicked();
    void onSeekSliderReleased();
    void onSeekSliderMoved(int value);
    void onLoopCountChanged(int value);
    void onAudioTrackChanged(int index);
    void onSubTrackChanged(int index);
    void onVolumeChanged(int value);
    
    void onLoadSubsClicked();
    void onSubSettingsClicked();
    
    // Unused placeholders
    void onTimeJumpReturnPressed();
    void onSubScaleChanged(double value);
    void onSubPosChanged(double value);
    void onSubColorClicked();

private:
    QComboBox *m_comboSources;
    QSlider *m_sliderSeek;
    QPushButton *m_btnPlay;
    QPushButton *m_btnStop;
    
    QSpinBox *m_spinLoop;
    QComboBox *m_comboAudio;
    QComboBox *m_comboSubs;
    QSlider *m_sliderVolume;
    
    QPushButton *m_btnLoadSubs;
    QPushButton *m_btnSubSettings;

    QTimer *m_timer;
    obs_source_t *m_currentSource;
    bool m_isSeeking;
    uint32_t m_currentSubColor;
    
    MpvSubSettingsDialog *m_subDialog;

    ObsMpvSource* getCurrentMpvSource();
    void updateUiFromSource();
    void updateSourceList();
    
    QString formatTime(double seconds);
    double parseTime(const QString &text);
    void populateTracks(QComboBox *combo, const char *type);
};
