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
    // Matches the default m_fullDumpFontSize (10.0) — see the
    // m_fullDumpFont member comment. Normally overwritten immediately by
    // CommDumpView's own updateFullDumpFontSize() call, but built here too
    // so the model is self-consistent even without a view attached.
    m_fullDumpFont = QFont("JetBrains Mono");
    m_fullDumpFont.setStyleHint(QFont::Monospace);
    m_fullDumpFont.setPointSize(static_cast<int>(m_fullDumpFontSize));
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

    // Rebuilt here, once, rather than in data() on every FontRole query —
    // see the m_fullDumpFont member comment.
    m_fullDumpFont = QFont("JetBrains Mono");
    m_fullDumpFont.setStyleHint(QFont::Monospace);
    m_fullDumpFont.setPointSize(static_cast<int>(m_fullDumpFontSize));

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
//  bytesPerLine is caller-supplied (8, 16, or 32, see setDumpBytesPerLine());
//  the extra mid-line gap scales with it too (after byte bytesPerLine/2, so
//  it still visually splits the line in half for any of the three widths).
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
    QVector<PendingRecord> one(1);
    one[0] = { timestampUs, plugin, details, isTx, data };
    addRecords(one);
}

// ─────────────────────────────────────────────────────────────────────────────
//  addRecords — batched insert. A single beginInsertRows()/endInsertRows()
//  pair covers the whole `pending` list, so the view (and everything
//  connected to rowsInserted — selection model, header, etc.) only relayouts
//  once for the batch instead of once per record. This is what lets the
//  producer side (see CommDumpView's coalesced ingestion queue) push a burst
//  of hundreds of records through without a proportional number of GUI
//  transactions. Eviction (if maxRecords() is set) is applied once, after
//  the whole batch has landed — see evictIfNeeded().
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpModel::addRecords(const QVector<PendingRecord> &pending)
{
    if (pending.isEmpty())
        return;

    const int first = m_records.size();
    const int last   = first + pending.size() - 1;

    beginInsertRows(QModelIndex(), first, last);
    m_records.reserve(m_records.size() + pending.size());
    for (const PendingRecord &p : pending) {
        Record r;
        r.timestampUs = p.timestampUs;
        r.plugin      = p.plugin;
        r.details     = p.details;
        r.isTx        = p.isTx;
        r.data        = p.data;
        m_records.append(std::move(r));
    }
    endInsertRows();

    m_totalIngested += pending.size();

    evictIfNeeded();
}

// ─────────────────────────────────────────────────────────────────────────────
//  setMaxRecords / evictIfNeeded — bounded (ring-buffer) retention.
//
//  Without a cap, a long-running or high-throughput capture grows
//  m_records (and every QByteArray payload + lazily-cached dump string in
//  it) without bound, which is exactly the "stability over a very large
//  amount of trace acquisition" failure mode: eventually either the process
//  runs out of memory, or the tree view itself becomes sluggish simply from
//  the sheer row count.
//
//  Eviction uses hysteresis rather than trimming to exactly maxRecords()
//  every time: once size() exceeds m_maxRecords, it's trimmed back down to
//  ~90% of it (kEvictBackToRatio) in one beginRemoveRows()/endRemoveRows()
//  batch. QVector's front-removal is an O(n) shift of the remaining
//  elements, so doing that shift on *every single insert* once near the cap
//  would turn every addRecords() call into an O(n) operation. Trimming in
//  large batches instead means the O(n) shift happens roughly once every
//  (10% of maxRecords) new records, which amortizes to a small, bounded
//  extra cost per insert rather than a per-insert O(n) cost.
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpModel::setMaxRecords(int max)
{
    if (max < 0)
        max = 0;
    if (m_maxRecords == max)
        return;
    m_maxRecords = max;
    evictIfNeeded();
}

void CommDumpModel::evictIfNeeded()
{
    if (m_maxRecords <= 0 || m_records.size() <= m_maxRecords)
        return;

    constexpr double kEvictBackToRatio = 0.9;
    const int keepFrom = m_records.size() - static_cast<int>(m_maxRecords * kEvictBackToRatio);
    if (keepFrom <= 0)
        return;

    beginRemoveRows(QModelIndex(), 0, keepFrom - 1);
    m_records.remove(0, keepFrom);
    endRemoveRows();
}

void CommDumpModel::setShowAscii(bool on)
{
    if (m_showAscii == on)
        return;
    m_showAscii = on;

    // Clear the full dump cache and the collapsed-row ascii preview cache for
    // all records so they regenerate with the new ASCII setting. (Hex preview
    // doesn't depend on m_showAscii, so hexPreviewCache is left alone.)
    for (auto &r : m_records) {
        r.fullDumpCache.clear();
        r.asciiPreviewCache.clear();
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
    if (n != 8 && n != 16 && n != 32)
        return;   // only 8/16/32 are valid — silently ignore anything else
    if (m_dumpBytesPerLine == n)
        return;
    m_dumpBytesPerLine = n;

    // Both the expanded child row's full dump AND the collapsed top-level
    // row's Data/ASCII previews depend on bytesPerLine — clear all three
    // caches, and notify the child row (if materialized) same as before.
    for (int i = 0; i < m_records.size(); ++i) {
        m_records[i].fullDumpCache.clear();
        m_records[i].hexPreviewCache.clear();
        m_records[i].asciiPreviewCache.clear();

        const QModelIndex parentIdx = index(i, 0);
        const QModelIndex childIdx = index(0, 0, parentIdx);
        if (childIdx.isValid())
            emit dataChanged(childIdx, childIdx, { Qt::DisplayRole });
    }
    // Top-level previews: one batched dataChanged over the whole column
    // range instead of a second per-row loop/emit — cheaper, and now
    // required for correctness now that these previews are cached (before
    // this change they were rebuilt unconditionally on every paint, so a
    // plain repaint request was enough; now the view needs to be told the
    // data actually changed).
    if (!m_records.isEmpty())
        emit dataChanged(index(0, ColData), index(m_records.size() - 1, ColAscii), { Qt::DisplayRole });
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

    // A LOAD is a deliberate, one-shot "open this exact file" action rather
    // than live capture, so it deliberately does NOT go through
    // evictIfNeeded() — a saved trace is shown in full even if it's larger
    // than maxRecords(), which only bounds ongoing addRecords() ingestion.
    // totalIngestedCount() restarts from this file's size, since it's the
    // start of a new viewing session, not a continuation of a live one.
    m_totalIngested = m_records.size();
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
        
        if (role == Qt::FontRole)
            return m_fullDumpFont;   // cached — see setFullDumpFontSize()

        if (role == Qt::ForegroundRole) {
            static const QBrush kFullDumpFg{QColor("#8a95a8")};
            return kFullDumpFg;
        }
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
                // Pure function of timestampUs, which never changes for a
                // given record (append-only, immutable once ingested) — safe
                // to cache with no invalidation path required. The two delta
                // modes below are deliberately NOT cached: they read another
                // record's timestampUs (previous / first), and while that's
                // stable across eviction for TimeDeltaPrevious, the "first"
                // record for TimeSinceCaptureStart changes on eviction, so
                // caching it would need extra invalidation for a case that's
                // rare (mode not default) and cheap to recompute anyway.
                if (rec->wallClockCache.isEmpty())
                    rec->wallClockCache = formatTimestampUs(rec->timestampUs);
                return rec->wallClockCache;
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
        case ColData:
            // Same lazily-built-once pattern as fullDumpCache. Empty is used
            // as the "not yet built" sentinel; a record with no payload
            // legitimately produces an empty string too, so it just recomputes
            // (cheaply — hexOnlyPreview on empty data is a no-op) instead of
            // caching a distinguishable "empty" state. Not worth the extra
            // bool just to skip that.
            if (rec->hexPreviewCache.isEmpty())
                rec->hexPreviewCache = hexOnlyPreview(rec->data, m_dumpBytesPerLine);
            return rec->hexPreviewCache;
        case ColAscii:
            if (!m_showAscii)
                return QVariant();
            if (rec->asciiPreviewCache.isEmpty())
                rec->asciiPreviewCache = asciiOnlyPreview(rec->data, m_dumpBytesPerLine);
            return rec->asciiPreviewCache;
        default: return {};
        }
    case Qt::FontRole:
        if (index.column() == ColData || index.column() == ColAscii) {
            // Fixed (non-dynamic) monospace font shared by every hex/ASCII
            // preview cell — built once ever rather than on every single
            // cell paint, which matters once a trace runs into the tens or
            // hundreds of thousands of rows and the view is scrolled a lot.
            static const QFont kPreviewFont = [] {
                QFont f("JetBrains Mono");
                f.setStyleHint(QFont::Monospace);
                return f;
            }();
            return kPreviewFont;
        }
        return {};
    case Qt::ForegroundRole:
        if (index.column() == ColPlugin)
            return QBrush(colorForPlugin(rec->plugin));
        if (index.column() == ColDir) {
            static const QBrush kTxFg{QColor("#50fa7b")};
            static const QBrush kRxFg{QColor("#4a9eff")};
            return rec->isTx ? kTxFg : kRxFg;
        }
        if (index.column() == ColLength) {
            static const QBrush kLengthFg{QColor("#ffb86c")};
            return kLengthFg;
        }
        if (index.column() == ColData) {
            static const QBrush kDataFg{QColor("#f1fa8c")};   // yellow — same hue used in the full dump
            return kDataFg;
        }
        if (index.column() == ColAscii) {
            static const QBrush kAsciiFg{QColor("#8be9fd")};  // cyan — same hue used in the full dump
            return kAsciiFg;
        }
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
            return QStringLiteral("Double-click to cycle the full-dump view: 8 → 16 → 32 bytes per line");
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
