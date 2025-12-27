#pragma once

#include <QDockWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <obs.h>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QLineEdit;
class ObsMpvSource;
class MpvSubSettingsDialog;
class PlaylistTableWidget;

class MpvControlDock : public QDockWidget {
    Q_OBJECT
friend class PlaylistTableWidget;

public:
    MpvControlDock(QWidget *parent = nullptr);
    ~MpvControlDock();

    void onSceneItemSelectionChanged();
    ObsMpvSource* getCurrentMpvSource();
    void updatePlaylistTable();

private slots:
    void onTimerTick();
    void onSourceChanged(int index);
    void onPlayClicked();
    void onStopClicked();
    void onSeekSliderReleased();
    void onSeekSliderMoved(int value);
    void onLoopCountChanged(int value);
    void onFadeInToggled(bool checked);
    void onFadeInChanged(double value);
    void onFadeOutToggled(bool checked);
    void onFadeOutChanged(double value);
    void onAudioTrackChanged(int index);
    void onSubTrackChanged(int index);
    void onVolumeChanged(int value);
    
    void onLoadSubsClicked();
    void onSubSettingsClicked();
    
    // Playlist Slots
    void onAddFiles();
    void onRemoveFile();
    void onMoveUp();
    void onMoveDown();
    void onItemClicked(QTableWidgetItem *item);

    // Unused placeholders
    void onTimeJumpReturnPressed();
    void onSubScaleChanged(double value);
    void onSubPosChanged(double value);
    void onSubColorClicked();

private:
    QComboBox *m_comboSources;
    QSlider *m_sliderSeek;
    QSlider *m_sliderVolume;
    QPushButton *m_btnPlay;
    QPushButton *m_btnStop;
    
    QComboBox *m_comboAudio;
    QComboBox *m_comboSubs;
    
    QSpinBox *m_spinLoop;
    QCheckBox *m_checkFadeIn;
    QDoubleSpinBox *m_spinFadeIn;
    QCheckBox *m_checkFadeOut;
    QDoubleSpinBox *m_spinFadeOut;
    
    QCheckBox *m_checkAutoFPS;

    QPushButton *m_btnLoadSubs;
    QPushButton *m_btnSubSettings;
    
    // Playlist UI
    PlaylistTableWidget *m_tablePlaylist;
    QPushButton *m_btnAdd;
    QPushButton *m_btnRemove;
    QPushButton *m_btnUp;
    QPushButton *m_btnDown;
    QLabel *m_labelTotalDuration;

    QTimer *m_timer;
    obs_source_t *m_currentSource;
    bool m_isSeeking;
    uint32_t m_currentSubColor;
    
    MpvSubSettingsDialog *m_subDialog;

    void updateUiFromSource();
    void updateSourceList();
    
    QString formatTime(double seconds);
    double parseTime(const QString &text);
    void populateTracks(QComboBox *combo, const char *type);
};
