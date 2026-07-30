#include "CommDumpView.hpp"
#include "CommDumpModel.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeView>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QToolButton>
#include <QMenu>
#include <QAction>
#include <QScrollBar>
#include <QFont>
#include <QFileDialog>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMessageBox>
#include <QEvent>
#include <QtGlobal>
#include <QApplication>
#include <QClipboard>
#include <QShortcut>
#include <QKeySequence>
#include <QItemSelectionModel>
#include <QSet>
#include <algorithm>

CommDumpView::CommDumpView(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("panelFrame");
    setFrameShape(QFrame::NoFrame);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName("panelHeader");
    header->setFrameShape(QFrame::NoFrame);

    auto *hlay = new QHBoxLayout(header);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(6);

    m_titleLabel = new QLabel("COMM  DUMP | Filters ", header);
    m_titleLabel->setObjectName("panelTitle");

    m_dirFilterCb = new QComboBox(header);
    m_dirFilterCb->setToolTip("Filter rows by direction");
    m_dirFilterCb->addItem("Rx/Tx");
    m_dirFilterCb->addItem("Rx only");
    m_dirFilterCb->addItem("Tx only");

    m_pluginFilterBtn = new QToolButton(header);
    m_pluginFilterBtn->setText("plugins");
    m_pluginFilterBtn->setToolTip("Filter rows by plugin");
    m_pluginFilterBtn->setPopupMode(QToolButton::InstantPopup);
    m_pluginMenu = new QMenu(m_pluginFilterBtn);
    m_pluginFilterBtn->setMenu(m_pluginMenu);

    m_countLabel = new QLabel("", header);
    m_countLabel->setObjectName("panelInfo");

    m_autoScrollCb = new QCheckBox("auto-scroll", header);
    m_autoScrollCb->setChecked(true);
    m_autoScrollCb->setToolTip("Keep scrolled to the latest record");

    m_asciiCb = new QCheckBox("show-ascii", header);
    m_asciiCb->setChecked(true);
    m_asciiCb->setToolTip("Show the ASCII column");

    m_saveBtn = new QToolButton(header);
    m_saveBtn->setText("SAVE");
    m_saveBtn->setToolTip("Save trace to a file");
    m_saveBtn->setPopupMode(QToolButton::InstantPopup);
    m_saveMenu = new QMenu(m_saveBtn);
    QAction *saveAllAct = m_saveMenu->addAction("Save all records");
    QAction *saveFilteredAct = m_saveMenu->addAction("Save filtered records");
    m_saveBtn->setMenu(m_saveMenu);
    connect(saveAllAct,      &QAction::triggered, this, &CommDumpView::onSaveAll);
    connect(saveFilteredAct, &QAction::triggered, this, &CommDumpView::onSaveFilteredOnly);

    m_saveBtn->setEnabled(false);   // nothing to save until a record arrives

    m_loadBtn = new QPushButton("LOAD", header);
    m_loadBtn->setObjectName("clearBtn");
    m_loadBtn->setToolTip("Reload a previously saved trace");
    connect(m_loadBtn, &QPushButton::clicked, this, &CommDumpView::onLoadTriggered);

    m_clearBtn = new QPushButton("CLEAR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear comm dump");

    // Left side: title + filters. Right side (after the stretch): ASCII
    // toggle, auto-scroll, record count, and action buttons.
    hlay->addWidget(m_titleLabel);
    hlay->addWidget(m_dirFilterCb);
    hlay->addWidget(m_pluginFilterBtn);

    hlay->addSpacing(8);
    hlay->addStretch(1);

    hlay->addWidget(m_asciiCb);
    hlay->addWidget(m_autoScrollCb);
    hlay->addWidget(m_countLabel);
    hlay->addWidget(m_saveBtn);
    hlay->addWidget(m_loadBtn);
    hlay->addWidget(m_clearBtn);

    m_model = new CommDumpModel(this);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setObjectName("commDumpTree");
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(false);
    m_tree->setWordWrap(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColTimestamp, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColPlugin,    QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColDetails,   QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColDir,       QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColLength,    QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColData,      QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColAscii,     QHeaderView::Stretch);
    m_tree->setColumnWidth(CommDumpModel::ColDetails, 160);
    m_tree->setColumnWidth(CommDumpModel::ColData, 220);
    m_tree->installEventFilter(this);

    // Multi-row selection (Ctrl/Shift-click) so several records can be
    // selected and copied at once.
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Right-click menu: Copy / Select All / Expand All / Collapse All.
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_treeContextMenu = new QMenu(m_tree);
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &CommDumpView::onTreeContextMenuRequested);

    // Ctrl+C copies the current selection (always as the full dump, see
    // buildCopyText()); Ctrl+A selects every row currently passing the
    // filters. WidgetWithChildrenShortcut so they only fire while the tree
    // (or its viewport) has focus, not globally across the whole window.
    auto *copyShortcut = new QShortcut(QKeySequence::Copy, m_tree);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, &CommDumpView::onCopySelected);

    auto *selectAllShortcut = new QShortcut(QKeySequence::SelectAll, m_tree);
    selectAllShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(selectAllShortcut, &QShortcut::activated, this, &CommDumpView::onSelectAllRows);

    root->addWidget(header);
    root->addWidget(m_tree, 1);

    connect(m_clearBtn, &QPushButton::clicked, this, &CommDumpView::clear);
    connect(m_autoScrollCb, &QCheckBox::toggled, this, &CommDumpView::setAutoScroll);
    connect(m_dirFilterCb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        reapplyAllFilters();
    });
    connect(m_asciiCb, &QCheckBox::toggled, this, [this](bool on) {
        m_model->setShowAscii(on);
        m_tree->setColumnHidden(CommDumpModel::ColAscii, !on);
    });
    // Double-click a header to cycle that column's display mode — the sole
    // control for both now that the Timestamp-format dropdown is gone (see
    // header comment above); TimeFormatCount is the enum's own sentinel so
    // this doesn't need a separately-maintained "how many modes" constant.
    connect(m_tree->header(), &QHeaderView::sectionDoubleClicked, this, [this](int section) {
        if (section == CommDumpModel::ColTimestamp) {
            const int next = (static_cast<int>(m_model->timeFormat()) + 1) % CommDumpModel::TimeFormatCount;
            m_model->setTimeFormat(static_cast<CommDumpModel::TimeFormat>(next));
        } else if (section == CommDumpModel::ColData) {
            m_model->setDumpBytesPerLine(m_model->dumpBytesPerLine() == 16 ? 8 : 16);
        }
    });

    // Handle double-click on the Timestamp column to expand/collapse
    // Expansion only occurs if the data exceeds the preview size
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.isValid()) return;

        // Only react if the click was on the Timestamp column
        if (index.column() != CommDumpModel::ColTimestamp) {
            return;
        }

        CommDumpModel *model = qobject_cast<CommDumpModel *>(m_tree->model());
        if (!model) return;

        const CommDumpModel::Record *rec = model->recordForIndex(index);
        if (!rec) return;

        bool shouldExpand = (rec->data.size() > CommDumpModel::k_previewBytes);

        if (shouldExpand) {
            if (m_tree->isExpanded(index)) {
                m_tree->collapse(index);
            } else {
                m_tree->expand(index);
            }
        } else {
            // If data is small, ensure it is collapsed (no-op if already collapsed)
            if (m_tree->isExpanded(index)) {
                m_tree->collapse(index);
            }
        }
    });


    updateCountLabel();

    // Initialize font size based on the tree's current font
    updateFullDumpFontSize();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Filtering — direction (combo box) AND plugin (menu of checkable actions)
//  are combined; a row is visible only if it passes both.
// ─────────────────────────────────────────────────────────────────────────────
bool CommDumpView::rowPassesFilters(int row) const
{
    const CommDumpModel::Record *rec = m_model->recordForIndex(m_model->index(row, 0));
    if (!rec)
        return true;   // shouldn't happen — don't hide a row we can't classify

    const int  sel = m_dirFilterCb->currentIndex();   // 0 All, 1 Rx, 2 Tx
    if ((sel == 1 && rec->isTx) || (sel == 2 && !rec->isTx))
        return false;

    if (auto *act = m_pluginActions.value(rec->plugin, nullptr))
        return act->isChecked();
    return true;   // unknown plugin (shouldn't happen) — don't hide it
}

void CommDumpView::reapplyAllFilters()
{
    for (int row = 0; row < m_model->recordCount(); ++row)
        m_tree->setRowHidden(row, QModelIndex(), !rowPassesFilters(row));
}

// ─────────────────────────────────────────────────────────────────────────────
//  ensurePluginKnown — adds a checked-by-default action to the plugin filter
//  menu the first time a given plugin name is seen. Called from addRecord(),
//  so the menu grows dynamically as new plugins report traffic.
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpView::ensurePluginKnown(const QString &plugin)
{
    if (m_pluginActions.contains(plugin))
        return;

    auto *act = m_pluginMenu->addAction(plugin);
    act->setCheckable(true);
    act->setChecked(true);
    connect(act, &QAction::toggled, this, &CommDumpView::onPluginActionToggled);
    m_pluginActions.insert(plugin, act);
}

void CommDumpView::onPluginActionToggled(bool /*checked*/)
{
    reapplyAllFilters();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Select all / expand all / collapse all
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpView::onSelectAllRows()
{
    // Deliberately not m_tree->selectAll(): that would also pull in child
    // (full-dump) rows for whichever records happen to be expanded right
    // now, double-counting them against their own parent row. Selecting
    // column-spanning ranges over just the top-level rows (skipping any
    // currently hidden by the direction/plugin filters) is what "select
    // all" should mean here — buildCopyText() already always emits the
    // full dump regardless of expand state, so there's no need to select
    // the child rows too.
    QItemSelection sel;
    for (int row = 0; row < m_model->recordCount(); ++row) {
        if (m_tree->isRowHidden(row, QModelIndex()))
            continue;
        const QModelIndex left  = m_model->index(row, 0);
        const QModelIndex right = m_model->index(row, CommDumpModel::ColCount - 1);
        if (left.isValid() && right.isValid())
            sel.select(left, right);
    }
    m_tree->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
}

void CommDumpView::onExpandAll()
{
    m_tree->expandAll();
}

void CommDumpView::onCollapseAll()
{
    m_tree->collapseAll();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Copy to clipboard — always the full dump, never the truncated preview.
// ─────────────────────────────────────────────────────────────────────────────
QList<int> CommDumpView::selectedRecordRows() const
{
    QSet<int> rows;
    const QModelIndexList sel = m_tree->selectionModel()->selectedRows(0);
    for (const QModelIndex &idx : sel) {
        if (!idx.isValid())
            continue;
        // A selected full-dump child row belongs to its parent record; fold
        // it back onto the same row number so it isn't copied twice.
        const QModelIndex top = m_model->isChildRow(idx) ? m_model->parent(idx) : idx;
        if (top.isValid())
            rows.insert(top.row());
    }
    QList<int> result = rows.values();
    std::sort(result.begin(), result.end());
    return result;
}

QString CommDumpView::buildCopyText(const QList<int> &rows) const
{
    QString out;
    for (int i = 0; i < rows.size(); ++i) {
        const int row = rows[i];
        const CommDumpModel::Record *rec = m_model->recordForIndex(m_model->index(row, 0));
        if (!rec)
            continue;

        const QString ts = m_model->data(m_model->index(row, CommDumpModel::ColTimestamp)).toString();
        const int     len = rec->data.size();

        // Mirrors the tree's own column order (Timestamp | Plugin | Details
        // | Dir | Length), tab-separated so it pastes cleanly into a table
        // if the destination understands tabs, and reads fine as plain text
        // if it doesn't.
        out += QStringLiteral("%1\t%2\t%3\t%4\t%5 byte%6\n")
                   .arg(ts, rec->plugin, rec->details, rec->isTx ? QStringLiteral("Tx") : QStringLiteral("Rx"))
                   .arg(len)
                   .arg(len == 1 ? QStringLiteral("") : QStringLiteral("s"));

        // Always the full hex+ASCII dump — "expand the data" applies even
        // if this particular row is currently collapsed on screen.
        const QString dump = m_model->fullDumpForRow(row);
        if (!dump.isEmpty())
            out += dump + '\n';

        if (i + 1 < rows.size())
            out += '\n';
    }
    return out;
}

void CommDumpView::onCopySelected()
{
    const QList<int> rows = selectedRecordRows();
    if (rows.isEmpty())
        return;

    const QString text = buildCopyText(rows);
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void CommDumpView::onTreeContextMenuRequested(const QPoint &pos)
{
    m_treeContextMenu->clear();

    const bool hasSelection = m_tree->selectionModel()->hasSelection();
    const bool hasRecords    = m_model->recordCount() > 0;

    QAction *copyAct = m_treeContextMenu->addAction("Copy");
    copyAct->setEnabled(hasSelection);
    copyAct->setShortcut(QKeySequence::Copy);
    connect(copyAct, &QAction::triggered, this, &CommDumpView::onCopySelected);

    QAction *selectAllAct = m_treeContextMenu->addAction("Select All");
    selectAllAct->setEnabled(hasRecords);
    selectAllAct->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAct, &QAction::triggered, this, &CommDumpView::onSelectAllRows);

    m_treeContextMenu->addSeparator();

    QAction *expandAllAct = m_treeContextMenu->addAction("Expand All");
    expandAllAct->setEnabled(hasRecords);
    connect(expandAllAct, &QAction::triggered, this, &CommDumpView::onExpandAll);

    QAction *collapseAllAct = m_treeContextMenu->addAction("Collapse All");
    collapseAllAct->setEnabled(hasRecords);
    connect(collapseAllAct, &QAction::triggered, this, &CommDumpView::onCollapseAll);

    m_treeContextMenu->exec(m_tree->viewport()->mapToGlobal(pos));
}

// ─────────────────────────────────────────────────────────────────────────────
//  rebuildPluginMenuFromModel — used after a LOAD replaces every record:
//  throws away the old menu (whatever plugins/filters were set up before)
//  and re-derives the full plugin list from what's now in the model, each
//  starting visible.
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpView::rebuildPluginMenuFromModel()
{
    m_pluginMenu->clear();
    m_pluginActions.clear();
    for (int row = 0; row < m_model->recordCount(); ++row) {
        const QString plugin = m_model->data(m_model->index(row, CommDumpModel::ColPlugin)).toString();
        ensurePluginKnown(plugin);
    }
}

void CommDumpView::addRecord(const QString &plugin, const QString &details, bool isTx,
                              const QByteArray &data)
{
    const int newRow = m_model->recordCount();
    m_model->addRecord(plugin, details, isTx, data);
    ensurePluginKnown(plugin);

    const bool hide = !rowPassesFilters(newRow);
    if (hide)
        m_tree->setRowHidden(newRow, QModelIndex(), true);

    // First column of the child row spans the full row width, so its wrapped
    // hex-dump text is readable without horizontal scrolling.
    m_tree->setFirstColumnSpanned(0, m_model->index(newRow, 0), true);

    updateCountLabel();

    if (!m_saveBtn->isEnabled()) {
        m_saveBtn->setEnabled(true);
    }

    if (m_autoScroll && !hide)
        m_tree->scrollToBottom();
}

void CommDumpView::clear()
{
    m_model->clear();
    m_pluginMenu->clear();
    m_pluginActions.clear();
    updateCountLabel();
    m_saveBtn->setEnabled(false);   // nothing to save until a new record arrives
}

void CommDumpView::setDumpFont(const QFont &font)
{
    m_tree->setFont(font);
}

void CommDumpView::setTreeFont(const QFont &font)
{
    m_tree->setFont(font);
    updateFullDumpFontSize();
}

void CommDumpView::updateFullDumpFontSize()
{
    if (!m_tree) return;

    const QFont treeFont = m_tree->font();
    const double currentFontSize = treeFont.pointSizeF();

    // m_fullDumpFontProportion is the view's own stored ratio (e.g. 0.8).
    // Deliberately NOT read back from m_model->fullDumpFontSize() — that
    // getter returns the *absolute* point size the model was last given,
    // and treating it as a proportion again would multiply it into itself
    // on every call (font change, load, etc.), growing without bound.
    const double newDumpSize = currentFontSize * m_fullDumpFontProportion;

    m_model->setFullDumpFontSize(newDumpSize);
}


void CommDumpView::updateCountLabel()
{
    const int n = m_model->recordCount();
    m_countLabel->setText(n == 0 ? QString() : QString("%1 record%2").arg(n).arg(n == 1 ? "" : "s"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Save / reload traces
// ─────────────────────────────────────────────────────────────────────────────
void CommDumpView::onSaveAll()          { saveToFile(false); }
void CommDumpView::onSaveFilteredOnly() { saveToFile(true); }

void CommDumpView::saveToFile(bool filteredOnly)
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Save Comm Dump Trace", QString(), "Comm Dump Trace (*.json)");
    if (path.isEmpty())
        return;

    QList<int> rows;
    if (filteredOnly) {
        for (int row = 0; row < m_model->recordCount(); ++row) {
            if (!m_tree->isRowHidden(row, QModelIndex()))
                rows << row;
        }
    }

    // Records always carry raw absolute microsecond timestamps (see
    // CommDumpModel::recordToJson), so relative time is derivable after
    // reload regardless of what's saved here — "timeFormat" is purely a
    // convenience so the trace reopens showing whichever mode was active
    // when it was saved. Wrapped in an object (rather than a bare array) so
    // timeFormat/fontSizeProportion can travel alongside the records;
    // onLoadTriggered() still accepts a bare records array for older files.
    QJsonObject root;
    switch (m_model->timeFormat()) {
    case CommDumpModel::TimeWallClock:         root["timeFormat"] = QStringLiteral("wallClock");        break;
    case CommDumpModel::TimeDeltaPrevious:     root["timeFormat"] = QStringLiteral("deltaPrevious");    break;
    case CommDumpModel::TimeSinceCaptureStart: root["timeFormat"] = QStringLiteral("sinceCaptureStart"); break;
    }
    root["fontSizeProportion"] = m_fullDumpFontProportion;
    root["dumpBytesPerLine"] = m_model->dumpBytesPerLine();
    root["records"] = m_model->toJsonArray(rows);

    const QJsonDocument doc(root);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, "Save failed", "Could not write to:\n" + path);
        return;
    }
    f.write(doc.toJson(QJsonDocument::Compact));
}

void CommDumpView::onLoadTriggered()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Load Comm Dump Trace", QString(), "Comm Dump Trace (*.json)");
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Load failed", "Could not read:\n" + path);
        return;
    }

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !(doc.isArray() || doc.isObject())) {
        QMessageBox::warning(this, "Load failed",
                              "File is not a valid comm dump trace:\n" + err.errorString());
        return;
    }

    // New files are {"timeFormat": ..., "records": [...]}; older ones are
    // just the bare records array (no timeFormat, defaults to wall-clock).
    // Also accepts the legacy "absolute"/"relative" strings some older
    // trace files use, mapped onto their closest equivalent here.
    QJsonArray recordsArr;
    CommDumpModel::TimeFormat loadedFormat = CommDumpModel::TimeWallClock;
    double fontSizeProp = m_fullDumpFontProportion; // keep current setting unless the file overrides it
    int loadedBytesPerLine = m_model->dumpBytesPerLine(); // ditto
    if (doc.isArray()) {
        recordsArr = doc.array();
    } else {
        const QJsonObject root = doc.object();
        recordsArr = root.value("records").toArray();
        const QString fmt = root.value("timeFormat").toString();
        if (fmt == QStringLiteral("deltaPrevious") || fmt == QStringLiteral("relative"))
            loadedFormat = CommDumpModel::TimeDeltaPrevious;
        else if (fmt == QStringLiteral("sinceCaptureStart"))
            loadedFormat = CommDumpModel::TimeSinceCaptureStart;
        // else "wallClock"/"absolute"/unknown/missing -> TimeWallClock (default)

        // Load font size proportion if present, clamped to a sane range so
        // a corrupt or hand-edited file (0, negative, or absurdly large)
        // can't produce a zero-size or huge dump font.
        if (root.contains("fontSizeProportion"))
            fontSizeProp = qBound(0.1, root.value("fontSizeProportion").toDouble(), 5.0);

        // Only 8 and 16 are valid; anything else (missing key, hand-edited
        // file) falls back to whatever's currently set rather than silently
        // accepting a nonsensical bytes-per-line.
        if (root.contains("dumpBytesPerLine")) {
            const int v = root.value("dumpBytesPerLine").toInt();
            if (v == 8 || v == 16)
                loadedBytesPerLine = v;
        }
    }

    m_model->loadJsonArray(recordsArr);
    m_fullDumpFontProportion = fontSizeProp;

    // beginResetModel()/endResetModel() drops all view-side per-row state
    // (spans, hidden flags) — rebuild it for the freshly loaded rows.
    for (int row = 0; row < m_model->recordCount(); ++row)
        m_tree->setFirstColumnSpanned(0, m_model->index(row, 0), true);

    m_dirFilterCb->setCurrentIndex(0);   // reset to "All": the new trace may have a different plugin set
    m_model->setTimeFormat(loadedFormat);
    m_model->setDumpBytesPerLine(loadedBytesPerLine);
    rebuildPluginMenuFromModel();
    reapplyAllFilters();
    updateCountLabel();

    updateFullDumpFontSize();

    if (m_autoScroll)
        m_tree->scrollToBottom();
}

bool CommDumpView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_tree && event->type() == QEvent::FontChange) {
        updateFullDumpFontSize();
    }
    return QFrame::eventFilter(watched, event);
}
