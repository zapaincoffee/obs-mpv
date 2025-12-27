#pragma once
#include <QDialog>
#include <obs.h>

class QDoubleSpinBox;

class MpvSubSettingsDialog : public QDialog {
	Q_OBJECT
public:
	explicit MpvSubSettingsDialog(QWidget *parent = nullptr);
	void setSource(obs_source_t *source);

private slots:
	void onDelayChanged(double val);
	void onScaleChanged(double val);
	void onPosChanged(double val);

private:
	obs_source_t *m_source = nullptr;
	QDoubleSpinBox *m_spinDelay;
	QDoubleSpinBox *m_spinScale;
	QDoubleSpinBox *m_spinPos;
	
	void updateSetting(const char *key, double val);
};
