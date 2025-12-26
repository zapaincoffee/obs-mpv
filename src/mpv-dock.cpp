#include "mpv-dock.hpp"
#include "mpv-sub-dialog.hpp"
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFileDialog>
#include <obs-module.h>
#include <obs-frontend-api.h>

MpvControlDock::MpvControlDock(QWidget *parent) : QDockWidget(parent), m_currentSource(nullptr), m_isSeeking(false) {
    setWindowTitle("MPV Controls");
    setMinimumWidth(250);

    m_subDialog = new MpvSubSettingsDialog(this);

    QWidget *content = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(content);

    // Source Selection
    QHBoxLayout *sourceLayout = new QHBoxLayout();
    sourceLayout->addWidget(new QLabel("Target:", content));
    m_comboSources = new QComboBox(content);
    sourceLayout->addWidget(m_comboSources);
    layout->addLayout(sourceLayout);

    // Seek Slider
    m_sliderSeek = new QSlider(Qt::Horizontal, content);
    m_sliderSeek->setRange(0, 1000);
    layout->addWidget(m_sliderSeek);
    
    // Volume Slider
    QHBoxLayout *volLayout = new QHBoxLayout();
    volLayout->addWidget(new QLabel("Vol:", content));
    m_sliderVolume = new QSlider(Qt::Horizontal, content);
    m_sliderVolume->setRange(0, 100);
    m_sliderVolume->setValue(100);
    volLayout->addWidget(m_sliderVolume);
    layout->addLayout(volLayout);

    // Transport Controls
    QHBoxLayout *btns = new QHBoxLayout();
    m_btnPlay = new QPushButton("Play/Pause", content);
    m_btnStop = new QPushButton("Stop (Freeze)", content);
    btns->addWidget(m_btnPlay);
    btns->addWidget(m_btnStop);
    layout->addLayout(btns);

    // Options
    QFormLayout *form = new QFormLayout();
    m_comboAudio = new QComboBox(content);
    m_comboSubs = new QComboBox(content);
    m_spinLoop = new QSpinBox(content);
    m_spinLoop->setRange(-1, 99);
    form->addRow("Audio Track:", m_comboAudio);
    form->addRow("Subtitle Track:", m_comboSubs);
    form->addRow("Loop Count:", m_spinLoop);
    layout->addLayout(form);
    
    // Subtitle Actions
    QHBoxLayout *subBtns = new QHBoxLayout();
    m_btnLoadSubs = new QPushButton("Load Subs...", content);
    m_btnSubSettings = new QPushButton("Sub Settings", content);
    subBtns->addWidget(m_btnLoadSubs);
    subBtns->addWidget(m_btnSubSettings);
    layout->addLayout(subBtns);

    layout->addStretch();
    setWidget(content);

    // Connections
    connect(m_comboSources, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MpvControlDock::onSourceChanged);
    connect(m_btnPlay, &QPushButton::clicked, this, &MpvControlDock::onPlayClicked);
    connect(m_btnStop, &QPushButton::clicked, this, &MpvControlDock::onStopClicked);
    connect(m_sliderSeek, &QSlider::sliderPressed, [this](){ m_isSeeking = true; });
    connect(m_sliderSeek, &QSlider::sliderReleased, this, &MpvControlDock::onSeekSliderReleased);
    connect(m_sliderVolume, &QSlider::valueChanged, this, &MpvControlDock::onVolumeChanged);
    connect(m_comboAudio, QOverload<int>::of(&QComboBox::activated), this, &MpvControlDock::onAudioTrackChanged);
    connect(m_comboSubs, QOverload<int>::of(&QComboBox::activated), this, &MpvControlDock::onSubTrackChanged);
    connect(m_spinLoop, QOverload<int>::of(&QSpinBox::valueChanged), this, &MpvControlDock::onLoopCountChanged);
    connect(m_btnLoadSubs, &QPushButton::clicked, this, &MpvControlDock::onLoadSubsClicked);
    connect(m_btnSubSettings, &QPushButton::clicked, this, &MpvControlDock::onSubSettingsClicked);

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MpvControlDock::onTimerTick);
    m_timer->start(500);
}

MpvControlDock::~MpvControlDock() {
    if (m_currentSource) obs_source_release(m_currentSource);
}

void MpvControlDock::onTimerTick() {
    updateSourceList();
    if (!m_currentSource) {
        setEnabled(false);
        if (m_subDialog->isVisible()) m_subDialog->hide();
        return;
    }
    setEnabled(true);
    updateUiFromSource();
}

void MpvControlDock::updateSourceList() {
    struct SourceInfo { QString name; obs_source_t *source; };
    std::vector<SourceInfo> sources;
    obs_enum_sources([](void *d, obs_source_t *s) -> bool {
        if (strcmp(obs_source_get_id(s), "mpv_source") == 0)
            static_cast<std::vector<SourceInfo>*>(d)->push_back({obs_source_get_name(s), s});
        return true;
    }, &sources);

    bool needsUpdate = false;
    if (m_comboSources->count() != (int)sources.size()) needsUpdate = true;
    else {
        for (size_t i = 0; i < sources.size(); ++i) {
            if (m_comboSources->itemText((int)i) != sources[i].name) {
                needsUpdate = true;
                break;
            }
        }
    }

    if (needsUpdate) {
        QString current = m_comboSources->currentText();
        m_comboSources->blockSignals(true);
        m_comboSources->clear();
        for (const auto &i : sources) m_comboSources->addItem(i.name, QVariant::fromValue((void*)i.source));
        int idx = m_comboSources->findText(current);
        if (idx >= 0) m_comboSources->setCurrentIndex(idx);
        else { onSourceChanged(0); }
        m_comboSources->blockSignals(false);
    }
}

void MpvControlDock::onSourceChanged(int index) {
    if (index < 0) return;
    obs_source_t *newSource = static_cast<obs_source_t*>(m_comboSources->itemData(index).value<void*>());
    if (newSource != m_currentSource) {
        if (m_currentSource) obs_source_release(m_currentSource);
        m_currentSource = newSource;
        if (m_currentSource) obs_source_get_ref(m_currentSource);
    }
}

void MpvControlDock::updateUiFromSource() {
    if (!m_currentSource) return;

    if (!m_isSeeking) {
        int64_t t = obs_source_media_get_time(m_currentSource);
        int64_t d = obs_source_media_get_duration(m_currentSource);
        if (d > 0) m_sliderSeek->setValue((int)((double)t / d * 1000.0));
    }
    m_btnPlay->setText(obs_source_media_get_state(m_currentSource) == OBS_MEDIA_STATE_PLAYING ? "Pause" : "Play");

    obs_data_t *s = obs_source_get_settings(m_currentSource);
    if (!s) return;
    
    if (!m_sliderVolume->isSliderDown()) {
        m_sliderVolume->blockSignals(true);
        m_sliderVolume->setValue((int)obs_data_get_double(s, "volume"));
        m_sliderVolume->blockSignals(false);
    }

    auto parse = [&](const char *key, QComboBox *combo) {
        if (combo->view()->isVisible()) return;
        const char *data = obs_data_get_string(s, key);
        QString qdata = data;
        if (combo->property("last_data").toString() == qdata) return;
        combo->setProperty("last_data", qdata);
        combo->blockSignals(true);
        combo->clear(); combo->addItem("None", -1);
        for (const QString &e : qdata.split('|')) {
            QStringList p = e.split(':');
            if (p.size() >= 3) {
                combo->addItem(p[1], p[0].toInt());
                if (p[2] == "1") combo->setCurrentIndex(combo->count()-1);
            }
        }
        combo->blockSignals(false);
    };
    parse("track_list_audio", m_comboAudio);
    parse("track_list_sub", m_comboSubs);
    
    obs_data_release(s);
}

void MpvControlDock::onPlayClicked() {
    if (m_currentSource) obs_source_media_play_pause(m_currentSource, obs_source_media_get_state(m_currentSource) == OBS_MEDIA_STATE_PLAYING);
}

void MpvControlDock::onStopClicked() { if (m_currentSource) obs_source_media_stop(m_currentSource); }

void MpvControlDock::onSeekSliderReleased() {
    m_isSeeking = false;
    if (m_currentSource) obs_source_media_set_time(m_currentSource, (int64_t)((m_sliderSeek->value()/1000.0) * obs_source_media_get_duration(m_currentSource)));
}

void MpvControlDock::onVolumeChanged(int v) {
    if (!m_currentSource) return;
    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_double(s, "volume", (double)v);
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onAudioTrackChanged(int) {
    if (!m_currentSource) return;
    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_int(s, "audio_track", m_comboAudio->currentData().toInt());
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onSubTrackChanged(int) {
    if (!m_currentSource) return;
    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_int(s, "subtitle_track", m_comboSubs->currentData().toInt());
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onLoopCountChanged(int v) {
    if (!m_currentSource) return;
    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_int(s, "loop", v);
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onLoadSubsClicked() {
    if (!m_currentSource) return;
    QString f = QFileDialog::getOpenFileName(this, "Load Subtitle File", "", "Subtitles (*.srt *.ass *.vtt *.sub);;All Files (*.*)");
    if (!f.isEmpty()) {
        obs_data_t *s = obs_source_get_settings(m_currentSource);
        obs_data_set_string(s, "load_subtitle", f.toUtf8().constData());
        obs_source_update(m_currentSource, s);
        obs_data_release(s);
    }
}

void MpvControlDock::onSubSettingsClicked() {
    if (m_currentSource) {
        m_subDialog->setSource(m_currentSource);
        m_subDialog->show();
        m_subDialog->raise();
        m_subDialog->activateWindow();
    }
}
void MpvControlDock::onSeekSliderMoved(int) {}
void MpvControlDock::onTimeJumpReturnPressed() {}
void MpvControlDock::onSubScaleChanged(double) {}
void MpvControlDock::onSubPosChanged(double) {}
void MpvControlDock::onSubColorClicked() {}
void MpvControlDock::onSceneItemSelectionChanged() {}
QString MpvControlDock::formatTime(double) {return "";}
double MpvControlDock::parseTime(const QString &) {return 0;}
void MpvControlDock::populateTracks(QComboBox *, const char *) {}
