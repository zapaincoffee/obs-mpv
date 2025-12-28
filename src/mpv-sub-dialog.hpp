#pragma once
#include <QDialog>
#include <obs.h>
#include <QFont>
#include <QColor>

class QDoubleSpinBox;
class QPushButton;
class QSpinBox;

class MpvSubSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MpvSubSettingsDialog(QWidget *parent = nullptr);
    void setSource(obs_source_t *source);

    private slots:
    void onDelayChanged(double val);
    void onScaleChanged(double val);
    void onPosChanged(double val);
    
    void onFontClicked();
    void onColorClicked();
    void onShadowColorClicked();
    void onFontSizeChanged(int val);
    void onShadowOffsetChanged(int val);

private:
    obs_source_t *m_source = nullptr;
    QDoubleSpinBox *m_spinDelay;
    QDoubleSpinBox *m_spinScale;
    QDoubleSpinBox *m_spinPos;
    
    // Style UI
    QPushButton *m_btnFont;
    QPushButton *m_btnColor;
    QPushButton *m_btnShadowColor;
    QSpinBox *m_spinFontSize;
    QSpinBox *m_spinShadowOffset;
    
    // State
    QFont m_currentFont;
    QColor m_subColor;
    QColor m_shadowColor;

    void updateSetting(const char *key, double val);
    void applyStyle();
};
