#include "mpv-dock.hpp"
#include "obs-mpv-source.hpp"
#include "playlist-table-widget.hpp"
#include "mpv-sub-dialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QGroupBox>
#include <QHeaderView>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include "plugin-support.h"

MpvControlDock::MpvControlDock(QWidget *parent) : QDockWidget(parent), m_currentSource(nullptr), m_isSeeking(false) {
	setObjectName("MPVControls");
	setWindowTitle("MPV Controls & Playlist");
	setMinimumWidth(350);

	m_subDialog = new MpvSubSettingsDialog(this);

	QWidget *content = new QWidget(this);
	QVBoxLayout *layout = new QVBoxLayout(content);

	// Source Selection
	QHBoxLayout *sourceLayout = new QHBoxLayout();
	sourceLayout->addWidget(new QLabel("Target:", content));
	m_comboSources = new QComboBox(content);
	sourceLayout->addWidget(m_comboSources);
	layout->addLayout(sourceLayout);

	    // Version Label
	    QLabel *lblVersion = new QLabel(QString("v%1 (Build %2)").arg(PLUGIN_VERSION).arg("95e0f95"), content);
	    lblVersion->setAlignment(Qt::AlignRight);
	    lblVersion->setStyleSheet("color: gray; font-size: 10px;");
	    layout->addWidget(lblVersion);
	
	    // --- Playlist Table ---
	    m_table = new PlaylistTableWidget(this, content);
	    layout->addWidget(m_table);
	
	    // --- Media Controls Group ---
	    QGroupBox *controlsGroup = new QGroupBox("Controls", content);
	    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsGroup);
	
	    // Seek Slider
	    m_sliderSeek = new QSlider(Qt::Horizontal, content);
	    m_sliderSeek->setRange(0, 1000);
	    controlsLayout->addWidget(m_sliderSeek);
	    
	    // Time Labels
	    QHBoxLayout *timeLayout = new QHBoxLayout();
	    m_lblTimeCurrent = new QLabel("00:00", content);
	    m_lblTimeRemaining = new QLabel("-00:00", content);
	    m_lblPlaylistRemaining = new QLabel("Total: 00:00", content);
	    timeLayout->addWidget(m_lblTimeCurrent);
	    timeLayout->addStretch();
	    timeLayout->addWidget(m_lblTimeRemaining);
	    timeLayout->addStretch();
	    timeLayout->addWidget(m_lblPlaylistRemaining);
	    controlsLayout->addLayout(timeLayout);
	
	    // Volume Slider
	    QHBoxLayout *volLayout = new QHBoxLayout();
	    volLayout->addWidget(new QLabel("Vol:", content));
	    m_sliderVolume = new QSlider(Qt::Horizontal, content);
	    m_sliderVolume->setRange(0, 100);
	    m_sliderVolume->setValue(100);
	    volLayout->addWidget(m_sliderVolume);
	    controlsLayout->addLayout(volLayout);
	
	    // Transport Controls
	    QHBoxLayout *btns = new QHBoxLayout();
	    m_btnRestart = new QPushButton("⏮", content); m_btnRestart->setToolTip("Play from Beginning");
	    m_btnPlay = new QPushButton("▶", content); m_btnPlay->setToolTip("Play");
	    m_btnPause = new QPushButton("⏸", content); m_btnPause->setToolTip("Pause");
	    m_btnStop = new QPushButton("⏹", content); m_btnStop->setToolTip("Stop");
	    
	    btns->addWidget(m_btnRestart);
	    btns->addWidget(m_btnPlay);
	    btns->addWidget(m_btnPause);
	    btns->addWidget(m_btnStop);
	    controlsLayout->addLayout(btns);
	    
	    // Advanced Toggles
	    QHBoxLayout *advBtns = new QHBoxLayout();
	    m_checkFadePlay = new QCheckBox("Play w/ Fade", content);
	    m_btnRestartFade = new QPushButton("Restart w/ Fade", content);
	    advBtns->addWidget(m_checkFadePlay);
	    advBtns->addWidget(m_btnRestartFade);
	    controlsLayout->addLayout(advBtns);
	
	    // Options
	    QFormLayout *form = new QFormLayout();
	    m_comboAudio = new QComboBox(content);
	    m_comboSubs = new QComboBox(content);
	    m_spinLoop = new QSpinBox(content);
	    m_spinLoop->setRange(-1, 99);
	    m_spinLoop->setSpecialValueText("Infinite");
	
	    // Fade In Row
	    QHBoxLayout *fadeInLayout = new QHBoxLayout();
	    m_checkFadeIn = new QCheckBox("Fade In:", content);
	    m_spinFadeIn = new QDoubleSpinBox(content);
	    m_spinFadeIn->setRange(0, 10.0);
	    m_spinFadeIn->setSuffix(" s");
	    m_spinFadeIn->setMinimumHeight(30);
	    m_spinFadeIn->setEnabled(false);
	    fadeInLayout->addWidget(m_checkFadeIn);
	    fadeInLayout->addWidget(m_spinFadeIn);
	
	    // Fade Out Row
	    QHBoxLayout *fadeOutLayout = new QHBoxLayout();
	    m_checkFadeOut = new QCheckBox("Fade Out:", content);
	    m_spinFadeOut = new QDoubleSpinBox(content);
	    m_spinFadeOut->setRange(0, 10.0);
	    m_spinFadeOut->setSuffix(" s");
	    m_spinFadeOut->setMinimumHeight(30);
	    m_spinFadeOut->setEnabled(false);
	    fadeOutLayout->addWidget(m_checkFadeOut);
	    fadeOutLayout->addWidget(m_spinFadeOut);
	
	    form->addRow("Audio:", m_comboAudio);
	    form->addRow("Subs:", m_comboSubs);
	    form->addRow("Loops:", m_spinLoop); // Loop Count
	    form->addRow(fadeInLayout);
	    form->addRow(fadeOutLayout);
	    
	    QPushButton *btnSubSettings = new QPushButton("Subtitle Settings...", content);
	    form->addRow(btnSubSettings);
	
	    controlsLayout->addLayout(form);
	    layout->addWidget(controlsGroup);
	
	    // Connect signals
	    connect(m_comboSources, &QComboBox::currentIndexChanged, this, &MpvControlDock::onSourceChanged);
	    connect(m_sliderSeek, &QSlider::sliderReleased, this, &MpvControlDock::onSeek);
	    connect(m_sliderSeek, &QSlider::sliderPressed, this, [this]() { m_isSeeking = true; });
	    connect(m_sliderVolume, &QSlider::valueChanged, this, &MpvControlDock::onVolumeChanged);
	    
	    connect(m_btnPlay, &QPushButton::clicked, this, &MpvControlDock::onPlayClicked);
	    connect(m_btnPause, &QPushButton::clicked, this, &MpvControlDock::onPauseClicked);
	    connect(m_btnStop, &QPushButton::clicked, this, &MpvControlDock::onStopClicked);
	    connect(m_btnRestart, &QPushButton::clicked, this, &MpvControlDock::onRestartClicked);
	    connect(m_btnRestartFade, &QPushButton::clicked, this, &MpvControlDock::onRestartFadeClicked);
	    
	    connect(btnSubSettings, &QPushButton::clicked, m_subDialog, &QDialog::show);
	
	    connect(m_comboAudio, &QComboBox::activated, this, &MpvControlDock::onTrackChanged);
	    connect(m_comboSubs, &QComboBox::activated, this, &MpvControlDock::onTrackChanged);
	    
	    connect(m_checkFadeIn, &QCheckBox::toggled, m_spinFadeIn, &QDoubleSpinBox::setEnabled);
	    connect(m_checkFadeOut, &QCheckBox::toggled, m_spinFadeOut, &QDoubleSpinBox::setEnabled);
	    
	    connect(m_checkFadeIn, &QCheckBox::clicked, this, &MpvControlDock::saveSettings);
	    connect(m_spinFadeIn, &QDoubleSpinBox::editingFinished, this, &MpvControlDock::saveSettings);
	    connect(m_checkFadeOut, &QCheckBox::clicked, this, &MpvControlDock::saveSettings);
	    connect(m_spinFadeOut, &QDoubleSpinBox::editingFinished, this, &MpvControlDock::saveSettings);
	    connect(m_spinLoop, &QSpinBox::editingFinished, this, &MpvControlDock::saveSettings);
	
	    QTimer *timer = new QTimer(this);
	    connect(timer, &QTimer::timeout, this, &MpvControlDock::updateTimer);
	    timer->start(100);
	
	    refreshSources();
	}
	
	void MpvControlDock::onPlayClicked() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (!source) return;
	    
	    if (m_checkFadePlay->isChecked()) {
	        source->playlist_play_with_fade(source->get_current_index(), m_spinFadeIn->value());
	    } else {
	        source->play(); // Resume or Play
	    }
	}
	
	void MpvControlDock::onPauseClicked() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (source) source->pause();
	}
	
	void MpvControlDock::onStopClicked() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (source) source->stop();
	}
	
	void MpvControlDock::onRestartClicked() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (source) source->seek(0);
	}
	
	void MpvControlDock::onRestartFadeClicked() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (source) source->playlist_restart_with_fade(m_spinFadeIn->value());
	}
	
	void MpvControlDock::updateTimer() {
	    ObsMpvSource *source = getCurrentMpvSource();
	    if (!source) {
	        setEnabled(false);
	        m_lblTimeCurrent->setText("00:00");
	        return;
	    }
	    setEnabled(true);
	
	    if (!m_isSeeking) {
	        double pos = source->get_time_pos();
	        double dur = source->get_duration();
	        if (dur > 0) {
	            m_sliderSeek->setValue((int)((pos / dur) * 1000));
	        } else {
	            m_sliderSeek->setValue(0);
	        }
	        
	        auto fmt = [](double s) {
	            int m = (int)s / 60;
	            int sec = (int)s % 60;
	            return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
	        };
	        
	        m_lblTimeCurrent->setText(fmt(pos));
	        m_lblTimeRemaining->setText("-" + fmt(source->get_time_remaining()));
	        m_lblPlaylistRemaining->setText("Total: " + fmt(source->get_playlist_time_remaining()));
	    }
	    
	    // Highlight Active Track
	    int idx = source->get_current_index();
	    if (idx >= 0 && idx < m_table->rowCount()) {
	        for (int i=0; i<m_table->rowCount(); i++) {
	            // Simple visual check - could be optimized
	            QFont f = m_table->item(i, 0)->font();
	            f.setBold(i == idx);
	            m_table->item(i, 0)->setFont(f);
	            if (i == idx) m_table->selectRow(i);
	        }
	    }
	    
	    // Check if tracks changed (simple polling for now)
	    // In a real optimized version we'd use a signal or atomic flag
	}
	form->addRow("Audio Track:", m_comboAudio);
	form->addRow("Subtitle Track:", m_comboSubs);
	form->addRow("Loop Count:", m_spinLoop);
	form->addRow(fadeInLayout);
	form->addRow(fadeOutLayout);

	m_checkAutoFPS = new QCheckBox("Auto Match OBS FPS", content);
	form->addRow(m_checkAutoFPS);

	controlsLayout->addLayout(form);

	// Subtitle Actions
	QHBoxLayout *subBtns = new QHBoxLayout();
	m_btnLoadSubs = new QPushButton("Load Subs...", content);
	m_btnSubSettings = new QPushButton("Sub Settings", content);
	subBtns->addWidget(m_btnLoadSubs);
	subBtns->addWidget(m_btnSubSettings);
	controlsLayout->addLayout(subBtns);

	controlsGroup->setLayout(controlsLayout);
	layout->addWidget(controlsGroup);

	// --- Playlist Group ---
	QGroupBox *playlistGroup = new QGroupBox("Playlist", content);
	QVBoxLayout *playlistLayout = new QVBoxLayout();

    m_table = new PlaylistTableWidget(this, content);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"File", "Duration", "FPS", "Ch", "Loop", "Subs"});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);

    playlistLayout->addWidget(m_table);

    // Playlist buttons
    QHBoxLayout *playlistBtns = new QHBoxLayout();
    m_btnAdd = new QPushButton("Add", content);
    m_btnRemove = new QPushButton("Remove", content);
    m_btnUp = new QPushButton("Up", content);
    m_btnDown = new QPushButton("Down", content);
    playlistBtns->addWidget(m_btnAdd);
    playlistBtns->addWidget(m_btnRemove);
    playlistBtns->addStretch();
    playlistBtns->addWidget(m_btnUp);
    playlistBtns->addWidget(m_btnDown);
    playlistLayout->addLayout(playlistBtns);

    m_labelTotalDuration = new QLabel("Total Duration: 00:00:00", content);
    playlistLayout->addWidget(m_labelTotalDuration);

    playlistGroup->setLayout(playlistLayout);
    layout->addWidget(playlistGroup);

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
    connect(m_checkFadeIn, &QCheckBox::toggled, this, &MpvControlDock::onFadeInToggled);
    connect(m_spinFadeIn, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvControlDock::onFadeInChanged);
    connect(m_checkFadeOut, &QCheckBox::toggled, this, &MpvControlDock::onFadeOutToggled);
    connect(m_spinFadeOut, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvControlDock::onFadeOutChanged);

    connect(m_checkAutoFPS, &QCheckBox::toggled, [this](bool checked){
        ObsMpvSource *source = getCurrentMpvSource();
        if (source) {
            source->set_auto_obs_fps(checked);
            obs_data_t *s = obs_source_get_settings(m_currentSource);
            obs_data_set_bool(s, "auto_obs_fps", checked);
            obs_source_update(m_currentSource, s);
            obs_data_release(s);
        }
    });

    connect(m_btnLoadSubs, &QPushButton::clicked, this, &MpvControlDock::onLoadSubsClicked);
    connect(m_btnSubSettings, &QPushButton::clicked, this, &MpvControlDock::onSubSettingsClicked);

    // Playlist Connections
    connect(m_btnAdd, &QPushButton::clicked, this, &MpvControlDock::onAddFiles);
    connect(m_btnRemove, &QPushButton::clicked, this, &MpvControlDock::onRemoveFile);
    connect(m_btnUp, &QPushButton::clicked, this, &MpvControlDock::onMoveUp);
    connect(m_btnDown, &QPushButton::clicked, this, &MpvControlDock::onMoveDown);
    connect(m_table, &QTableWidget::itemClicked, this, &MpvControlDock::onItemClicked);


    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MpvControlDock::onTimerTick);
    m_timer->start(100);
}

MpvControlDock::~MpvControlDock() {
    if (m_currentSource) obs_source_release(m_currentSource);
}

void MpvControlDock::saveSettings() {
    // Helper to trigger save on generic edits
    // Actual saving happens via signal connections to specific slots or immediate updates
    // This is just a placeholder if needed for batch saves
}

void MpvControlDock::onTimerTick() {
    updateSourceList();
    
    // Call the specific timer update for labels/seek
    updateTimer(); 
    
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
    updatePlaylistTable();
}

void MpvControlDock::updateUiFromSource() {
    if (!m_currentSource) return;

    // Update Play Button text
    ObsMpvSource *source = getCurrentMpvSource();
    if (source) {
        if (source->is_playing()) {
            m_btnPlay->setText("▶");
            m_btnPlay->setToolTip("Resume (Playing)");
            m_btnPlay->setEnabled(false); // Disable play if playing
            m_btnPause->setEnabled(true);
        } else if (source->is_paused()) {
            m_btnPlay->setText("▶");
            m_btnPlay->setToolTip("Resume");
            m_btnPlay->setEnabled(true);
            m_btnPause->setEnabled(false); // Disable pause if paused
        } else {
            m_btnPlay->setText("▶");
            m_btnPlay->setEnabled(true);
            m_btnPause->setEnabled(false);
        }
    }

    obs_data_t *s = obs_source_get_settings(m_currentSource);
    if (!s) return;

    if (!m_sliderVolume->isSliderDown()) {
        m_sliderVolume->blockSignals(true);
        m_sliderVolume->setValue((int)obs_data_get_double(s, "volume"));
        m_sliderVolume->blockSignals(false);
    }

    m_checkAutoFPS->blockSignals(true);
    m_checkAutoFPS->setChecked(obs_data_get_bool(s, "auto_obs_fps"));
    m_checkAutoFPS->blockSignals(false);

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
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int selectedRow = m_table->currentRow();
    
    if (m_checkFadePlay->isChecked()) {
        source->playlist_play_with_fade(selectedRow >= 0 ? selectedRow : source->get_current_index(), m_spinFadeIn->value());
        return;
    }

    // Standard play logic
    if (source->is_paused() || source->is_idle()) {
        source->play();
    } else if (selectedRow >= 0 && selectedRow != source->get_current_index()) {
        source->playlist_play(selectedRow);
    } else if (source->get_current_index() < 0 && source->playlist_count() > 0) {
        source->playlist_play(0);
    }
}

void MpvControlDock::onStopClicked() { if (m_currentSource) obs_source_media_stop(m_currentSource); }

void MpvControlDock::onSeekSliderReleased() {
    m_isSeeking = false;
    if (m_currentSource) obs_source_media_set_time(m_currentSource, (int64_t)((m_sliderSeek->value()/1000.0) * obs_source_media_get_duration(m_currentSource)));
}

void MpvControlDock::onVolumeChanged(int v) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int row = m_table->currentRow();
    if (row >= 0) {
        ObsMpvSource::PlaylistItem* item = source->playlist_get_item(row);
        if (item) {
            item->volume = (double)v;
        }
    }

    // Also apply to currently playing source
    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_double(s, "volume", (double)v);
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onAudioTrackChanged(int) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int track_id = m_comboAudio->currentData().toInt();
    int row = m_table->currentRow();
    if (row >= 0) {
        ObsMpvSource::PlaylistItem* item = source->playlist_get_item(row);
        if (item) {
            item->audio_track = track_id;
        }
    }

    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_int(s, "audio_track", track_id);
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onSubTrackChanged(int) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int track_id = m_comboSubs->currentData().toInt();
    int row = m_table->currentRow();
    if (row >= 0) {
        ObsMpvSource::PlaylistItem* item = source->playlist_get_item(row);
        if (item) {
            item->sub_track = track_id;
        }
    }

    obs_data_t *s = obs_source_get_settings(m_currentSource);
    obs_data_set_int(s, "subtitle_track", track_id);
    obs_source_update(m_currentSource, s);
    obs_data_release(s);
}

void MpvControlDock::onLoopCountChanged(int v) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int row = m_table->currentRow();
    if (row >= 0) {
        ObsMpvSource::PlaylistItem* item = source->playlist_get_item(row);
        if (item) {
            item->loop_count = v;
            item->loop = (v != 0); // Keep boolean for compatibility
        }
    }
    
    // Update visuals
    updatePlaylistTable();
}

void MpvControlDock::onFadeInToggled(bool checked) {
    m_spinFadeIn->setEnabled(checked);
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;
    int row = m_table->currentRow();
    if (row >= 0) {
        auto* item = source->playlist_get_item(row);
        if (item) item->fade_in_enabled = checked;
    }
}

void MpvControlDock::onFadeInChanged(double v) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;
    int row = m_table->currentRow();
    if (row >= 0) {
        auto* item = source->playlist_get_item(row);
        if (item) item->fade_in = v;
    }
}

void MpvControlDock::onFadeOutToggled(bool checked) {
    m_spinFadeOut->setEnabled(checked);
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;
    int row = m_table->currentRow();
    if (row >= 0) {
        auto* item = source->playlist_get_item(row);
        if (item) item->fade_out_enabled = checked;
    }
}

void MpvControlDock::onFadeOutChanged(double v) {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;
    int row = m_table->currentRow();
    if (row >= 0) {
        auto* item = source->playlist_get_item(row);
        if (item) item->fade_out = v;
    }
}

void MpvControlDock::onLoadSubsClicked() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    QString f = QFileDialog::getOpenFileName(this, "Load Subtitle File", "", "Subtitles (*.srt *.ass *.vtt *.sub);;All Files (*.*)");
    if (!f.isEmpty()) {
        int row = m_table->currentRow();
        if (row >= 0) {
            ObsMpvSource::PlaylistItem* item = source->playlist_get_item(row);
            if (item) {
                item->ext_sub_path = f.toStdString();
            }
        }

        obs_data_t *s = obs_source_get_settings(m_currentSource);
        obs_data_set_string(s, "load_subtitle", f.toUtf8().constData());
        obs_source_update(m_currentSource, s);
        obs_data_release(s);
        updatePlaylistTable(); // Update to show subs status
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

// Playlist
void MpvControlDock::onAddFiles() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    QStringList files = QFileDialog::getOpenFileNames(this, "Add Media Files", "", "All Files (*.*)");
    if (files.isEmpty()) return;

    std::vector<std::string> paths;
    for (const QString &file : files) {
        paths.push_back(file.toStdString());
    }
    source->playlist_add_multiple(paths);
    updatePlaylistTable();
}

void MpvControlDock::onRemoveFile() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int_fast64_t row = m_table->currentRow();
    if (row >= 0) {
        source->playlist_remove(row);
        updatePlaylistTable();
    }
}

void MpvControlDock::onMoveUp() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int_fast64_t row = m_table->currentRow();
    if (row > 0) {
        source->playlist_move(row, row - 1);
        updatePlaylistTable();
        m_table->setCurrentCell(row - 1, 0);
    }
}

void MpvControlDock::onMoveDown() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) return;

    int_fast64_t row = m_table->currentRow();
    if (row >= 0 && row < m_table->rowCount() - 1) {
        source->playlist_move(row, row + 1);
        updatePlaylistTable();
        m_table->setCurrentCell(row + 1, 0);
    }
}

void MpvControlDock::onItemClicked(QTableWidgetItem *item) {
    Q_UNUSED(item);
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source || !item) return;

    int row = item->row();
    ObsMpvSource::PlaylistItem* playlist_item = source->playlist_get_item(row);
    if (!playlist_item) return;

    m_sliderVolume->blockSignals(true);
    m_sliderVolume->setValue(playlist_item->volume);
    m_sliderVolume->blockSignals(false);

    m_spinLoop->blockSignals(true);
    m_spinLoop->setValue(playlist_item->loop_count);
    m_spinLoop->blockSignals(false);

    m_checkFadeIn->blockSignals(true);
    m_checkFadeIn->setChecked(playlist_item->fade_in_enabled);
    m_checkFadeIn->blockSignals(false);
    m_spinFadeIn->setEnabled(playlist_item->fade_in_enabled);

    m_spinFadeIn->blockSignals(true);
    m_spinFadeIn->setValue(playlist_item->fade_in);
    m_spinFadeIn->blockSignals(false);

    m_checkFadeOut->blockSignals(true);
    m_checkFadeOut->setChecked(playlist_item->fade_out_enabled);
    m_checkFadeOut->blockSignals(false);
    m_spinFadeOut->setEnabled(playlist_item->fade_out_enabled);

    m_spinFadeOut->blockSignals(true);
    m_spinFadeOut->setValue(playlist_item->fade_out);
    m_spinFadeOut->blockSignals(false);

    auto populate = [](QComboBox *combo, const std::vector<ObsMpvSource::MpvTrack> &tracks, int current_id) {
        combo->blockSignals(true);
        combo->clear();
        combo->addItem("None", -1);
        for (const auto &t : tracks) {
            combo->addItem(QString::fromStdString(t.name), t.id);
            if (t.id == current_id) combo->setCurrentIndex(combo->count() - 1);
        }
        combo->blockSignals(false);
    };

    populate(m_comboAudio, playlist_item->audio_tracks, playlist_item->audio_track);
    populate(m_comboSubs, playlist_item->sub_tracks, playlist_item->sub_track);
}

void MpvControlDock::updatePlaylistTable() {
    ObsMpvSource *source = getCurrentMpvSource();
    if (!source) {
        m_table->setRowCount(0);
        return;
    }

    m_table->setRowCount(source->playlist_count());
    double total_duration = 0.0;

    auto createItem = [](const QString &text) {
        QTableWidgetItem *item = new QTableWidgetItem(text);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        return item;
    };

    for (int i = 0; i < source->playlist_count(); ++i) {
        auto *item = source->playlist_get_item(i);
        if (item) {
            QString displayName = QString::fromStdString(item->name);
            bool isCurrent = (i == source->m_current_index);

            if (isCurrent) {
                if (source->is_playing()) displayName = "▶ " + displayName;
                else displayName = "⏸ " + displayName;
            }

            m_table->setItem(i, 0, createItem(displayName));
            m_table->setItem(i, 1, createItem(formatTime(item->duration)));

            m_table->setItem(i, 2, createItem(item->fps > 0 ? QString::number(item->fps, 'f', 2) : ""));
            m_table->setItem(i, 3, createItem(item->audio_channels > 0 ? QString::number(item->audio_channels) : ""));

            QString loopStr;
            if (item->loop_count < 0) loopStr = "∞";
            else if (item->loop_count == 0) loopStr = "1";
            else loopStr = QString::number(item->loop_count);
            m_table->setItem(i, 4, createItem(loopStr));

            QString subText = "No";
            if (!item->ext_sub_path.empty()) subText = "Ext";
            else if (!item->sub_tracks.empty()) subText = "Int";

            m_table->setItem(i, 5, createItem(subText));

            if (isCurrent) {
                QFont font = m_table->font();
                font.setBold(true);
                // Highlight color using OBS theme compatible logic usually, 
                // but for now a simple transparent blue is okay.
                QColor activeColor(0, 120, 215, 60); 

                for (int c = 0; c < 6; c++) {
                    QTableWidgetItem *cell = m_table->item(i, c);
                    if (cell) {
                        cell->setFont(font);
                        cell->setBackground(activeColor);
                        // Also set text color to ensure contrast? 
                        // Often better to leave text color for theme compatibility.
                    }
                }
            }

            total_duration += item->duration;
        }
    }
    m_labelTotalDuration->setText("Total Duration: " + formatTime(total_duration));
}

ObsMpvSource* MpvControlDock::getCurrentMpvSource() {
    if (!m_currentSource) return nullptr;
    return static_cast<ObsMpvSource*>(obs_obj_get_data(m_currentSource));
}

QString MpvControlDock::formatTime(double seconds) {
    int h = (int)(seconds / 3600);
    int m = (int)((seconds - (h * 3600)) / 60);
    int s = (int)(seconds - (h * 3600) - (m * 60));
    return QString("%1:%2:%3").arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
double MpvControlDock::parseTime(const QString &) {return 0;}
void MpvControlDock::populateTracks(QComboBox *, const char *) {}
