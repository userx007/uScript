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

// Appended to a column header's label so double-click-to-cycle columns are
// visually distinguishable from ordinary ones without needing per-section
// QSS (QHeaderView styles all sections uniformly; singling one out would
// need a custom-painted header). A small suffix glyph + a matching
// Qt::ToolTipRole (see headerData()) is simpler and just as discoverable.
const QString kDoubleClickMarker = QStringLiteral(" ⟲");

// Palette for per-plugin colouring of ColPlugin. Chosen to sit alongside the
// other fixed accent colours already used in this view (Dracula-ish: the
// Tx/Rx green/blue, the Length orange, the Data yellow, the Ascii cyan) —
// bright enough to read on the dark tree background without repeating any
// of those exact hues, so the plugin name doesn't get visually confused with
// direction/length/data colouring in the same row. Cycles if there are more
// distinct plugins than colours.
const QVector<QColor> &pluginColorPalette()
{
    static const QVector<QColor> kPalette = {
        QColor("#ff79c6"),   // pink
        QColor("#bd93f9"),   // purple
        QColor("#ffcb6b"),   // amber
        QColor("#69f0ae"),   // mint
        QColor("#82aaff"),   // periwinkle
        QColor("#ff8b94"),   // coral
        QColor("#c3e88d"),   // lime
        QColor("#f78c6c"),   // salmon orange
        QColor("#89ddff"),   // pale cyan
        QColor("#d0a3ff"),   // lavender
    };
    return kPalette;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  colorForPlugin — deterministic pick from pluginColorPalette() via
//  qHash(plugin), cached in m_pluginColors to avoid re-hashing on every
//  paint. Deterministic-by-hash (rather than by first-seen order) means a
//  given plugin name always gets the same colour across every session and
//  every reloaded trace, independent of row/paint order. Two plugin names
//  can in principle collide on the same colour once there are more distinct
//  plugins than palette entries; that's an acceptable, rare trade-off for
//  session-independent stability.
// ─────────────────────────────────────────────────────────────────────────────
QColor CommDumpModel::colorForPlugin(const QString &plugin) const
{
    auto it = m_pluginColors.constFind(plugin);
    if (it != m_pluginColors.constEnd())
        return it.value();

    const QVector<QColor> &palette = pluginColorPalette();
    const QColor color = palette[static_cast<uint>(qHash(plugin)) % static_cast<uint>(palette.size())];
    m_pluginColors.insert(plugin, color);
    return color;
}

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

    // Clear each record's cache and, if its child row is currently
    // materialized, notify the view — done in one pass rather than two
    // separate loops over m_records.
    for (int i = 0; i < m_records.size(); ++i) {
        m_records[i].fullDumpCache.clear();

        const QModelIndex parentIdx = index(i, 0); // Top level row
        const QModelIndex childIdx = index(0, 0, parentIdx); // Child row
        if (childIdx.isValid())
            emit dataChanged(childIdx, childIdx, { Qt::DisplayRole, Qt::FontRole });
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
//
//  deltaUs is clamped to 0 rather than formatted as-is: timestamps come from
//  the producer's system wall clock (std::chrono::system_clock — see
//  commdump_now_us() in ICommDumpProtocol.hpp), which isn't guaranteed
//  monotonic — an NTP resync or manual clock change between two records can
//  make it go
//  backwards, and naively formatting a negative delta here produces garbled
//  output (e.g. "-1.-500000", since seconds and the microsecond fraction are
//  computed with two separate truncating operations). Reporting "0" for that
//  rare case is preferable to a nonsensical string.
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::formatDurationSecUs(qint64 deltaUs)
{
    if (deltaUs < 0)
        deltaUs = 0;
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
//  hexAsciiFull — classic N-bytes-per-line hex+ASCII dump, monospace-ready.
//  bytesPerLine is caller-supplied (8 or 16, see setDumpBytesPerLine()); the
//  extra mid-line gap scales with it too (after byte bytesPerLine/2, so it
//  still visually splits the line in half for either width).
//  If includeAscii is false, it returns only the hex part without the "|...|" suffix.
//
//  Plain text, not HTML: the child row's Qt::ForegroundRole (see data()) is
//  a single flat colour for the whole block, so hex and ASCII are not
//  colour-differentiated the way the top-level row's Data/ASCII columns are.
//  If per-region colouring inside the expanded dump is wanted, it needs to
//  be added here (e.g. returning an HTML fragment) *and* the corresponding
//  ForegroundRole branch below would need to stop overriding it with a
//  single colour, since Qt::ForegroundRole always wins over inline
//  HTML/span colours when both are present.
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::hexAsciiFull(const QByteArray &data, bool includeAscii, double fontSize, int bytesPerLine)
{
    Q_UNUSED(fontSize) // Font size is handled by Qt's rendering context in the view, 
                       // or via FontRole. The text content itself is just the dump.
    
    QString out;
    const int perLine = bytesPerLine;
    const int midGap  = perLine / 2 - 1;   // e.g. 7 for 16 bytes/line, 3 for 8 bytes/line
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
            if (i == midGap) line += ' ';
        }
        if (includeAscii)
            line += " |" + ascii + "|";
        out += line;
        if (off + perLine < data.size())
            out += '\n';
    }
    return out;
}

void CommDumpModel::addRecord(qint64 timestampUs, const QString &plugin, const QString &details, bool isTx,
                               const QByteArray &data)
{
    appendRecord(timestampUs, plugin, details, isTx, data);
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

void CommDumpModel::setDumpBytesPerLine(int n)
{
    if (m_dumpBytesPerLine == n)
        return;
    m_dumpBytesPerLine = n;

    // Only the expanded child rows are affected; clear each cache and, if
    // materialized, notify the view — same pattern as setFullDumpFontSize().
    for (int i = 0; i < m_records.size(); ++i) {
        m_records[i].fullDumpCache.clear();

        const QModelIndex parentIdx = index(i, 0);
        const QModelIndex childIdx = index(0, 0, parentIdx);
        if (childIdx.isValid())
            emit dataChanged(childIdx, childIdx, { Qt::DisplayRole });
    }
    emit headerDataChanged(Qt::Horizontal, ColData, ColData);
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

void CommDumpModel::loadJsonArray(const QJsonArray &arr)
{
    beginResetModel();
    m_records.clear();
    m_records.reserve(arr.size());

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

QString CommDumpModel::fullDumpForRow(int row) const
{
    if (row < 0 || row >= m_records.size())
        return {};

    const Record &rec = m_records[row];
    if (rec.data.isEmpty())
        return {};

    // Same lazily-built cache the expanded child row's data() uses — a row
    // that's already been expanded on screen doesn't pay to reformat here.
    if (rec.fullDumpCache.isEmpty())
        rec.fullDumpCache = hexAsciiFull(rec.data, m_showAscii, m_fullDumpFontSize, m_dumpBytesPerLine);
    return rec.fullDumpCache;
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
                rec->fullDumpCache = hexAsciiFull(rec->data, m_showAscii, m_fullDumpFontSize, m_dumpBytesPerLine);
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
        case ColData:      return hexOnlyPreview(rec->data, m_dumpBytesPerLine);
        case ColAscii:     return m_showAscii ? asciiOnlyPreview(rec->data, m_dumpBytesPerLine) : QVariant();
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
        if (index.column() == ColPlugin)
            return QBrush(colorForPlugin(rec->plugin));
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
    if (orientation != Qt::Horizontal)
        return {};

    if (role == Qt::ToolTipRole) {
        switch (section) {
        case ColTimestamp:
            return QStringLiteral("Double-click to cycle: wall-clock time → Δ since previous → since capture start");
        case ColData:
            return QStringLiteral("Double-click to switch the full-dump view between 8 and 16 bytes per line");
        default:
            return {};
        }
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColTimestamp: {
        QString label;
        switch (m_timeFormat) {
        case TimeWallClock:         label = QStringLiteral("Timestamp");             break;
        case TimeDeltaPrevious:     label = QStringLiteral("Timestamp (Δ prev)");    break;
        case TimeSinceCaptureStart: label = QStringLiteral("Timestamp (t0)");        break;
        default:                   label = QStringLiteral("Timestamp");             break;
        }
        return label + kDoubleClickMarker;
    }
    case ColPlugin:    return QStringLiteral("Plugin");
    case ColDetails:   return QStringLiteral("Details");
    case ColDir:       return QStringLiteral("Dir");
    case ColLength:    return QStringLiteral("Length");
    case ColData:      return QStringLiteral("Data:%1").arg(m_dumpBytesPerLine) + kDoubleClickMarker;
    case ColAscii:     return QStringLiteral("ASCII");
    default: return {};
    }
}
