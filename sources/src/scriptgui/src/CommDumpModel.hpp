#pragma once
#include <QAbstractItemModel>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdint>

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

    // ── Persistence (save/reload traces) ───────────────────────────────────
    // rows empty => export every record; otherwise only the given row indices
    // (used for "save filtered traces only").
    QJsonArray toJsonArray(const QList<int> &rows = {}) const;
    // Replaces all current records with the ones decoded from arr.
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

    static QString formatTimestampUs(qint64 us);   // "HH:mm:ss.mmmuuu" (microsecond resolution)

private:
    void appendRecord(qint64 timestampUs, const QString &plugin, const QString &details,
                       bool isTx, const QByteArray &data);
    QJsonObject recordToJson(const Record &r) const;

    static QString hexOnlyPreview(const QByteArray &data, int maxBytes);
    static QString asciiOnlyPreview(const QByteArray &data, int maxBytes);
    // includeAscii: if true, appends the ASCII column "|...|"; if false, returns hex only
    static QString hexAsciiFull(const QByteArray &data, bool includeAscii);

    QVector<Record> m_records;
    bool m_showAscii = true;
};
