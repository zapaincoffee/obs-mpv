#include "mpv-sub-dialog.hpp"
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QLabel>

MpvSubSettingsDialog::MpvSubSettingsDialog(QWidget *parent) : QDialog(parent) {
	setWindowTitle("Subtitle Settings");
	setMinimumWidth(300);

	QFormLayout *layout = new QFormLayout(this);

	m_spinDelay = new QDoubleSpinBox(this);
	m_spinDelay->setRange(-600.0, 600.0);
	m_spinDelay->setSingleStep(0.1);
	m_spinDelay->setSuffix(" s");
	layout->addRow("Delay (+/-):", m_spinDelay);

	m_spinScale = new QDoubleSpinBox(this);
	m_spinScale->setRange(0.1, 10.0);
	m_spinScale->setSingleStep(0.1);
	m_spinScale->setValue(1.0);
	layout->addRow("Scale:", m_spinScale);

	m_spinPos = new QDoubleSpinBox(this);
	m_spinPos->setRange(0.0, 150.0);
	m_spinPos->setSingleStep(1.0);
	m_spinPos->setValue(100.0); // Default MPV
	layout->addRow("Vertical Position:", m_spinPos);

	connect(m_spinDelay, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onDelayChanged);
	connect(m_spinScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onScaleChanged);
	connect(m_spinPos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onPosChanged);
}

void MpvSubSettingsDialog::setSource(obs_source_t *source) {
	if (m_source) obs_source_release(m_source);
	m_source = source;
	if (m_source) obs_source_get_ref(m_source);
	
	// Ideally read current values here from settings, but defaults are ok for now
}

void MpvSubSettingsDialog::updateSetting(const char *key, double val) {
	if (!m_source) return;
	obs_data_t *s = obs_source_get_settings(m_source);
	obs_data_set_double(s, key, val);
	obs_source_update(m_source, s);
	obs_data_release(s);
}

void MpvSubSettingsDialog::onDelayChanged(double val) { updateSetting("sub_delay", val); }
void MpvSubSettingsDialog::onScaleChanged(double val) { updateSetting("sub_scale", val); }
void MpvSubSettingsDialog::onPosChanged(double val) { updateSetting("sub_pos", val); }
