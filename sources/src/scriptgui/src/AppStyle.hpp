#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  Application-wide Qt Style Sheet.
//  "Industrial Terminal" aesthetic: charcoal base, electric-blue accents,
//  amber execution bands, monospace code panels.
// ─────────────────────────────────────────────────────────────────────────────

inline const QString APP_STYLESHEET = R"(

/* ── Global ─────────────────────────────────────────────────────────────── */

QMainWindow, QDialog {
    background-color: #13151a;
}

QWidget {
    background-color: #13151a;
    color: #c8d0dc;
    font-family: "Segoe UI", "SF Pro Display", "Helvetica Neue", sans-serif;
    font-size: 12px;
}

/* ── Splitters ───────────────────────────────────────────────────────────── */

QSplitter::handle {
    background-color: #252a35;
}
QSplitter::handle:horizontal {
    width: 3px;
}
QSplitter::handle:vertical {
    height: 3px;
}
QSplitter::handle:hover {
    background-color: #4a9eff;
}

/* ── Panel frames ────────────────────────────────────────────────────────── */

QFrame#panelFrame {
    background-color: #1c1f27;
    border: 1px solid #252a35;
    border-radius: 4px;
}

/* ── Panel header bars ───────────────────────────────────────────────────── */

QFrame#panelHeader {
    background-color: #0e1016;
    border-bottom: 1px solid #252a35;
    border-radius: 0px;
    min-height: 30px;
    max-height: 30px;
}

QLabel#panelTitle {
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    font-size: 10px;
    font-weight: bold;
    letter-spacing: 2px;
    padding-left: 10px;
    color: #d3d3d3;
}

QLabel#panelInfo {
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    font-size: 10px;
    padding-right: 10px;
    color: #d3d3d3;
}

/* ── Toolbar ─────────────────────────────────────────────────────────────── */

QFrame#toolbar {
    background-color: #0e1016;
    border-bottom: 2px solid #252a35;
    min-height: 52px;
    max-height: 52px;
}

QLabel#toolbarLabel {
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    font-size: 10px;
    font-weight: bold;
    letter-spacing: 2px;	
    color: #d3d3d3;
}

/* ── Line edits ──────────────────────────────────────────────────────────── */

QLineEdit {
    background-color: #12141a;
    color: #c8d0dc;
    border: 1px solid #252a35;
    border-radius: 3px;
    padding: 4px 8px;
    font-family: "JetBrains Mono", "Cascadia Code", "Consolas", monospace;
    font-size: 13px;
    selection-background-color: #4a9eff44;
}
QLineEdit:focus {
    border-color: #4a9eff;
}
QLineEdit:hover {
    border-color: #353a48;
}

/* ── Buttons ─────────────────────────────────────────────────────────────── */

QPushButton {
    background-color: #1c1f27;
    color: #d3d3d3;
    border: 1px solid #252a35;
    border-radius: 3px;
    padding: 5px 14px;
    font-size: 13px;
    min-width: 70px;
}
QPushButton:hover {
    background-color: #252a35;
    border-color: #4a9eff;
    color: #ffffff;
}
QPushButton:pressed {
    background-color: #0e1016;
}
QPushButton:disabled {
    color: #404855;
    border-color: #1c1f27;
}

QPushButton#browseBtn {
    min-width: 28px;
    max-width: 28px;
    padding: 5px 6px;
    color: #60697a;
    font-size: 14px;
}
QPushButton#browseBtn:hover {
    color: #4a9eff;
}

QPushButton#startBtn {
    background-color: #1a3a1a;
    border-color: #2a5a2a;
    color: #50fa7b;
    font-weight: bold;
    min-width: 90px;
    font-size: 12px;
    letter-spacing: 1px;
}
QPushButton#startBtn:hover {
    background-color: #1e4a1e;
    border-color: #50fa7b;
}
QPushButton#startBtn:pressed {
    background-color: #0f2a0f;
}

QPushButton#startBtn[running="true"] {
    background-color: #3a1a1a;
    border-color: #6a2a2a;
    color: #ff5555;
}
QPushButton#startBtn[running="true"]:hover {
    background-color: #4a1a1a;
    border-color: #ff5555;
}

QPushButton#clearBtn {
    min-width: 28px;
    max-width: 60px;
    padding: 4px 8px;
    font-size: 10px;
    color: #60697a;
}
QPushButton#clearBtn:hover {
    color: #ff5555;
    border-color: #ff5555;
}

QPushButton#stopBtn {
    min-width: 28px;
    max-width: 60px;
    padding: 4px 8px;
    font-size: 10px;
    color: #ff5555;
    border-color: #ff5555;
}
QPushButton#stopBtn:hover {
    color: #ff0000;
    border-color: #ff0000;
}
QPushButton#stopBtn:disabled {
    color: #3a3f4b;
    border-color: #2a2f3a;
}

/* ── Script viewers (QPlainTextEdit) ─────────────────────────────────────── */

QPlainTextEdit#scriptView {
    background-color: #12141a;
    color: #abb2bf;
    border: none;
    selection-background-color: #4a9eff33;
    /* font-family and font-size are set programmatically via setFont()
       so they are intentionally absent here — QSS would override setFont(). */
}

/* ── Log viewer ──────────────────────────────────────────────────────────── */

QTextEdit#logView {
    background-color: #0d0f14;
    color: #abb2bf;
    border: none;
    selection-background-color: #4a9eff33;
    /* font set programmatically; HTML spans carry per-line font-family anyway */
}

/* ── Shell terminal display ──────────────────────────────────────────────── */

QTextEdit#shellView {
    background-color: #0a0c10;
    color: #abb2bf;
    border: none;
    border-top: 1px solid #252a35;
    selection-background-color: #4a9eff33;
    /* font set programmatically via setTerminalFont() */
}

/* ── Status bar ──────────────────────────────────────────────────────────── */

QFrame#statusBar {
    background-color: #0e1016;
    border-top: 1px solid #252a35;
    min-height: 24px;
    max-height: 24px;
}

QLabel#statusText {
    color: #E5F2C9;
    font-size: 13px;
    padding-left: 8px;
}

QLabel#statusRight {
    color: #E5F2C9;
    font-size: 13px;
    padding-right: 8px;
}

/* ── Scrollbars ──────────────────────────────────────────────────────────── */

QScrollBar:vertical {
    background: #0d0f14;
    width: 8px;
    margin: 0;
}
QScrollBar::handle:vertical {
    background: #252a35;
    border-radius: 4px;
    min-height: 20px;
}
QScrollBar::handle:vertical:hover {
    background: #353a48;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

QScrollBar:horizontal {
    background: #0d0f14;
    height: 8px;
    margin: 0;
}
QScrollBar::handle:horizontal {
    background: #252a35;
    border-radius: 4px;
    min-width: 20px;
}
QScrollBar::handle:horizontal:hover {
    background: #353a48;
}
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

/* ── Tooltips ────────────────────────────────────────────────────────────── */

QToolTip {
    background-color: #1c1f27;
    color: #c8d0dc;
    border: 1px solid #4a9eff;
    padding: 4px 8px;
    border-radius: 3px;
}

/* ── CheckBox ────────────────────────────────────────────────────────────── */

QCheckBox {
    color: #60697a;
    font-size: 11px;
    spacing: 6px;
}
QCheckBox:hover { color: #c8d0dc; }
QCheckBox::indicator {
    width: 14px;
    height: 14px;
    background-color: #12141a;
    border: 1px solid #252a35;
    border-radius: 2px;
}
QCheckBox::indicator:checked {
    background-color: #4a9eff;
    border-color: #4a9eff;
}

/* ── Context menu (right-click) ──────────────────────────────────────────── */

QMenu {
    background-color: #1c1f27;
    border:           1px solid #353a48;
    border-radius:    4px;
    padding:          4px 0px;
    color:            #c8d0dc;
}

QMenu::item {
    background-color: transparent;
    padding:          6px 28px 6px 24px;
    font-size:        13px;
    border-radius:    2px;
    margin:           1px 4px;
}

QMenu::item:selected {
    background-color: #2d3240;
    color:            #ffffff;
    border-left:      2px solid #4a9eff;
    padding-left:     22px;   /* compensate for the 2px border */
}

QMenu::item:disabled {
    color:            #404855;
}

QMenu::separator {
    height:           1px;
    background:       #252a35;
    margin:           4px 8px;
}

QMenu::icon {
    padding-left:     6px;
}

/* Keyboard shortcut text (shown on the right side of each item) */
QMenu::item:selected {
    /* shortcut colour inherits from item:selected — no override needed */
}

)";
