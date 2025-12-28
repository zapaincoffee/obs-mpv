#include "mpv-sub-dialog.hpp"
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QFontDialog>
#include <QColorDialog>
#include "obs-mpv-source.hpp"

MpvSubSettingsDialog::MpvSubSettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("Subtitle Settings");
    setMinimumWidth(350);

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
    m_spinPos->setValue(100.0);
    layout->addRow("Vertical Position:", m_spinPos);
    
    // Style Controls
    m_btnFont = new QPushButton("Arial", this);
    layout->addRow("Font:", m_btnFont);
    
    m_spinFontSize = new QSpinBox(this);
    m_spinFontSize->setRange(10, 200);
    m_spinFontSize->setValue(55);
    layout->addRow("Font Size:", m_spinFontSize);
    
    m_btnColor = new QPushButton(this);
    m_btnColor->setStyleSheet("background-color: #FFFFFF;");
    layout->addRow("Color:", m_btnColor);
    
    m_btnShadowColor = new QPushButton(this);
    m_btnShadowColor->setStyleSheet("background-color: #000000;");
    layout->addRow("Shadow Color:", m_btnShadowColor);
    
    m_spinShadowOffset = new QSpinBox(this);
    m_spinShadowOffset->setRange(0, 20);
    m_spinShadowOffset->setValue(2);
    layout->addRow("Shadow Offset:", m_spinShadowOffset);

    connect(m_spinDelay, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onDelayChanged);
    connect(m_spinScale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onScaleChanged);
    connect(m_spinPos, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MpvSubSettingsDialog::onPosChanged);
    
    connect(m_btnFont, &QPushButton::clicked, this, &MpvSubSettingsDialog::onFontClicked);
    connect(m_btnColor, &QPushButton::clicked, this, &MpvSubSettingsDialog::onColorClicked);
    connect(m_btnShadowColor, &QPushButton::clicked, this, &MpvSubSettingsDialog::onShadowColorClicked);
    connect(m_spinFontSize, QOverload<int>::of(&QSpinBox::valueChanged), this, &MpvSubSettingsDialog::onFontSizeChanged);
    connect(m_spinShadowOffset, QOverload<int>::of(&QSpinBox::valueChanged), this, &MpvSubSettingsDialog::onShadowOffsetChanged);
}

void MpvSubSettingsDialog::setSource(obs_source_t *source) {
    if (m_source) obs_source_release(m_source);
    m_source = source;
    if (m_source) obs_source_get_ref(m_source);

    if (m_source) {
        ObsMpvSource *s = static_cast<ObsMpvSource*>(obs_obj_get_data(m_source));
        if (s) {
            auto style = s->get_sub_style();
            m_currentFont.setFamily(QString::fromStdString(style.font));
            m_btnFont->setText(m_currentFont.family());
            
            m_subColor = QColor(QString::fromStdString(style.color));
            m_btnColor->setStyleSheet(QString("background-color: %1;").arg(m_subColor.name()));
            
            m_shadowColor = QColor(QString::fromStdString(style.shadow_color));
            m_btnShadowColor->setStyleSheet(QString("background-color: %1;").arg(m_shadowColor.name()));
            
            m_spinFontSize->setValue(style.font_size);
            m_spinShadowOffset->setValue(style.shadow_offset);
        }
    }
}

void MpvSubSettingsDialog::updateSetting(const char *key, double val) {
    if (!m_source) return;
    obs_data_t *s = obs_source_get_settings(m_source);
    obs_data_set_double(s, key, val);
    obs_source_update(m_source, s);
    obs_data_release(s);
}

void MpvSubSettingsDialog::applyStyle() {
    if (!m_source) return;
    ObsMpvSource *s = static_cast<ObsMpvSource*>(obs_obj_get_data(m_source));
    if (s) {
        ObsMpvSource::SubStyle style;
        style.font = m_currentFont.family().toStdString();
        style.color = m_subColor.name().toStdString();
        style.shadow_color = m_shadowColor.name().toStdString();
        style.font_size = m_spinFontSize->value();
        style.shadow_offset = m_spinShadowOffset->value();
        s->set_sub_style(style);
    }
}

void MpvSubSettingsDialog::onDelayChanged(double val) { updateSetting("sub_delay", val); }
void MpvSubSettingsDialog::onScaleChanged(double val) { updateSetting("sub_scale", val); }
void MpvSubSettingsDialog::onPosChanged(double val) { updateSetting("sub_pos", val); }

void MpvSubSettingsDialog::onFontClicked() {
    bool ok;
    QFont font = QFontDialog::getFont(&ok, m_currentFont, this);
    if (ok) {
        m_currentFont = font;
        m_btnFont->setText(font.family());
        applyStyle();
    }
}

void MpvSubSettingsDialog::onColorClicked() {
    QColor c = QColorDialog::getColor(m_subColor, this, "Subtitle Color");
    if (c.isValid()) {
        m_subColor = c;
        m_btnColor->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        applyStyle();
    }
}

void MpvSubSettingsDialog::onShadowColorClicked() {
    QColor c = QColorDialog::getColor(m_shadowColor, this, "Shadow Color");
    if (c.isValid()) {
        m_shadowColor = c;
        m_btnShadowColor->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        applyStyle();
    }
}

void MpvSubSettingsDialog::onFontSizeChanged(int) { applyStyle(); }
void MpvSubSettingsDialog::onShadowOffsetChanged(int) { applyStyle(); }
