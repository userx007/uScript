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
#include <QScrollBar>
#include <QFont>

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

    m_titleLabel = new QLabel("COMM  DUMP | Plugin Rx/Tx", header);
    m_titleLabel->setObjectName("panelTitle");

    m_dirFilterCb = new QComboBox(header);
    m_dirFilterCb->setToolTip("Filter rows by direction");
    m_dirFilterCb->addItem("All");
    m_dirFilterCb->addItem("Rx only");
    m_dirFilterCb->addItem("Tx only");

    m_countLabel = new QLabel("", header);
    m_countLabel->setObjectName("panelInfo");

    m_autoScrollCb = new QCheckBox("auto-scroll", header);
    m_autoScrollCb->setChecked(true);
    m_autoScrollCb->setToolTip("Keep scrolled to the latest record");

    m_clearBtn = new QPushButton("CLEAR", header);
    m_clearBtn->setObjectName("clearBtn");
    m_clearBtn->setToolTip("Clear comm dump");

    hlay->addWidget(m_titleLabel);
    hlay->addWidget(m_dirFilterCb);
    hlay->addSpacing(8);
    hlay->addStretch(1);
    hlay->addWidget(m_autoScrollCb);
    hlay->addWidget(m_countLabel);
    hlay->addWidget(m_clearBtn);

    m_model = new CommDumpModel(this);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_model);
    m_tree->setObjectName("commDumpTree");
    m_tree->setRootIsDecorated(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(false);   // child rows are taller (full dump)
    m_tree->setWordWrap(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColTimestamp, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColPlugin,    QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColDetails,   QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColDir,       QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColLength,    QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(CommDumpModel::ColData,      QHeaderView::Stretch);
    m_tree->setColumnWidth(CommDumpModel::ColDetails, 160);

    root->addWidget(header);
    root->addWidget(m_tree, 1);

    connect(m_clearBtn, &QPushButton::clicked, this, &CommDumpView::clear);
    connect(m_autoScrollCb, &QCheckBox::toggled, this, &CommDumpView::setAutoScroll);
    connect(m_dirFilterCb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        // Re-apply the filter to every existing top-level row.
        for (int row = 0; row < m_model->recordCount(); ++row) {
            const QModelIndex idx = m_model->index(row, 0);
            const bool isTx = m_model->data(m_model->index(row, CommDumpModel::ColDir))
                                   .toString() == "Tx";
            const int  sel  = m_dirFilterCb->currentIndex();   // 0 All, 1 Rx, 2 Tx
            const bool hide = (sel == 1 && isTx) || (sel == 2 && !isTx);
            m_tree->setRowHidden(row, QModelIndex(), hide);
        }
    });

    updateCountLabel();
}

void CommDumpView::addRecord(const QString &plugin, const QString &details, bool isTx,
                              const QByteArray &data)
{
    const int newRow = m_model->recordCount();
    m_model->addRecord(plugin, details, isTx, data);

    const int sel  = m_dirFilterCb->currentIndex();
    const bool hide = (sel == 1 && isTx) || (sel == 2 && !isTx);
    if (hide)
        m_tree->setRowHidden(newRow, QModelIndex(), true);

    // First column of the child row spans the full row width, so its wrapped
    // hex-dump text is readable without horizontal scrolling.
    m_tree->setFirstColumnSpanned(0, m_model->index(newRow, 0), true);

    updateCountLabel();

    if (m_autoScroll && !hide)
        m_tree->scrollToBottom();
}

void CommDumpView::clear()
{
    m_model->clear();
    updateCountLabel();
}

void CommDumpView::setDumpFont(const QFont &font)
{
    m_tree->setFont(font);
}

void CommDumpView::updateCountLabel()
{
    const int n = m_model->recordCount();
    m_countLabel->setText(n == 0 ? QString() : QString("%1 record%2").arg(n).arg(n == 1 ? "" : "s"));
}
