#include "StatusLed.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QTimerEvent>
#include <cmath>

StatusLed::StatusLed(QWidget *parent)
    : QWidget(parent)
{
    setFixedSize(14, 14);
}

void StatusLed::setState(State s)
{
    if (m_state == s) return;
    m_state = s;

    if (m_timerId) {
        killTimer(m_timerId);
        m_timerId = 0;
    }

    if (s == State::Running) {
        m_pulse   = 0.f;
        m_pulseUp = true;
        m_timerId = startTimer(40); // 25 fps is plenty for a pulse
    }

    update();
}

void StatusLed::timerEvent(QTimerEvent *)
{
    constexpr float step = 0.06f;
    if (m_pulseUp) {
        m_pulse += step;
        if (m_pulse >= 1.f) { m_pulse = 1.f; m_pulseUp = false; }
    } else {
        m_pulse -= step;
        if (m_pulse <= 0.f) { m_pulse = 0.f; m_pulseUp = true; }
    }
    update();
}

void StatusLed::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor core, glow;
    float  glowAlpha = 0.f;

    switch (m_state) {
    case State::Idle:
        core      = QColor(0x3a, 0x3f, 0x50);
        glow      = core;
        glowAlpha = 0.f;
        break;
    case State::Ready:
        core      = QColor(0x4a, 0x9e, 0xff);
        glow      = QColor(0x4a, 0x9e, 0xff);
        glowAlpha = 0.35f;
        break;
    case State::Running: {
        // Interpolate green brightness with pulse
        int   g   = static_cast<int>(180 + 75 * m_pulse);
        core      = QColor(0x10, g, 0x50);
        glow      = QColor(0x50, 0xfa, 0x7b);
        glowAlpha = 0.15f + 0.45f * m_pulse;
        break;
    }
    case State::Error:
        core      = QColor(0xff, 0x55, 0x55);
        glow      = QColor(0xff, 0x55, 0x55);
        glowAlpha = 0.45f;
        break;
    }

    const QRectF r(0, 0, width(), height());
    const QPointF center = r.center();
    const float   radius = width() / 2.f - 1.f;

    // Outer glow
    if (glowAlpha > 0.f) {
        QRadialGradient rg(center, radius * 2.2f, center);
        QColor gc = glow;
        gc.setAlphaF(glowAlpha);
        rg.setColorAt(0.0, gc);
        gc.setAlphaF(0.f);
        rg.setColorAt(1.0, gc);
        p.setPen(Qt::NoPen);
        p.setBrush(rg);
        p.drawEllipse(center, radius * 2.2f, radius * 2.2f);
    }

    // Core disc with radial gradient (gives a shiny dome feel)
    QRadialGradient cg(center - QPointF(radius * 0.25f, radius * 0.30f), radius);
    QColor light = core.lighter(160);
    cg.setColorAt(0.0, light);
    cg.setColorAt(0.6, core);
    cg.setColorAt(1.0, core.darker(130));

    p.setPen(QPen(core.darker(170), 1.0));
    p.setBrush(cg);
    p.drawEllipse(center, radius, radius);
}
