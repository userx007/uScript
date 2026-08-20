#pragma once
#include <QAbstractItemModel>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QColor>
#include <cstdint>
#include <QFont>

// ─────────────────────────────────────────────────────────────────────────────
//  CommDumpModel — one row per plugin Rx/Tx event, with a single collapsible
//  child row (index 0 of the parent) holding the full hex+ASCII dump.
//
//  Columns:
//    0 Timestamp | 1 Plugin | 2 Details | 3 Dir | 4 Length | 5 Data | 6 ASCII
//
//  Top-level rows show only the first k_previewBytes bytes of the payload in
//  column 5 (hex only, plus a "…" marker when truncated). Column 6 shows the
//  ASCII translation of the same preview bytes; it is only ever computed when
//  the view has ASCII display enabled (see setShowAscii()) — nothing about
//  the wire format carries ASCII, it is derived from the raw bytes locally,
//  entirely on the GUI side.
//
//  Expanding a row reveals its child, a single row spanning the full width
//  (via QTreeView's setFirstColumnSpanned) with the complete hex+ASCII dump
//  in a monospace, word-wrapped label. Nothing is re-fetched on expand: the
//  full payload is already resident in the record (it arrived complete in
//  the GUI:COMM_DUMP line — see ICommDumpProtocol.hpp), so expand/collapse
//  is a pure view-side operation with no cost beyond formatting the string
//  once, lazily.
// ─────────────────────────────────────────────────────────────────────────────
class CommDumpModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column { ColTimestamp = 0, ColPlugin, ColDetails, ColDir, ColLength, ColData, ColAscii, ColCount };
    // TimeWallClock:        wall-clock time the record was stamped ("HH:mm:ss.uuuuuu").
    // TimeDeltaPrevious:    delta since the *previous* top-level record, as
    //                       "S.uuuuuu" seconds (row 0's delta is always 0).
    // TimeSinceCaptureStart: delta since the *first* top-level record, also
    //                       as "S.uuuuuu" seconds (row 0 is always 0).
    // All three are derived on the fly from Record::timestampUs, so no extra
    // data needs to be stored/persisted to support any of them.
    // TimeFormatCount is a sentinel (not a real mode) so the view can cycle
    // through the real modes with a plain "% TimeFormatCount" without a
    // separately-maintained count.
    enum TimeFormat { TimeWallClock = 0, TimeDeltaPrevious, TimeSinceCaptureStart, TimeFormatCount };
    static constexpr int k_previewBytes = 8;

    struct Record {
        // Microseconds since the Unix epoch, as captured by the *producer*
        // process (the interpreter/plugin, at the moment the Rx/Tx event was
        // observed) and passed in verbatim by addRecord() — see
        // ICommDumpProtocol.hpp / uGuiNotify.hpp::gui_notify_comm_dump(). Not
        // the GUI's own receipt time, so this stays on the same clock basis
        // as the Log panel's timestamps (both use std::chrono::system_clock).
        qint64    timestampUs = 0;
        QString   plugin;
        QString   details;     // already formatted (comm port / ip:port / i2c addr / ...)
        bool      isTx = false;
        QByteArray data;
        mutable QString fullDumpCache;   // lazily built on first expand
    };

    // Plain-data staging struct for addRecords() — lets a caller (see
    // CommDumpView's coalesced ingestion queue) accumulate several records
    // off to the side and hand them to the model in one shot, instead of
    // paying one full insert transaction (and one view relayout) per record.
    struct PendingRecord {
        qint64     timestampUs = 0;
        QString    plugin;
        QString    details;
        bool       isTx = false;
        QByteArray data;
    };

    explicit CommDumpModel(QObject *parent = nullptr);

    // Appends one record. timestampUs is the producer's own timestamp
    // (microseconds since the Unix epoch, captured when the event actually
    // happened — see ICommDumpProtocol.hpp), not a "now" stamped here, so
    // that this column shares a common time base with the Log panel.
    // Equivalent to addRecords() with a single-element list — kept as a
    // convenience for callers that only ever add one record at a time.
    void addRecord(qint64 timestampUs, const QString &plugin, const QString &details, bool isTx,
                    const QByteArray &data);

    // Appends every record in `pending` inside a single beginInsertRows()/
    // endInsertRows() pair, so a burst of N records costs one view relayout
    // instead of N. If the resulting size exceeds maxRecords(), the oldest
    // records are evicted in one beginRemoveRows()/endRemoveRows() batch —
    // see setMaxRecords() for why eviction itself is batched too. No-op if
    // `pending` is empty.
    void addRecords(const QVector<PendingRecord> &pending);

    void clear();
    int  recordCount() const { return m_records.size(); }

    // Caps how many records are retained. Once recordCount() would exceed
    // this, the oldest records are dropped (ring-buffer semantics) so a
    // long-running capture can't grow memory/row-count without bound. To
    // avoid paying an O(n) front-eviction shift on almost every insert,
    // eviction only kicks in once the *hard* cap is exceeded and then trims
    // back down to ~90% of it in one batch (hysteresis) — so the expensive
    // shift happens roughly once every (10% of max) records instead of on
    // every single insert. 0 = unlimited (the previous, default behaviour).
    void setMaxRecords(int max);
    int  maxRecords() const { return m_maxRecords; }

    // Total records ever accepted by addRecord()/addRecords(), *not*
    // reduced by eviction — lets the view report "X shown, Y total, oldest
    // trimmed" instead of silently losing history with no indication.
    qint64 totalIngestedCount() const { return m_totalIngested; }

    // Whether column ColAscii is populated. When false, data() returns an
    // empty value for that column instead of computing the ASCII text, so
    // toggling it off actually avoids the per-row work, not just hides it.
    void setShowAscii(bool on);
    bool showAscii() const { return m_showAscii; }

    // Switches how column ColTimestamp is rendered (see TimeFormat above).
    // Pure display toggle: does not touch stored data, so it's cheap and
    // fully reversible, including after a trace has been reloaded from disk.
    void setTimeFormat(TimeFormat fmt);
    TimeFormat timeFormat() const { return m_timeFormat; }

    // How many bytes per line the full hex+ASCII dump (child row) wraps at
    // — 8, 16, or 32. Also reflected in the ColData header label ("Data:8" /
    // "Data:16" / "Data:32"). Like the other display toggles, this only
    // affects rendering (and clears fullDumpCache so it regenerates), not
    // stored data. Any other value is ignored (kept at the current setting).
    void setDumpBytesPerLine(int n);
    int dumpBytesPerLine() const { return m_dumpBytesPerLine; }

    // Font size (absolute point size) for the full hex dump child row. The
    // model only ever stores/uses an absolute size — converting "proportion
    // of the tree's base font" into a concrete point size is the view's job
    // (see CommDumpView::updateFullDumpFontSize()); the model must never be
    // asked to reinterpret its own stored size as a proportion again.
    void setFullDumpFontSize(double pointSize);
    double fullDumpFontSize() const { return m_fullDumpFontSize; }

    // ── Persistence (save/reload traces) ───────────────────────────────────
    // rows empty => export every record; otherwise only the given row indices
    // (used for "save filtered traces only").
    QJsonArray toJsonArray(const QList<int> &rows = {}) const;
    // Replaces all current records with the ones decoded from arr. Font size
    // is a pure display setting owned by the view, not by trace data, so it
    // isn't handled here — see CommDumpView::onLoadTriggered().
    void loadJsonArray(const QJsonArray &arr);

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // Row-level access used by the view's item delegate.
    bool isChildRow(const QModelIndex &index) const;
    const Record *recordForIndex(const QModelIndex &index) const;

    // Full hex+ASCII dump text for the record at `row`, built (and cached)
    // exactly the way the expanded child row's Qt::DisplayRole is (see
    // data()) — regardless of whether that row is currently expanded in the
    // view. Used by CommDumpView's copy-to-clipboard feature, which always
    // copies the full dump rather than the collapsed preview. Returns an
    // empty string for an out-of-range row or a record with no payload.
    QString fullDumpForRow(int row) const;

    static QString formatTimestampUs(qint64 us);   // "HH:mm:ss.mmmuuu" (microsecond resolution)
    static QString formatDurationSecUs(qint64 deltaUs);   // "S.uuuuuu" duration, e.g. "0.785645" / "99999.445678"

private:
    QJsonObject recordToJson(const Record &r) const;

    // If m_records.size() exceeds m_maxRecords (hard cap), removes the
    // oldest records in one beginRemoveRows()/endRemoveRows() batch, down
    // to the hysteresis low-water mark — see setMaxRecords().
    void evictIfNeeded();

    static QString hexOnlyPreview(const QByteArray &data, int maxBytes);
    static QString asciiOnlyPreview(const QByteArray &data, int maxBytes);
    // includeAscii: if true, appends the ASCII column "|...|"; if false, returns hex only
    // fontSize: the point size to use for the font of the full dump text
    // bytesPerLine: 8, 16, or 32 — see setDumpBytesPerLine()
    static QString hexAsciiFull(const QByteArray &data, bool includeAscii, double fontSize, int bytesPerLine);

    // Stable per-plugin colour for ColPlugin's ForegroundRole, picked
    // deterministically from a fixed palette by hashing the plugin name —
    // NOT by first-seen order. Hashing means the same plugin name gets the
    // same colour every time, in every session and every reloaded trace,
    // regardless of which plugin happened to log first or which row the
    // view painted first (first-seen order would depend on paint order,
    // since this is invoked lazily from the const data() call). Cached in
    // m_pluginColors purely so repeated lookups skip re-hashing.
    QColor colorForPlugin(const QString &plugin) const;

    QVector<Record> m_records;
    bool m_showAscii = true;
    TimeFormat m_timeFormat = TimeWallClock;
    double m_fullDumpFontSize = 10.0; // Default absolute size
    int m_dumpBytesPerLine = 16;      // 8, 16, or 32 — see setDumpBytesPerLine()

    // Cached once per setFullDumpFontSize() call (rather than rebuilt on
    // every single data()/FontRole query) — constructing a QFont from a
    // family name touches the font database, which is wasteful to repeat
    // for every cell paint of a large, fast-scrolling trace.
    QFont m_fullDumpFont;

    int   m_maxRecords = 0;       // 0 = unlimited; see setMaxRecords()
    qint64 m_totalIngested = 0;   // monotonic; see totalIngestedCount()

    mutable QHash<QString, QColor> m_pluginColors;
};
