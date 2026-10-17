#pragma once
#include <QColor>
#include <QSize>
#include <QWidget>

class QPaintEvent;
class QTimerEvent;

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
        enum class State
        {
            Idle,
            Ready,
            Running,
            Error
        };

        /** Creates a 14x14 LED. */
        explicit StatusLed(QWidget *pParent = nullptr);

        /** Sets the LED state (starting or stopping the blink timer). */
        void setState(State eS);

        /** Returns the LED state. */
        State state() const
        {
            return m_state;
        }

        /** Returns the fixed LED size. */
        QSize sizeHint() const override
        {
            return {14, 14};
        }

        /** Returns the fixed LED size. */
        QSize minimumSizeHint() const override
        {
            return {14, 14};
        }

    protected:
        void paintEvent(QPaintEvent *) override;
        /** Toggles the blink phase. */
        void timerEvent(QTimerEvent *) override;

    private:
        State m_state  = State::Idle;
        int m_timerId  = 0;
        float m_pulse  = 0.f; // 0..1 for running animation
        bool m_pulseUp = true;
};
