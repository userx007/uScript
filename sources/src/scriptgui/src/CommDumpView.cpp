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

    // Moved CountLabel to the far right (after stretch)
    m_countLabel = new QLabel("", header);
    m_countLabel->setObjectName("panelInfo");

    // Removed ASCII checkbox from here; will add it to the right side below

    m_autoScrollCb = new QCheckBox("auto-scroll", header);
    m_autoScrollCb->setChecked(true);
    m_autoScrollCb->setToolTip("Keep scrolled to the latest record");

    // Create the ASCII checkbox here to place it next to auto-scroll
    m_asciiCb = new QCheckBox("show-ascii", header);
    m_asciiCb->setChecked(true);
    m_asciiCb->setToolTip("Show the ASCII column");

    // Timestamp display mode. A dropdown rather than a "rotate on double
    // click" gesture because the latter has no visible affordance — nothing
    // in the header hints that double-clicking does anything, so people are
    // unlikely to discover it on their own. The combo box is kept in sync
    // with double-clicking the Timestamp header too (added below, once
    // m_tree exists) as a convenience shortcut for people who do know it,
    // but the combo is the primary, discoverable control.
    m_timeFormatCb = new QComboBox(header);
    m_timeFormatCb->setToolTip("Timestamp display:\n"
                                " • hh:mm:ss.usec — wall-clock time\n"
                                " • Δ prev (s.usec) — delta since the previous record\n"
                                " • t0 (s.usec) — delta since the first record in the trace\n"
                                "(double-clicking the Timestamp column header cycles through these)");
    m_timeFormatCb->addItem("hh:mm:ss.usec",  CommDumpModel::TimeWallClock);
    m_timeFormatCb->addItem("Δ prev (s.usec)", CommDumpModel::TimeDeltaPrevious);
    m_timeFormatCb->addItem("t0 (s.usec)",     CommDumpModel::TimeSinceCaptureStart);

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

    // Initially disabled
    m_saveBtn->setEnabled(false);

    m_loadBtn = new QPushButton("LOAD", header);
    m_loadBtn->setObjectName("clearBtn");
    m_loadBtn->setToolTip("Reload a previously saved trace");
    connect(m_loadBtn, &QPushButton::clicked, this, &CommDumpView::onLoadTriggered);

    m_clearBtn = new QPushButton("CLEAR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear comm dump");

    // --- Updated Layout Order ---

    // Left side: Title and Filters
    hlay->addWidget(m_titleLabel);
    hlay->addWidget(m_dirFilterCb);
    hlay->addWidget(m_pluginFilterBtn);

    // Spacer to push everything to the right
    hlay->addSpacing(8);
    hlay->addStretch(1);

    // Right side: ASCII, Time format, Auto-Scroll, Count, and Action Buttons
    hlay->addWidget(m_timeFormatCb);
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
    connect(m_timeFormatCb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        m_model->setTimeFormat(static_cast<CommDumpModel::TimeFormat>(m_timeFormatCb->itemData(idx).toInt()));
    });
    // Double-click the Timestamp header to rotate abs <-> relative; routed
    // through the combo box (rather than calling setTimeFormat directly) so
    // the dropdown's displayed selection never drifts out of sync with it.
    connect(m_tree->header(), &QHeaderView::sectionDoubleClicked, this, [this](int section) {
        if (section == CommDumpModel::ColTimestamp)
            m_timeFormatCb->setCurrentIndex((m_timeFormatCb->currentIndex() + 1) % m_timeFormatCb->count());
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

    // Disable SAVE button when cleared
    m_saveBtn->setEnabled(false);
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

    // Get the current font size of the tree view
    const QFont treeFont = m_tree->font();
    const double currentFontSize = treeFont.pointSizeF();

    // m_fullDumpFontProportion is the view's own stored ratio (e.g. 0.8).
    // Deliberately NOT read back from m_model->fullDumpFontSize() — that
    // getter returns the *absolute* point size the model was last given,
    // and treating it as a proportion again would multiply it into itself
    // on every call (font change, load, etc.), growing without bound.
    const double newDumpSize = currentFontSize * m_fullDumpFontProportion;

    // Set the absolute size in the model
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
    // reload regardless of what's saved here — this "timeFormat" key is
    // purely a convenience so the trace reopens showing whichever mode was
    // active when it was saved. Wrapped in an object (rather than saving
    // the bare array like before) but onLoadTriggered() still accepts the
    // old plain-array files for backward compatibility.
    QJsonObject root;
    switch (m_model->timeFormat()) {
    case CommDumpModel::TimeWallClock:         root["timeFormat"] = QStringLiteral("wallClock");        break;
    case CommDumpModel::TimeDeltaPrevious:     root["timeFormat"] = QStringLiteral("deltaPrevious");    break;
    case CommDumpModel::TimeSinceCaptureStart: root["timeFormat"] = QStringLiteral("sinceCaptureStart"); break;
    }
    root["fontSizeProportion"] = m_fullDumpFontProportion;
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
    // Also accepts "absolute"/"relative" — the strings used by the
    // previous, 2-mode build of this feature — mapped onto their closest
    // equivalents here, so traces saved with that version still restore
    // a sensible display mode.
    QJsonArray recordsArr;
    CommDumpModel::TimeFormat loadedFormat = CommDumpModel::TimeWallClock;
    double fontSizeProp = m_fullDumpFontProportion; // keep current setting unless the file overrides it
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
    }

    m_model->loadJsonArray(recordsArr);
    m_fullDumpFontProportion = fontSizeProp;

    // beginResetModel()/endResetModel() drops all view-side per-row state
    // (spans, hidden flags) — rebuild it for the freshly loaded rows.
    for (int row = 0; row < m_model->recordCount(); ++row)
        m_tree->setFirstColumnSpanned(0, m_model->index(row, 0), true);

    m_dirFilterCb->setCurrentIndex(0);   // reset to "All" — old filter selection no longer applies
    m_timeFormatCb->setCurrentIndex(m_timeFormatCb->findData(loadedFormat));
    rebuildPluginMenuFromModel();
    reapplyAllFilters();
    updateCountLabel();

    // Update the full dump font size based on the loaded proportion and current tree font
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
