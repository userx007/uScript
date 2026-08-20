#pragma once
#include <QFrame>
#include <QByteArray>
#include <QString>
#include <QHash>
#include <QList>
#include <QVector>
#include <QPoint>
#include "CommDumpModel.hpp"   // needed for CommDumpModel::PendingRecord (m_pendingQueue member)

class QTreeView;
class QTimer;
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
//  Always visible (not collapsible). Retention is bounded (see
//  kDefaultMaxRecords / CommDumpModel::setMaxRecords()) rather than
//  unlimited, so a long or very high-rate capture can't grow memory/row
//  count without bound; CLEAR still drops everything immediately as before.
//
//  MainWindow feeds this from dispatchLine() on GUI:COMM_DUMP:<base64> —
//  decode + unpack happens in MainWindow (it already owns all base64/GUI:
//  parsing), this widget only ever sees plain Qt types.
//
//  Ingestion is coalesced: addRecord() does NOT push straight into the
//  model/tree. It appends to m_pendingQueue and arms m_flushTimer (or, if
//  the queue has grown past kForceFlushThreshold, flushes immediately); the
//  actual model insert + view bookkeeping (filter visibility, column
//  spanning, count label, auto-scroll) happens once per flush in
//  flushPending(), covering however many records arrived during that
//  window. This turns "N records arrived" from N full view transactions
//  into O(N / batch size), which is what keeps a very high acquisition rate
//  from throttling the GUI event loop (and, transitively, from stalling the
//  interpreter process if its stdout pipe backs up waiting for the GUI to
//  keep draining it).
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
    // timestampUs: microseconds since the Unix epoch, as captured by the
    // producer process at the moment the event was observed (decoded from
    // the GUI:COMM_DUMP wire payload — see ICommDumpProtocol.hpp). Passed
    // straight through to the model rather than re-stamped with "now" here,
    // so this panel's Timestamp column stays on the same time base as the
    // Log panel's.
    void addRecord(qint64 timestampUs, const QString &plugin, const QString &details, bool isTx,
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
    void onCopySelected();
    void onSelectAllRows();
    void onExpandAll();
    void onCollapseAll();
    void onTreeContextMenuRequested(const QPoint &pos);
    // Coalesced-flush timer target — see m_pendingQueue / m_flushTimer.
    void flushPending();

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
    // Applies filter-visibility + first-column-spanning to the freshly
    // inserted row range [first, last] (inclusive), and grows the plugin
    // filter menu for any new plugin names among them. Shared by
    // flushPending() (live capture) and onLoadTriggered() (reload).
    void prepareNewRows(int first, int last);

    // Copy-to-clipboard support. Selection can straddle both top-level
    // record rows and their expanded full-dump child row; selectedRecordRows()
    // collapses that down to the (deduplicated, ascending) set of record row
    // numbers involved. buildCopyText() then renders those records as plain
    // text in the tree's current column order, always using the *full*
    // hex+ASCII dump (via CommDumpModel::fullDumpForRow()) regardless of
    // whether that row happens to be expanded on screen right now.
    QList<int> selectedRecordRows() const;
    QString buildCopyText(const QList<int> &rows) const;

    // ── Coalesced ingestion (see class comment) ────────────────────────────
    // Default cap handed to CommDumpModel::setMaxRecords() in the ctor:
    // generous enough that ordinary sessions never notice it, but bounded so
    // an unattended very-long or very-high-rate capture can't run the
    // process out of memory. 0 would mean unlimited (the model's own
    // default) — deliberately not used here.
    static constexpr int kDefaultMaxRecords = 200'000;
    // How long a burst of incoming records is allowed to accumulate before
    // being flushed as one batch. Short enough that "live" traces still feel
    // live; long enough to meaningfully coalesce a fast burst.
    static constexpr int kFlushIntervalMs = 30;
    // Safety valve: if the queue grows past this many *pending* (not yet
    // flushed) records before the timer fires, flush immediately instead of
    // letting the queue itself become an unbounded buffer during a
    // pathological burst.
    static constexpr int kForceFlushThreshold = 5000;

    QVector<CommDumpModel::PendingRecord> m_pendingQueue;
    QTimer *m_flushTimer;

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
    QMenu         *m_treeContextMenu;  // right-click menu: Copy / Select All / Expand All / Collapse All
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
