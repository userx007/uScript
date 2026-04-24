#pragma once
#include <QWidget>
#include <QColor>

/**
 * @brief Tiny circular LED indicator widget.
 *
 * States:
 *   Idle    – dim grey
 *   Ready   – blue
 *   Running – animated pulsing green
 *   Error   – red
 */
class StatusLed : public QWidget
{
    Q_OBJECT
public:
    enum class State { Idle, Ready, Running, Error };

    explicit StatusLed(QWidget *parent = nullptr);

    void setState(State s);
    State state() const { return m_state; }

    QSize sizeHint() const override { return {14, 14}; }
    QSize minimumSizeHint() const override { return {14, 14}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void timerEvent(QTimerEvent *) override;

private:
    State   m_state     = State::Idle;
    int     m_timerId   = 0;
    float   m_pulse     = 0.f;    // 0..1 for running animation
    bool    m_pulseUp   = true;
};
