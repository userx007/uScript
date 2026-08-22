#include "CommDumpModel.hpp"
#include <QBrush>
#include <QFont>
#include <QColor>
#include <QDateTime>
#include <chrono>
#include <algorithm>
#include <numeric>

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

    // Both storages' cached dump text depend on font size — clear both
    // regardless of m_collapsedMode, so a mode toggle afterwards doesn't
    // resurrect a stale cache built at the old size. Only the CURRENTLY
    // ACTIVE storage's rows are notified via dataChanged: those are the
    // only rows a QAbstractItemModel contract allows a view to assume
    // currently exist.
    for (auto &r : m_records)       r.fullDumpCache.clear();
    for (auto &r : m_aggregateRows) r.fullDumpCache.clear();

    const int n = recordCount();
    for (int i = 0; i < n; ++i) {
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
//
//  Every record is ALWAYS appended to the raw log AND folded into the
//  aggregate index, regardless of collapsedMode() — see setCollapsedMode()
//  for why both are always kept current. Model-change signals for whichever
//  side isn't currently displayed are simply skipped (not "wrong", just
//  not emitted), since a QAbstractItemModel must never emit a signal about
//  rows a view isn't allowed to assume exist right now.
//
//  Returns an IngestResult telling the caller exactly which currently-
//  active-storage rows this call touched, and how many of those are
//  brand-new tail rows vs. existing rows updated in place — see the
//  IngestResult comment in the header for why that distinction matters
//  (in short: in collapsed mode a batch of repeats touches a handful of
//  existing rows and appends nothing, and treating that as "N new rows at
//  the tail" is what used to make the view auto-scroll away from — and
//  reprocess an arbitrary slice around — rows it had no business touching).
// ─────────────────────────────────────────────────────────────────────────────
CommDumpModel::IngestResult CommDumpModel::addRecords(const QVector<PendingRecord> &pending)
{
    IngestResult result;
    if (pending.isEmpty())
        return result;

    const int rawFirst = m_records.size();
    const int rawLast   = rawFirst + pending.size() - 1;

    if (!m_collapsedMode)
        beginInsertRows(QModelIndex(), rawFirst, rawLast);
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
    if (!m_collapsedMode)
        endInsertRows();

    const int aggregateCountBefore = m_aggregateRows.size();

    // Only bother tracking per-record touched rows while collapsed mode is
    // what's actually displayed — in raw mode the answer is always "the
    // whole contiguous tail block", computed below without needing this.
    QVector<int> touchedAggregateRows;
    if (m_collapsedMode)
        touchedAggregateRows.reserve(pending.size());
    for (const PendingRecord &p : pending) {
        const int row = updateAggregateForRecord(p.timestampUs, p.plugin, p.details, p.isTx, p.data);
        if (m_collapsedMode)
            touchedAggregateRows.append(row);
    }

    m_totalIngested += pending.size();

    evictIfNeeded();
    evictAggregateIfNeeded();

    if (m_collapsedMode) {
        // A batch dominated by repeats of the same handful of keys
        // otherwise leaves many duplicate entries pointing at the same
        // row — sort + unique collapses that down to the actual distinct
        // set of rows touched.
        std::sort(touchedAggregateRows.begin(), touchedAggregateRows.end());
        touchedAggregateRows.erase(std::unique(touchedAggregateRows.begin(), touchedAggregateRows.end()),
                                    touchedAggregateRows.end());

        // Defensive: evictAggregateIfNeeded() above may (rarely — see its
        // own comment; this is essentially never expected in practice)
        // have shifted or dropped rows since these indices were captured.
        // Drop anything now out of range rather than hand the view a
        // stale index.
        const int n = m_aggregateRows.size();
        touchedAggregateRows.erase(
            std::remove_if(touchedAggregateRows.begin(), touchedAggregateRows.end(),
                            [n](int row) { return row < 0 || row >= n; }),
            touchedAggregateRows.end());

        result.touchedRows  = std::move(touchedAggregateRows);
        result.rowsAppended = m_aggregateRows.size() - aggregateCountBefore;
    } else {
        // Raw mode: every record is a brand-new row, always appended at
        // the tail. evictIfNeeded() may have trimmed the front, so the
        // newly-added block is always the *last* min(pending.size(),
        // recordCount()) rows — the same adjustment CommDumpView used to
        // apply itself before this became the single source of truth for
        // both modes.
        const int countAfter = m_records.size();
        const int first = qMax(0, countAfter - qMin(pending.size(), countAfter));
        const int last  = countAfter - 1;
        result.touchedRows.resize(last - first + 1);
        std::iota(result.touchedRows.begin(), result.touchedRows.end(), first);
        result.rowsAppended = result.touchedRows.size();
    }

    return result;
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

    // Raw-storage mutation always happens (the raw log must stay bounded
    // regardless of view mode); the begin/endRemoveRows signal pair is only
    // emitted while raw rows are what's currently displayed — see
    // addRecords().
    if (!m_collapsedMode)
        beginRemoveRows(QModelIndex(), 0, keepFrom - 1);
    m_records.remove(0, keepFrom);
    if (!m_collapsedMode)
        endRemoveRows();
}

QString CommDumpModel::aggregateKey(const QString &plugin, const QString &details)
{
    return plugin + QChar(0x1F) + details;   // unit separator — see header comment
}

int CommDumpModel::updateAggregateForRecord(qint64 timestampUs, const QString &plugin, const QString &details,
                                             bool isTx, const QByteArray &data)
{
    const QString key = aggregateKey(plugin, details);
    const auto it = m_aggregateKeyToRow.constFind(key);

    if (it != m_aggregateKeyToRow.constEnd()) {
        const int row = it.value();
        AggregateEntry &e = m_aggregateRows[row];
        e.previousTimestampUs = e.timestampUs;   // shift before overwriting — see TimeDeltaPrevious in data()
        e.timestampUs = timestampUs;
        e.isTx        = isTx;
        e.data        = data;
        e.fullDumpCache.clear();   // stale — the latest payload just changed
        e.count      += 1;

        if (m_collapsedMode) {
            const QModelIndex topLeft     = index(row, 0);
            const QModelIndex bottomRight = index(row, ColCount - 1);
            emit dataChanged(topLeft, bottomRight);
        }
        return row;
    }

    AggregateEntry e;
    e.timestampUs           = timestampUs;
    e.plugin                = plugin;
    e.details                = details;
    e.isTx                   = isTx;
    e.data                   = data;
    e.count                  = 1;
    e.firstSeenTimestampUs   = timestampUs;
    e.previousTimestampUs    = timestampUs;

    const int newRow = m_aggregateRows.size();
    if (m_collapsedMode)
        beginInsertRows(QModelIndex(), newRow, newRow);
    m_aggregateRows.append(std::move(e));
    m_aggregateKeyToRow.insert(key, newRow);
    if (m_collapsedMode)
        endInsertRows();

    m_totalDistinctKeysSeen += 1;
    return newRow;
}

void CommDumpModel::evictAggregateIfNeeded()
{
    if (m_maxAggregateRows <= 0 || m_aggregateRows.size() <= m_maxAggregateRows)
        return;

    constexpr double kEvictBackToRatio = 0.9;
    const int keepFrom = m_aggregateRows.size() - static_cast<int>(m_maxAggregateRows * kEvictBackToRatio);
    if (keepFrom <= 0)
        return;

    if (m_collapsedMode)
        beginRemoveRows(QModelIndex(), 0, keepFrom - 1);

    for (int i = 0; i < keepFrom; ++i)
        m_aggregateKeyToRow.remove(aggregateKey(m_aggregateRows[i].plugin, m_aggregateRows[i].details));
    m_aggregateRows.remove(0, keepFrom);
    // Every remaining row shifted down by `keepFrom` — re-point the hash.
    for (auto it = m_aggregateKeyToRow.begin(); it != m_aggregateKeyToRow.end(); ++it)
        it.value() -= keepFrom;

    if (m_collapsedMode)
        endRemoveRows();
}

void CommDumpModel::rebuildAggregateFromRecords()
{
    // Always called from inside a begin/endResetModel() pair (see
    // setCollapsedMode()/loadJsonArray()) — no signals needed here.
    m_aggregateRows.clear();
    m_aggregateKeyToRow.clear();
    m_totalDistinctKeysSeen = 0;

    for (const Record &r : m_records) {
        const QString key = aggregateKey(r.plugin, r.details);
        const auto it = m_aggregateKeyToRow.constFind(key);
        if (it != m_aggregateKeyToRow.constEnd()) {
            AggregateEntry &e = m_aggregateRows[it.value()];
            e.previousTimestampUs = e.timestampUs;   // shift before overwriting — same as updateAggregateForRecord()
            e.timestampUs = r.timestampUs;
            e.isTx        = r.isTx;
            e.data        = r.data;
            e.fullDumpCache.clear();
            e.count      += 1;
            continue;
        }
        AggregateEntry e;
        e.timestampUs         = r.timestampUs;
        e.plugin               = r.plugin;
        e.details               = r.details;
        e.isTx                  = r.isTx;
        e.data                  = r.data;
        e.count                 = 1;
        e.firstSeenTimestampUs  = r.timestampUs;
        e.previousTimestampUs   = r.timestampUs;
        m_aggregateKeyToRow.insert(key, m_aggregateRows.size());
        m_aggregateRows.append(std::move(e));
        m_totalDistinctKeysSeen += 1;
    }

    // A pathologically wide raw log could produce more distinct keys than
    // m_maxAggregateRows in one rebuild — apply the same defensive cap here
    // too (signals suppressed either way since we're inside a model reset).
    evictAggregateIfNeeded();
}

void CommDumpModel::setCollapsedMode(bool on)
{
    if (m_collapsedMode == on)
        return;
    beginResetModel();
    m_collapsedMode = on;
    endResetModel();
}

const CommDumpModel::Record *CommDumpModel::rawRecordAt(int row) const
{
    if (row < 0 || row >= m_records.size())
        return nullptr;
    return &m_records[row];
}

void CommDumpModel::setShowAscii(bool on)
{
    if (m_showAscii == on)
        return;
    m_showAscii = on;

    // Clear the full dump cache for both storages (see setFullDumpFontSize
    // for why both, regardless of m_collapsedMode).
    for (auto &r : m_records)       r.fullDumpCache.clear();
    for (auto &r : m_aggregateRows) r.fullDumpCache.clear();

    const int n = recordCount();
    if (n > 0)
        emit dataChanged(index(0, ColAscii), index(n - 1, ColAscii), { Qt::DisplayRole });
}

void CommDumpModel::setTimeFormat(TimeFormat fmt)
{
    if (m_timeFormat == fmt)
        return;
    m_timeFormat = fmt;

    // Only the Timestamp column's *text* changes; nothing is recomputed on
    // the records themselves. Re-emitting headerDataChanged too so the
    // column header can reflect the active mode (see headerData()).
    const int n = recordCount();
    if (n > 0)
        emit dataChanged(index(0, ColTimestamp), index(n - 1, ColTimestamp), { Qt::DisplayRole });
    emit headerDataChanged(Qt::Horizontal, ColTimestamp, ColTimestamp);
}

void CommDumpModel::setDumpBytesPerLine(int n)
{
    if (n != 8 && n != 16 && n != 32)
        return;   // only 8/16/32 are valid — silently ignore anything else
    if (m_dumpBytesPerLine == n)
        return;
    m_dumpBytesPerLine = n;

    // Clear both storages' caches (see setFullDumpFontSize for why both),
    // then notify only whichever storage's child rows are currently
    // materialized/active.
    for (auto &r : m_records)       r.fullDumpCache.clear();
    for (auto &r : m_aggregateRows) r.fullDumpCache.clear();

    const int rows = recordCount();
    for (int i = 0; i < rows; ++i) {
        const QModelIndex parentIdx = index(i, 0);
        const QModelIndex childIdx = index(0, 0, parentIdx);
        if (childIdx.isValid())
            emit dataChanged(childIdx, childIdx, { Qt::DisplayRole });
    }
    emit headerDataChanged(Qt::Horizontal, ColData, ColData);
}

void CommDumpModel::clear()
{
    // m_totalIngested must be rebased to 0 here, not just m_records emptied
    // — this is the "start a fresh session" checkpoint, called both from
    // the CLEAR button and (via CommDumpView::clear()) at the start of
    // every script RUN. Without this reset, totalIngestedCount() would
    // keep accumulating across every run for the lifetime of the app,
    // making the view's "N records (of M captured, oldest trimmed)" label
    // report an M with no relationship to the current session — e.g. still
    // showing millions "captured" moments after a fresh run that has only
    // produced a few hundred records so far.
    m_totalIngested = 0;
    m_totalDistinctKeysSeen = 0;

    if (m_records.isEmpty() && m_aggregateRows.isEmpty())
        return;
    beginResetModel();
    m_records.clear();
    m_aggregateRows.clear();
    m_aggregateKeyToRow.clear();
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

    // Rebuilds the aggregate index from the freshly loaded raw log —
    // otherwise a collapsed view (whether already on, or turned on right
    // after this load) would keep showing whatever it had from before the
    // load instead of this file's own data. Cheap: a rebuild is only ever
    // this file's size, done once per load, not per record.
    rebuildAggregateFromRecords();

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
    const int topCount = recordCount();   // mode-aware — see recordCount()

    if (!parent.isValid())
        return topCount;

    // Only column-0 top-level indices report a child, so the tree doesn't
    // get a duplicate child per column.
    if (parent.column() != 0)
        return 0;
    if (parent.internalId() != kTopLevelSentinel)
        return 0;   // child rows never have children of their own

    const int row = parent.row();
    if (row < 0 || row >= topCount)
        return 0;
    const QByteArray &payload = m_collapsedMode ? m_aggregateRows[row].data : m_records[row].data;
    return payload.isEmpty() ? 0 : 1;
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
    // Mode-aware: `row` is always a CURRENTLY ACTIVE display-row index (raw
    // or aggregate — matches what recordCount()/index() mean right now),
    // since callers (CommDumpView's copy-to-clipboard) get `row` from
    // selection/iteration over the currently displayed tree.
    if (m_collapsedMode) {
        if (row < 0 || row >= m_aggregateRows.size())
            return {};
        const AggregateEntry &e = m_aggregateRows[row];
        if (e.data.isEmpty())
            return {};
        if (e.fullDumpCache.isEmpty())
            e.fullDumpCache = hexAsciiFull(e.data, m_showAscii, m_fullDumpFontSize, m_dumpBytesPerLine);
        return e.fullDumpCache;
    }

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
    if (m_collapsedMode) {
        if (recRow < 0 || recRow >= m_aggregateRows.size())
            return nullptr;
        return &m_aggregateRows[recRow];   // AggregateEntry* implicitly upcasts to Record*
    }
    if (recRow < 0 || recRow >= m_records.size())
        return nullptr;
    return &m_records[recRow];
}

// Timestamp of the row at active-storage index `row` in RAW mode's own
// sequence — used only by ColTimestamp's TimeDeltaPrevious rendering while
// raw (non-collapsed) is what's displayed, where "the previous row" is
// unambiguous because raw records are strictly append-only in time order.
// Collapsed mode does NOT use this (see data()): aggregate row order is
// first-seen order, not time order, so comparing against "the row before
// this one in the table" doesn't hold the same meaning there. Caller
// guarantees 0 <= row < recordCount() and !collapsedMode().
qint64 CommDumpModel::timestampAtActiveRow(int row) const
{
    return m_records[row].timestampUs;
}

// The reference point for TimeSinceCaptureStart: the RAW log's own first
// (chronologically earliest currently-retained — maxRecords() eviction
// trims from the front, same as raw mode always has) record, regardless of
// which display mode is active. Deliberately NOT "whichever row sits at
// aggregate index 0" — that's just whichever (plugin,details) key happened
// to be seen first, and it can repeat like any other key, which used to
// jump its own timestamp forward and make every OTHER row's "time since
// start" collapse toward (and clamp at) 0 each time it did.
qint64 CommDumpModel::captureStartTimestampUs() const
{
    return m_records.isEmpty() ? 0 : m_records.first().timestampUs;
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
            switch (m_timeFormat) {
            case TimeWallClock:
                return formatTimestampUs(rec->timestampUs);
            case TimeDeltaPrevious:
                if (m_collapsedMode) {
                    // See AggregateEntry::previousTimestampUs — delta since
                    // THIS key's own previous occurrence, not the aggregate
                    // row before it in (first-seen-ordered) table position.
                    const auto *agg = static_cast<const AggregateEntry *>(rec);
                    return formatDurationSecUs(rec->timestampUs - agg->previousTimestampUs);
                }
                return formatDurationSecUs(index.row() > 0
                    ? rec->timestampUs - timestampAtActiveRow(index.row() - 1)
                    : 0);
            case TimeSinceCaptureStart:
                return formatDurationSecUs(rec->timestampUs - captureStartTimestampUs());
            }
            return {};
        case ColPlugin:    return rec->plugin;
        case ColDetails:   return rec->details;
        case ColDir:       return rec->isTx ? QStringLiteral("Tx") : QStringLiteral("Rx");
        case ColLength:    return rec->data.size();
        case ColData:      return hexOnlyPreview(rec->data, m_dumpBytesPerLine);
        case ColAscii:     return m_showAscii ? asciiOnlyPreview(rec->data, m_dumpBytesPerLine) : QVariant();
        case ColRepeatCount:
            // Only meaningful in collapsed view — the column is hidden by
            // the view otherwise (see CommDumpView), but return {} rather
            // than a misleading "1" if something queries it regardless.
            if (!m_collapsedMode)
                return {};
            return static_cast<const AggregateEntry *>(rec)->count;
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
        if (index.column() == ColRepeatCount) {
            static const QBrush kCountFg{QColor("#6272a4")};  // muted grey-blue — a background stat, not primary data
            return kCountFg;
        }
        return {};
    case Qt::TextAlignmentRole:
        if (index.column() == ColLength || index.column() == ColRepeatCount)
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
        case ColRepeatCount:
            return QStringLiteral("Number of times this Plugin + Details combination has occurred");
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
    case ColRepeatCount: return QStringLiteral("Count");
    default: return {};
    }
}
