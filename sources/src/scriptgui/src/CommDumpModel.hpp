#pragma once
#include <QAbstractItemModel>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────────
//  CommDumpModel — one row per plugin Rx/Tx event, with a single collapsible
//  child row (index 0 of the parent) holding the full hex+ASCII dump.
//
//  Columns:
//    0 Timestamp | 1 Plugin | 2 Details | 3 Dir | 4 Length | 5 Data (preview)
//
//  Top-level rows show only the first k_previewBytes bytes of the payload in
//  column 5 (plus a "…" marker when truncated). Expanding a row reveals its
//  child, a single row spanning the full width (via QTreeView's
//  setFirstColumnSpanned) with the complete hex+ASCII dump in a monospace,
//  word-wrapped label. Nothing is re-fetched on expand: the full payload is
//  already resident in the record (it arrived complete in the GUI:COMM_DUMP
//  line — see ICommDumpProtocol.hpp), so expand/collapse is a pure view-side
//  operation with no cost beyond formatting the string once, lazily.
// ─────────────────────────────────────────────────────────────────────────────
class CommDumpModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Column { ColTimestamp = 0, ColPlugin, ColDetails, ColDir, ColLength, ColData, ColCount };
    static constexpr int k_previewBytes = 8;

    struct Record {
        QDateTime timestamp;   // captured when addRecord() is called, per user spec
        QString   plugin;
        QString   details;     // already formatted (comm port / ip:port / i2c addr / ...)
        bool      isTx = false;
        QByteArray data;
        mutable QString fullDumpCache;   // lazily built on first expand
    };

    explicit CommDumpModel(QObject *parent = nullptr);

    // Appends one record; timestamp is stamped "now" inside this call.
    void addRecord(const QString &plugin, const QString &details, bool isTx,
                    const QByteArray &data);

    void clear();
    int  recordCount() const { return m_records.size(); }

    // QAbstractItemModel
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // Row-level access used by the view's item delegate.
    bool isChildRow(const QModelIndex &index) const;
    const Record *recordForIndex(const QModelIndex &index) const;

private:
    static QString hexAsciiPreview(const QByteArray &data, int maxBytes);
    static QString hexAsciiFull(const QByteArray &data);

    QVector<Record> m_records;
};
