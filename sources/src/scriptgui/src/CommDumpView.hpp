#pragma once
#include <QFrame>
#include <QByteArray>
#include <QString>

class CommDumpModel;
class QTreeView;
class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QFont;

// ─────────────────────────────────────────────────────────────────────────────
//  CommDumpView — header bar + QTreeView (plugin Rx/Tx traffic dump panel).
//
//  Sits between LogViewer (w3) and ShellTerminal (w4) in m_logShellSplit.
//  Always visible (not collapsible), unlimited retention until CLEAR is
//  pressed — matches LogViewer's own behaviour, so the two panels read the
//  same way to the user.
//
//  MainWindow feeds this from dispatchLine() on GUI:COMM_DUMP:<base64> —
//  decode + unpack happens in MainWindow (it already owns all base64/GUI:
//  parsing), this widget only ever sees plain Qt types.
// ─────────────────────────────────────────────────────────────────────────────
class CommDumpView : public QFrame
{
    Q_OBJECT
public:
    explicit CommDumpView(QWidget *parent = nullptr);

    // plugin: e.g. "uart0". details: pre-formatted per plugin type (comm
    // port / "ip:port" / i2c addr / "SPI0 CS1" / CAN id / ...).
    // Timestamp is captured internally, at call time.
    void addRecord(const QString &plugin, const QString &details, bool isTx,
                    const QByteArray &data);

    void clear();
    void setDumpFont(const QFont &font);

public slots:
    void setAutoScroll(bool on) { m_autoScroll = on; }

private:
    void updateCountLabel();

    CommDumpModel *m_model;
    QTreeView     *m_tree;
    QLabel        *m_titleLabel;
    QLabel        *m_countLabel;
    QComboBox     *m_dirFilterCb;   // All / Rx only / Tx only
    QCheckBox     *m_autoScrollCb;
    QPushButton   *m_clearBtn;
    bool           m_autoScroll = true;
};
