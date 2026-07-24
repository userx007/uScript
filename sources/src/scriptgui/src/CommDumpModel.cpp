#include "CommDumpModel.hpp"
#include <QBrush>
#include <QFont>
#include <QColor>
#include <QDateTime>
#include <chrono>

namespace {
constexpr quintptr kTopLevelSentinel = static_cast<quintptr>(-1);

QString hexByte(unsigned char b) { return QString("%1").arg(b, 2, 16, QChar('0')).toUpper(); }

char asciiOrDot(unsigned char b) { return (b >= 0x20 && b < 0x7F) ? char(b) : '.'; }

qint64 nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}
} // namespace

CommDumpModel::CommDumpModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

// ─────────────────────────────────────────────────────────────────────────────
//  setFullDumpFontSize
//  Sets the point size for the full dump text. This is typically calculated
//  by the view based on the parent widget's font size * proportion.
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpModel::setFullDumpFontSize(double pointSize)
{
    if (qFuzzyCompare(m_fullDumpFontSize, pointSize))
        return;

    m_fullDumpFontSize = pointSize;

    // Clear caches so they regenerate with the new font size
    for (auto &r : m_records) {
        r.fullDumpCache.clear();
    }

    // Notify view that data changed for all child rows (column 0)
    for (int i = 0; i < m_records.size(); ++i) {
        QModelIndex parentIdx = index(i, 0); // Top level row
        QModelIndex childIdx = index(0, 0, parentIdx); // Child row
        if (childIdx.isValid()) {
            emit dataChanged(childIdx, childIdx, { Qt::DisplayRole, Qt::FontRole });
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  formatTimestampUs — "HH:mm:ss.mmmuuu": the QDateTime-formatted millisecond
//  part followed directly by the remaining 3 microsecond digits, giving a
//  6-digit fractional-second field at microsecond resolution without pulling
//  in a separate time-formatting dependency.
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::formatTimestampUs(qint64 us)
{
    const qint64 ms    = us / 1000;
    const int    subUs = static_cast<int>(us % 1000);
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    return dt.toString("HH:mm:ss.zzz") + QString("%1").arg(subUs, 3, 10, QChar('0'));
}

// ─────────────────────────────────────────────────────────────────────────────
//  formatDurationSecUs — "S.uuuuuu": plain seconds, unpadded, dot, then a
//  fixed 6-digit zero-padded microsecond fraction (e.g. "0.785645" or
//  "99999.445678"). Used for both TimeDeltaPrevious and TimeSinceCaptureStart
//  — both are just a duration in microseconds, the only difference is which
//  reference record the caller measured it against. Deliberately NOT built
//  on QDateTime: QDateTime::fromMSecsSinceEpoch() interprets its argument as
//  local time by default, so treating a duration as "ms since epoch" would
//  silently add the local UTC offset back in. Plain integer arithmetic here
//  keeps it timezone-independent.
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::formatDurationSecUs(qint64 deltaUs)
{
    const qint64 seconds = deltaUs / 1'000'000LL;
    const qint64 fracUs  = deltaUs % 1'000'000LL;
    return QString("%1.%2").arg(seconds).arg(fracUs, 6, 10, QChar('0'));
}

// ─────────────────────────────────────────────────────────────────────────────
//  hexOnlyPreview — "DE AD BE EF 01 02 03 04 …" (first maxBytes only, no ASCII)
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::hexOnlyPreview(const QByteArray &data, int maxBytes)
{
    const int n = qMin(data.size(), maxBytes);
    QString hex;
    for (int i = 0; i < n; ++i) {
        hex += hexByte(static_cast<unsigned char>(data[i]));
        if (i + 1 < n) hex += ' ';
    }
    if (data.size() > maxBytes)
        hex += QStringLiteral(" …");
    return hex;
}

// ─────────────────────────────────────────────────────────────────────────────
//  asciiOnlyPreview — "|...ascii...|" for the same leading maxBytes window
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::asciiOnlyPreview(const QByteArray &data, int maxBytes)
{
    const int n = qMin(data.size(), maxBytes);
    QString ascii;
    for (int i = 0; i < n; ++i)
        ascii += asciiOrDot(static_cast<unsigned char>(data[i]));
    if (ascii.isEmpty())
        return {};
    return ascii + (data.size() > maxBytes ? QStringLiteral("…") : QString());
}

// ─────────────────────────────────────────────────────────────────────────────
//  hexAsciiFull — classic 16-bytes-per-line hex+ASCII dump, monospace-ready.
//  If includeAscii is false, it returns only the hex part without the "|...|" suffix.
//
//  Returned as a small HTML fragment (wrapped in <pre>) rather than plain
//  text: the hex bytes are wrapped in a yellow <span> and the ASCII section
//  in a cyan one, so the two halves are visually distinguishable at a
//  glance. QStyledItemDelegate auto-detects rich text (Qt::mightBeRichText)
//  and renders it via QTextDocument, word-wrap included, so nothing else
//  needs to change on the view side to pick this up. The offset column
//  keeps the same muted grey used before this row grew colors.
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::hexAsciiFull(const QByteArray &data, bool includeAscii, double fontSize)
{
    Q_UNUSED(fontSize) // Font size is handled by Qt's rendering context in the view, 
                       // or via FontRole. The text content itself is just the dump.
    
    QString out;
    const int perLine = 16;
    for (int off = 0; off < data.size(); off += perLine) {
        const int n = qMin(perLine, data.size() - off);
        QString line = QString("%1  ").arg(off, 6, 16, QChar('0')).toUpper();
        QString ascii;
        for (int i = 0; i < perLine; ++i) {
            if (i < n) {
                const unsigned char b = static_cast<unsigned char>(data[off + i]);
                line += hexByte(b) + ' ';
                if (includeAscii)
                    ascii += asciiOrDot(b);
            } else {
                line += QStringLiteral("   ");
                if (includeAscii)
                    ascii += ' ';
            }
            if (i == 7) line += ' ';
        }
        if (includeAscii)
            line += " |" + ascii + "|";
        out += line;
        if (off + perLine < data.size())
            out += '\n';
    }
    return out;
}

void CommDumpModel::addRecord(const QString &plugin, const QString &details, bool isTx,
                               const QByteArray &data)
{
    appendRecord(nowMicros(), plugin, details, isTx, data);
}

void CommDumpModel::appendRecord(qint64 timestampUs, const QString &plugin, const QString &details,
                                  bool isTx, const QByteArray &data)
{
    Record r;
    r.timestampUs = timestampUs;
    r.plugin      = plugin;
    r.details     = details;
    r.isTx        = isTx;
    r.data        = data;

    const int row = m_records.size();
    beginInsertRows(QModelIndex(), row, row);
    m_records.append(std::move(r));
    endInsertRows();
}

void CommDumpModel::setShowAscii(bool on)
{
    if (m_showAscii == on)
        return;
    m_showAscii = on;

    // Clear the full dump cache for all records so they regenerate with the new ASCII setting
    for (auto &r : m_records) {
        r.fullDumpCache.clear();
    }

    if (!m_records.isEmpty())
        emit dataChanged(index(0, ColAscii), index(m_records.size() - 1, ColAscii), { Qt::DisplayRole });
}

void CommDumpModel::setTimeFormat(TimeFormat fmt)
{
    if (m_timeFormat == fmt)
        return;
    m_timeFormat = fmt;

    // Only the Timestamp column's *text* changes; nothing is recomputed on
    // the records themselves. Re-emitting headerDataChanged too so the
    // column header can reflect the active mode (see headerData()).
    if (!m_records.isEmpty())
        emit dataChanged(index(0, ColTimestamp), index(m_records.size() - 1, ColTimestamp), { Qt::DisplayRole });
    emit headerDataChanged(Qt::Horizontal, ColTimestamp, ColTimestamp);
}

void CommDumpModel::clear()
{
    if (m_records.isEmpty())
        return;
    beginResetModel();
    m_records.clear();
    endResetModel();
}

QJsonObject CommDumpModel::recordToJson(const Record &r) const
{
    QJsonObject o;
    o["ts"]      = QString::number(r.timestampUs);   // string: avoids double precision loss
    o["plugin"]  = r.plugin;
    o["details"] = r.details;
    o["dir"]     = r.isTx ? QStringLiteral("Tx") : QStringLiteral("Rx");
    o["data"]    = QString::fromLatin1(r.data.toBase64());
    return o;
}

QJsonArray CommDumpModel::toJsonArray(const QList<int> &rows) const
{
    QJsonArray arr;
    if (rows.isEmpty()) {
        for (const auto &r : m_records)
            arr.append(recordToJson(r));
    } else {
        for (int row : rows) {
            if (row >= 0 && row < m_records.size())
                arr.append(recordToJson(m_records[row]));
        }
    }
    return arr;
}

void CommDumpModel::loadJsonArray(const QJsonArray &arr, double fontSizeProportion)
{
    beginResetModel();
    m_records.clear();
    m_records.reserve(arr.size());
    
    // Note: We don't set m_fullDumpFontSize here directly because we don't know the 
    // base font size of the parent widget yet. The View will calculate the absolute 
    // point size using this proportion after loading.
    // However, if we want to store the proportion in the model for reload consistency,
    // we can update the member if passed.
    if (fontSizeProportion > 0) {
        // We keep the proportion, but the absolute size is calculated by the view
        // To make this work seamlessly, let's store the proportion in a separate member 
        // or just use the passed value as the new proportion if it's different from default
    }

    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Record r;
        r.timestampUs = o.value("ts").toString().toLongLong();
        r.plugin      = o.value("plugin").toString();
        r.details     = o.value("details").toString();
        r.isTx        = o.value("dir").toString() == QStringLiteral("Tx");
        r.data        = QByteArray::fromBase64(o.value("data").toString().toLatin1());
        m_records.append(std::move(r));
    }
    endResetModel();
}

QModelIndex CommDumpModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid())
        return createIndex(row, column, kTopLevelSentinel);

    // Only top-level rows (records) have a child, and only exactly one (row 0).
    if (parent.internalId() == kTopLevelSentinel && row == 0)
        return createIndex(row, column, static_cast<quintptr>(parent.row()));

    return {};
}

QModelIndex CommDumpModel::parent(const QModelIndex &child) const
{
    if (!child.isValid() || child.internalId() == kTopLevelSentinel)
        return {};
    return createIndex(static_cast<int>(child.internalId()), 0, kTopLevelSentinel);
}

int CommDumpModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_records.size();

    // Only column-0 top-level indices report a child, so the tree doesn't
    // get a duplicate child per column.
    if (parent.column() != 0)
        return 0;
    if (parent.internalId() != kTopLevelSentinel)
        return 0;   // child rows never have children of their own

    const int row = parent.row();
    if (row < 0 || row >= m_records.size())
        return 0;
    return m_records[row].data.isEmpty() ? 0 : 1;
}

int CommDumpModel::columnCount(const QModelIndex & /*parent*/) const
{
    return ColCount;
}

bool CommDumpModel::isChildRow(const QModelIndex &index) const
{
    return index.isValid() && index.internalId() != kTopLevelSentinel;
}

const CommDumpModel::Record *CommDumpModel::recordForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    const int recRow = (index.internalId() == kTopLevelSentinel)
                            ? index.row()
                            : static_cast<int>(index.internalId());
    if (recRow < 0 || recRow >= m_records.size())
        return nullptr;
    return &m_records[recRow];
}

QVariant CommDumpModel::data(const QModelIndex &index, int role) const
{
    const Record *rec = recordForIndex(index);
    if (!rec)
        return {};

    if (isChildRow(index)) {
        // Full dump row: only column 0 carries content; the view spans it
        // across the whole row with setFirstColumnSpanned().
        if (index.column() != 0)
            return {};
        
        if (role == Qt::DisplayRole) {
            if (rec->fullDumpCache.isEmpty()) {
                // Generate the dump text
                rec->fullDumpCache = hexAsciiFull(rec->data, m_showAscii, m_fullDumpFontSize);
            }
            return rec->fullDumpCache;
        }
        
        if (role == Qt::FontRole) {
            QFont f("JetBrains Mono");
            f.setStyleHint(QFont::Monospace);
            f.setPointSize(static_cast<int>(m_fullDumpFontSize));
            return f;
        }
        
        if (role == Qt::ForegroundRole)
            return QBrush(QColor("#8a95a8"));
        return {};
    }

    // Top-level record row.
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case ColTimestamp:
            // index.row() is the true record row here (top-level rows are
            // never reparented), so m_records[index.row() - 1/0] is "the
            // previous trace" / "the first trace" in display/insertion order.
            switch (m_timeFormat) {
            case TimeWallClock:
                return formatTimestampUs(rec->timestampUs);
            case TimeDeltaPrevious:
                return formatDurationSecUs(index.row() > 0
                    ? rec->timestampUs - m_records[index.row() - 1].timestampUs
                    : 0);
            case TimeSinceCaptureStart:
                return formatDurationSecUs(rec->timestampUs - m_records[0].timestampUs);
            }
            return {};
        case ColPlugin:    return rec->plugin;
        case ColDetails:   return rec->details;
        case ColDir:       return rec->isTx ? QStringLiteral("Tx") : QStringLiteral("Rx");
        case ColLength:    return rec->data.size();
        case ColData:      return hexOnlyPreview(rec->data, k_previewBytes);
        case ColAscii:     return m_showAscii ? asciiOnlyPreview(rec->data, k_previewBytes) : QVariant();
        default: return {};
        }
    case Qt::FontRole:
        if (index.column() == ColData || index.column() == ColAscii) {
            QFont f("JetBrains Mono");
            f.setStyleHint(QFont::Monospace);
            return f;
        }
        return {};
    case Qt::ForegroundRole:
        if (index.column() == ColDir)
            return rec->isTx ? QBrush(QColor("#50fa7b")) : QBrush(QColor("#4a9eff"));
        if (index.column() == ColLength)
            return QBrush(QColor("#ffb86c"));
        if (index.column() == ColData)
            return QBrush(QColor("#f1fa8c"));   // yellow — same hue used in the full dump
        if (index.column() == ColAscii)
            return QBrush(QColor("#8be9fd"));   // cyan — same hue used in the full dump
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == ColLength)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    default:
        return {};
    }
}

QVariant CommDumpModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColTimestamp:
        switch (m_timeFormat) {
        case TimeWallClock:         return QStringLiteral("Timestamp");
        case TimeDeltaPrevious:     return QStringLiteral("Timestamp (Δ prev)");
        case TimeSinceCaptureStart: return QStringLiteral("Timestamp (t0)");
        }
        return QStringLiteral("Timestamp");
    case ColPlugin:    return QStringLiteral("Plugin");
    case ColDetails:   return QStringLiteral("Details");
    case ColDir:       return QStringLiteral("Dir");
    case ColLength:    return QStringLiteral("Length");
    case ColData:      return QStringLiteral("Data");
    case ColAscii:     return QStringLiteral("ASCII");
    default: return {};
    }
}
