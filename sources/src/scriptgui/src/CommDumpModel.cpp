#include "CommDumpModel.hpp"
#include <QBrush>
#include <QFont>
#include <QColor>

namespace {
constexpr quintptr kTopLevelSentinel = static_cast<quintptr>(-1);

QString hexByte(unsigned char b) { return QString("%1").arg(b, 2, 16, QChar('0')).toUpper(); }

char asciiOrDot(unsigned char b) { return (b >= 0x20 && b < 0x7F) ? char(b) : '.'; }
} // namespace

CommDumpModel::CommDumpModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

// ─────────────────────────────────────────────────────────────────────────────
//  hexAsciiPreview — "DE AD BE EF 01 02 03 04 …" (first maxBytes only)
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::hexAsciiPreview(const QByteArray &data, int maxBytes)
{
    const int n = qMin(data.size(), maxBytes);
    QString hex;
    QString ascii;
    for (int i = 0; i < n; ++i) {
        const unsigned char b = static_cast<unsigned char>(data[i]);
        hex += hexByte(b);
        if (i + 1 < n) hex += ' ';
        ascii += asciiOrDot(b);
    }
    QString out = hex;
    if (data.size() > maxBytes)
        out += QStringLiteral(" …");
    if (!ascii.isEmpty())
        out += QStringLiteral("   |")+ ascii + (data.size() > maxBytes ? "…" : "") + "|";
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  hexAsciiFull — classic 16-bytes-per-line hex+ASCII dump, monospace-ready
// ─────────────────────────────────────────────────────────────────────────────
QString CommDumpModel::hexAsciiFull(const QByteArray &data)
{
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
                ascii += asciiOrDot(b);
            } else {
                line += QStringLiteral("   ");
            }
            if (i == 7) line += ' ';
        }
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
    Record r;
    r.timestamp = QDateTime::currentDateTime();   // stamped at insertion, per spec
    r.plugin    = plugin;
    r.details   = details;
    r.isTx      = isTx;
    r.data      = data;

    const int row = m_records.size();
    beginInsertRows(QModelIndex(), row, row);
    m_records.append(std::move(r));
    endInsertRows();
}

void CommDumpModel::clear()
{
    if (m_records.isEmpty())
        return;
    beginResetModel();
    m_records.clear();
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
            if (rec->fullDumpCache.isEmpty())
                rec->fullDumpCache = hexAsciiFull(rec->data);
            return rec->fullDumpCache;
        }
        if (role == Qt::FontRole) {
            QFont f("JetBrains Mono");
            f.setStyleHint(QFont::Monospace);
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
        case ColTimestamp: return rec->timestamp.toString("HH:mm:ss.zzz");
        case ColPlugin:    return rec->plugin;
        case ColDetails:   return rec->details;
        case ColDir:       return rec->isTx ? QStringLiteral("Tx") : QStringLiteral("Rx");
        case ColLength:    return rec->data.size();
        case ColData:      return hexAsciiPreview(rec->data, k_previewBytes);
        default: return {};
        }
    case Qt::FontRole:
        if (index.column() == ColData) {
            QFont f("JetBrains Mono");
            f.setStyleHint(QFont::Monospace);
            return f;
        }
        return {};
    case Qt::ForegroundRole:
        if (index.column() == ColDir)
            return rec->isTx ? QBrush(QColor("#50fa7b")) : QBrush(QColor("#4a9eff"));
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
    case ColTimestamp: return QStringLiteral("Timestamp");
    case ColPlugin:    return QStringLiteral("Plugin");
    case ColDetails:   return QStringLiteral("Details");
    case ColDir:       return QStringLiteral("Dir");
    case ColLength:    return QStringLiteral("Length");
    case ColData:      return QStringLiteral("Data");
    default: return {};
    }
}
