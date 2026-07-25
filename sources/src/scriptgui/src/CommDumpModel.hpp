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
    enum TimeFormat { TimeWallClock = 0, TimeDeltaPrevious, TimeSinceCaptureStart };
    static constexpr int k_previewBytes = 8;

    struct Record {
        qint64    timestampUs = 0;   // microseconds since epoch, stamped when addRecord() is called
        QString   plugin;
        QString   details;     // already formatted (comm port / ip:port / i2c addr / ...)
        bool      isTx = false;
        QByteArray data;
        mutable QString fullDumpCache;   // lazily built on first expand
    };

    explicit CommDumpModel(QObject *parent = nullptr);

    // Appends one record; timestamp is stamped "now" (microsecond resolution)
    // inside this call.
    void addRecord(const QString &plugin, const QString &details, bool isTx,
                    const QByteArray &data);

    void clear();
    int  recordCount() const { return m_records.size(); }

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

    // Font size for the full hex dump child row.
    // Calculated as a proportion of the parent QTreeView's font size.
    void setFullDumpFontSize(double proportion);
    double fullDumpFontSize() const { return m_fullDumpFontSize; }

    // ── Persistence (save/reload traces) ───────────────────────────────────
    // rows empty => export every record; otherwise only the given row indices
    // (used for "save filtered traces only").
    QJsonArray toJsonArray(const QList<int> &rows = {}) const;
    // Replaces all current records with the ones decoded from arr.
    void loadJsonArray(const QJsonArray &arr, double fontSizeProportion = 0.8);

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

    static QString formatTimestampUs(qint64 us);   // "HH:mm:ss.mmmuuu" (microsecond resolution)
    static QString formatDurationSecUs(qint64 deltaUs);   // "S.uuuuuu" duration, e.g. "0.785645" / "99999.445678"

private:
    void appendRecord(qint64 timestampUs, const QString &plugin, const QString &details,
                       bool isTx, const QByteArray &data);
    QJsonObject recordToJson(const Record &r) const;

    static QString hexOnlyPreview(const QByteArray &data, int maxBytes);
    static QString asciiOnlyPreview(const QByteArray &data, int maxBytes);
    // includeAscii: if true, appends the ASCII column "|...|"; if false, returns hex only
    // fontSize: the point size to use for the font of the full dump text
    static QString hexAsciiFull(const QByteArray &data, bool includeAscii, double fontSize);

    // Stable per-plugin colour for ColPlugin's ForegroundRole: each new
    // plugin name gets the next colour from a fixed palette (cycling once
    // exhausted), assigned the first time it's seen and cached so the same
    // plugin always reads the same colour for the life of the model. Lazily
    // populated from data() (const), hence the mutable cache/counter below.
    QColor colorForPlugin(const QString &plugin) const;

    QVector<Record> m_records;
    bool m_showAscii = true;
    TimeFormat m_timeFormat = TimeWallClock;
    double m_fullDumpFontSize = 10.0; // Default absolute size

    mutable QHash<QString, QColor> m_pluginColors;
    mutable int m_nextPluginColorIndex = 0;
};
