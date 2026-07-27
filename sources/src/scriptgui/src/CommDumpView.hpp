#pragma once
#include <QFrame>
#include <QByteArray>
#include <QString>
#include <QHash>

class CommDumpModel;
class QTreeView;
class QLabel;
class QPushButton;
class QCheckBox;
class QComboBox;
class QToolButton;
class QMenu;
class QAction;
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
//
//  Rows can be filtered by direction (Rx/Tx) AND by plugin name — the plugin
//  filter menu is built up dynamically as new plugin names are observed via
//  addRecord(), each one starting checked (visible). Traces can be saved to
//  / reloaded from a JSON file, either the full set or only what currently
//  passes both filters.
// ─────────────────────────────────────────────────────────────────────────────
class CommDumpView : public QFrame
{
    Q_OBJECT
public:
    explicit CommDumpView(QWidget *parent = nullptr);

    // plugin: e.g. "uart0". details: pre-formatted per plugin type (comm
    // port / "ip:port" / i2c addr / "SPI0 CS1" / CAN id / ...).
    // Timestamp is captured internally, at call time (microsecond resolution).
    void addRecord(const QString &plugin, const QString &details, bool isTx,
                    const QByteArray &data);

    void clear();
    void setDumpFont(const QFont &font);

public slots:
    void setAutoScroll(bool on) { m_autoScroll = on; }
    void setTreeFont(const QFont &font);

private slots:
    void onSaveAll();
    void onSaveFilteredOnly();
    void onLoadTriggered();
    void onPluginActionToggled(bool checked);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateCountLabel();
    void ensurePluginKnown(const QString &plugin);
    bool rowPassesFilters(int row) const;
    void reapplyAllFilters();
    void rebuildPluginMenuFromModel();
    void saveToFile(bool filteredOnly);
    void updateFullDumpFontSize();


    CommDumpModel *m_model;
    QTreeView     *m_tree;
    QLabel        *m_titleLabel;
    QLabel        *m_countLabel;
    QComboBox     *m_dirFilterCb;      // All / Rx only / Tx only
    QToolButton   *m_pluginFilterBtn;  // dropdown: checkbox per known plugin
    QMenu         *m_pluginMenu;
    QHash<QString, QAction *> m_pluginActions;   // plugin name -> its checkable action
    QCheckBox     *m_asciiCb;          // toggles the ASCII column on/off
    QCheckBox     *m_autoScrollCb;
    QToolButton   *m_saveBtn;          // dropdown: Save All / Save Filtered Only
    QMenu         *m_saveMenu;
    QPushButton   *m_loadBtn;
    QPushButton   *m_clearBtn;
    bool           m_autoScroll = true;

    // Proportion of the tree's base font size used for the full hex-dump
    // child row (e.g. 0.8 = 80%). This is the single source of truth for
    // that ratio; updateFullDumpFontSize() multiplies it by the tree's
    // *current* font size and pushes the resulting absolute point size into
    // the model via setFullDumpFontSize(). It must never be derived back
    // from the model's own fullDumpFontSize() getter — that returns an
    // absolute size, not a proportion, and feeding it back in would compound
    // on every font-size recalculation.
    double m_fullDumpFontProportion = 0.8;
};
