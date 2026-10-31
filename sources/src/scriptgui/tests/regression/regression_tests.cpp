#include <iostream>
#include <sstream>
#include <QApplication>
#include <QtTest>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTabBar>
#include <QTabWidget>
#include <QSettings>
#include <QDialog>
#include <QImage>
#include <QElapsedTimer>
#define private public
#define protected public
#include "MainWindow.hpp"
#include "CommDumpModel.hpp"
#include "SetupRunner.hpp"
#include "ScriptHighlighter.hpp"
#include "CommScriptHighlighter.hpp"
#include "IniHighlighter.hpp"
#undef private
#undef protected
static int fails=0;
#define CHECK(name, cond) do{ bool ok=(cond); if(!ok) ++fails; std::cout<<(ok?"PASS ":"FAIL ")<<name<<" [fails="<<fails<<"]"<<std::endl; }while(0)
static QByteArray readAll(const QString&p){ QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void write(const QString&p,const QByteArray&d,bool exec=false){ QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); f.close(); if(exec) QFile::setPermissions(p, QFile::permissions(p)|QFileDevice::ExeOwner); }
int main(int argc,char**argv){
  QApplication app(argc,argv);
  if(argc>2 && QString(argv[1])=="sudoasync"){ // run as a non-root user; argv[2] = writable work dir with setup.sh and bin/sudo (sleeps 2s, exit 1)
    QCoreApplication::setOrganizationName("audit"); QCoreApplication::setApplicationName("t");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, argv[2]); QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, argv[2]);
    MainWindow w; w.show(); QString setup=QString(argv[2])+"/setup.sh"; w.addTab(setup); w.m_setupPath=setup;
    int ticks=0; QTimer tick; QObject::connect(&tick,&QTimer::timeout,[&]{++ticks;}); tick.start(50);
    // the credentials dialog opens after the probe: dismiss it (Cancel)
    QTimer poll; QObject::connect(&poll,&QTimer::timeout,[&]{ if(auto*m=qobject_cast<QDialog*>(QApplication::activeModalWidget())) m->reject(); }); poll.start(100);
    QElapsedTimer el; el.start(); w.runSetupScript(); qint64 ms=el.elapsed();
    std::cout<<"probe+dialog took "<<ms<<" ms, GUI timer ticks during it: "<<ticks<<std::endl;
    CHECK("GUI stays responsive during slow sudo probe (>=20 ticks in ~2s)", ticks>=20 && ms>=1800);
    CHECK("cancel leaves no running setup", !w.m_setupRunner->isRunning() && !w.m_setupBusy);
    std::cout<<(fails?"FAILURES: ":"ALL PASSED ")<<fails<<std::endl; return fails; }
  QTemporaryDir td; QTemporaryDir cfgdir;
  QCoreApplication::setOrganizationName("audit"); QCoreApplication::setApplicationName("t");
  QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, cfgdir.path());
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cfgdir.path());
  if(argc>1 && QString(argv[1])=="bigmove"){ TermView tv; tv.processBytes("\x1b[2000000000B\x1b[2000000000C\x1b[2000000000;2000000000H" "x"); std::cout<<"PASS huge escape params survive"<<std::endl; return 0; }

  // highlighter regexes all valid
  { QTextDocument d; ScriptHighlighter a(&d); CommScriptHighlighter b(&d); IniHighlighter c(&d); bool allOk=true; QString bad;
    for(auto&r:a.m_rules) if(!r.pattern.isValid()){allOk=false;bad=r.pattern.pattern();}
    for(auto&r:b.m_rules) if(!r.pattern.isValid()){allOk=false;bad=r.pattern.pattern();}
    CHECK("all highlighter regexes valid "+bad.left(40).toStdString(), allOk); }
  // encoding / CRLF / BOM roundtrip
  { QString p=td.filePath("l1.txt"); write(p,"# \xB0" "C \xB5s\r\nX := 1\r\n"); ScriptViewer v; v.loadScript(p); v.m_editor->moveCursor(QTextCursor::End); v.m_editor->insertPlainText("Y := 2"); v.save();
    CHECK("latin1+CRLF preserved on save", readAll(p)==QByteArray("# \xB0" "C \xB5s\r\nX := 1\r\nY := 2")); 
    QString q=td.filePath("u8.txt"); write(q,"\xEF\xBB\xBF# \xC2\xB5s\nA := 1\n"); ScriptViewer w; w.loadScript(q); w.m_editor->moveCursor(QTextCursor::End); w.m_editor->insertPlainText("B"); w.save();
    CHECK("UTF-8 BOM + LF preserved", readAll(q)==QByteArray("\xEF\xBB\xBF# \xC2\xB5s\nA := 1\nB")); }
  // failed load
  { ScriptViewer v; bool ok=v.loadScript(td.filePath("gone.txt")); CHECK("failed load returns false and binds no file", !ok && v.currentFile().isEmpty() && v.m_editor->toPlainText().isEmpty()); }
  // tab stop follows zoom
  { ScriptViewer v; v.show(); v.setEditorFont(QFont("DejaVu Sans Mono",24)); QApplication::processEvents(); qreal sp=QFontMetricsF(v.m_editor->font()).horizontalAdvance(' ');
    CHECK("tab stop tracks font", qAbs(v.m_editor->tabStopDistance()-4*sp)<1.0); }
  // INCLUDE: keyboard no, click yes
  { ScriptViewer v; v.loadText("FOO := 1\nINCLUDE \"x.txt\"\nBAR := 2\n"); v.resize(500,300); v.show(); QSignalSpy sp(&v,&ScriptViewer::includeFileRequested);
    QTest::keyClick(v.m_editor,Qt::Key_Home,Qt::ControlModifier); QTest::keyClick(v.m_editor,Qt::Key_Down); CHECK("arrow keys do not open INCLUDE", sp.count()==0);
    QTextBlock b=v.m_editor->document()->findBlockByNumber(1); QRect r=v.m_editor->cursorRect(QTextCursor(b)); QTest::mouseClick(v.m_editor->viewport(),Qt::LeftButton,{},r.center()+QPoint(20,0));
    CHECK("mouse click opens INCLUDE", sp.count()==1); }
  // Tab with multi-line selection
  { ScriptViewer v; v.loadText("alpha\nbeta\ngamma"); v.show(); QTextCursor c=v.m_editor->textCursor(); c.select(QTextCursor::Document); v.m_editor->setTextCursor(c); QTest::keyClick(v.m_editor,Qt::Key_Tab);
    std::cout<<"  got: ["<<v.m_editor->toPlainText().replace("\n","|").toStdString()<<"]"<<std::endl; CHECK("Tab indents selected lines", v.m_editor->toPlainText()=="    alpha\n    beta\n    gamma"); }
  // terminal
  { ShellTerminal st; st.show(); TermView*tv=st.findChild<TermView*>(); QSignalSpy sp(tv,&TermView::keyBytesReady); tv->setFocus(); QTest::keyClick(tv,Qt::Key_Tab); CHECK("Tab reaches shell", sp.count()==1 && sp.at(0).at(0).toByteArray()=="\t"); }
  // terminal: long lines are reachable by horizontal scrolling and never paint over the gutter
  { ShellTerminal st; st.resize(500,200); st.show(); TermView*tv=st.findChild<TermView*>(); tv->processBytes(QByteArray(400,'W')+"\r\nshort\r\n"); QApplication::processEvents();
    auto*hb=tv->horizontalScrollBar(); CHECK("long line gives a horizontal scroll range", hb->maximum()>0);
    hb->setValue(hb->maximum()); QApplication::processEvents(); QImage img=tv->viewport()->grab().toImage(); bool gutterClean=true; QColor gbg(0x11,0x13,0x18);
    for(int y=0;y<img.height();++y) for(int x=0;x<tv->m_gutterW-1;++x){ QColor c=img.pixelColor(x,y); if(c!=gbg && qAbs(c.red()-gbg.red())+qAbs(c.green()-gbg.green())+qAbs(c.blue()-gbg.blue())>200 && y>tv->m_ch*3) gutterClean=false; }
    CHECK("scrolled text does not overpaint the gutter", gutterClean && img.width()>0);
    tv->processBytes(QByteArray(400,'x')); QApplication::processEvents(); CHECK("view follows the cursor column at the bottom", hb->value()>0); }
  // log cap: dropped lines are counted, gutter numbering stays true, saved file is annotated
  { LogViewer lv; lv.m_logEdit->document()->setMaximumBlockCount(100); lv.setScriptPath(td.filePath("proj/s.txt")); for(int i=0;i<250;++i) lv.appendLine(QString("line %1").arg(i));
    CHECK("dropped lines counted and shown", lv.m_dropped==150 && lv.m_countLabel->text().contains("150 older dropped") && lv.m_logEdit->m_lineOffset==150);
    lv.saveLog(); QDir d(td.filePath("proj/logs")); auto fl=d.entryList({"*.log"}); QByteArray txt= fl.isEmpty()?QByteArray():readAll(d.filePath(fl.first()));
    CHECK("saved log carries a truncation note", txt.startsWith("# NOTE: the first 150 lines") && txt.contains("line 249"));
    lv.clear(); CHECK("clear resets drop tracking", lv.m_dropped==0 && lv.m_logEdit->m_lineOffset==0); }
  // sudo password handling: reaches sudo via askpass only, never via the environment; removed afterwards
  { QString d=td.filePath("pwtest"); QDir().mkpath(d+"/bin"); write(d+"/bin/sudo","#!/bin/sh\n[ \"$1\" = \"-A\" ] && echo \"ASKPASS_GAVE=$($SUDO_ASKPASS)\"\necho \"ASKPASS_PATH=$SUDO_ASKPASS\"\necho \"ENV_HAS_PW=$(env | grep -c 's3cret')\"\n",true);
    write(d+"/setup.sh","#!/bin/bash\nsudo true\n",true); qputenv("PATH",(d+"/bin:"+QString(qgetenv("PATH"))).toUtf8());
    SetupRunner r; QStringList out; QObject::connect(&r,&SetupRunner::outputLine,[&](const QString&l,bool){out<<l;}); bool fin=false; QObject::connect(&r,&SetupRunner::finished,[&]{fin=true;});
    r.start(d+"/setup.sh","","s3cret"); for(int i=0;i<100&&!fin;++i) QTest::qWait(50); QString all=out.join("|"); QString ap; for(auto&l:out) if(l.startsWith("ASKPASS_PATH=")) ap=l.mid(13);
    CHECK("askpass helper delivers the password to sudo", all.contains("ASKPASS_GAVE=s3cret"));
    CHECK("password is not in the child environment", all.contains("ENV_HAS_PW=0"));
    CHECK("helper and password file are removed after the run", fin && !ap.isEmpty() && !QFileInfo::exists(ap) && !QFileInfo::exists(QFileInfo(ap).absolutePath()+"/pw")); }
  // model tester incl. layoutChanged paths
  { CommDumpModel m; QAbstractItemModelTester t(&m,QAbstractItemModelTester::FailureReportingMode::Fatal); m.setMaxRecords(10);
    for(int i=0;i<30;++i) m.addRecord(1000*i,"UART","a",i&1,QByteArray(20,'x')); m.setFullDumpFontSize(14); m.setDumpBytesPerLine(32); m.setCollapsedMode(true); m.setDumpBytesPerLine(8); CHECK("model invariants after zoom/layout", true); }
  // MainWindow behaviours with a fake interpreter
  { QString dir=td.filePath("dir with space"); QDir().mkpath(dir);
    QString fake=dir+"/fake interp.sh"; write(fake,"#!/bin/sh\nprintf '    indented line\\n'\nprintf 'GUI:EXEC_MAIN:1\\n'\nexit 0\n",true);
    QString script=dir+"/s.txt"; write(script,"FOO := 1\n");
    { MainWindow w; w.show(); w.addTab(script); w.m_interpreterPath=fake; w.onStartStop(); for(int i=0;i<100 && (w.m_process->state()!=QProcess::NotRunning || w.m_running);++i) QTest::qWait(50); QTest::qWait(200);
      QString log=w.m_w3->m_logEdit->toPlainText();
      CHECK("interpreter path with spaces runs", log.contains("Interpreter finished") && log.contains("exit code 0"));
      CHECK("leading indentation preserved in log", log.contains("    indented line")); }
    QString bad=dir+"/notexec.sh"; write(bad,"x"); // not executable -> FailedToStart
    { MainWindow w; w.show(); w.addTab(script); w.m_interpreterPath=bad; w.onStartStop(); QTest::qWait(300);
      CHECK("FailedToStart reported and state reset", !w.m_runningViewer && !w.m_running && w.m_w3->m_logEdit->toPlainText().contains("Could not start the interpreter")); }
    { // STOP: non-blocking, cooperative (flag file), and escalating
      QString coop=dir+"/coop.sh"; write(coop,"#!/bin/sh\nwhile [ ! -e \"$SCRIPT_STOP_FLAG_FILE\" ]; do sleep 0.05; done\nexit 0\n",true);
      QString hang=dir+"/hang.sh"; write(hang,"#!/bin/sh\nsleep 30\n",true);
      { MainWindow w; w.show(); w.addTab(script); w.m_interpreterPath=coop; w.onStartStop(); QTest::qWait(300); int ticks=0; QTimer t; QObject::connect(&t,&QTimer::timeout,[&]{++ticks;}); t.start(20);
        QElapsedTimer el; el.start(); w.onStartStop(); qint64 callMs=el.elapsed(); for(int i=0;i<100 && w.m_process->state()!=QProcess::NotRunning;++i) QTest::qWait(50);
        CHECK("STOP call returns immediately (non-blocking)", callMs<100);
        CHECK("cooperative interpreter exits via stop flag, no kill", w.m_process->state()==QProcess::NotRunning && !w.m_w3->m_logEdit->toPlainText().contains("Forcing") && el.elapsed()<2000); }
      { MainWindow w; w.show(); w.addTab(script); w.m_interpreterPath=hang; w.onStartStop(); QTest::qWait(300); int ticks=0; QTimer t; QObject::connect(&t,&QTimer::timeout,[&]{++ticks;}); t.start(20);
        w.onStartStop(); QTest::qWait(1000); bool stillRunning=w.m_process->state()==QProcess::Running; int ticksWhileWaiting=ticks;
        QElapsedTimer el; el.start(); w.onStartStop(); /* second press = escalate */ for(int i=0;i<60 && w.m_process->state()!=QProcess::NotRunning;++i) QTest::qWait(50);
        CHECK("GUI keeps ticking while waiting for a stubborn interpreter", stillRunning && ticksWhileWaiting>=30);
        CHECK("second STOP press kills immediately", w.m_process->state()==QProcess::NotRunning && el.elapsed()<1500 && w.m_w3->m_logEdit->toPlainText().contains("Forcing")); }
    }
    { MainWindow w; w.addTab(script); w.addTab(script); QWidget*first=w.m_tabWidget->widget(0); w.m_runningViewer=qobject_cast<ScriptViewer*>(first); w.m_tabWidget->tabBar()->moveTab(0,1);
      CHECK("running viewer survives tab move", w.runningViewer()==first && w.runningTabIndex()==1); }
    { MainWindow w; w.addTab(script); QString comm=dir+"/c.txt"; write(comm,"x\n"); QString mainS=dir+"/m.txt"; write(mainS,"UART:1.SCRIPT c.txt &\n"); ScriptViewer*v=w.addTab(mainS); w.m_runningViewer=v;
      w.dispatchLine("GUI:THREAD_START:1"); w.dispatchLine("GUI:THREAD_START:1"); w.dispatchLine("GUI:THREAD_DONE:1"); bool still=w.isThreadedCommFile(comm); w.dispatchLine("GUI:THREAD_DONE:1");
      CHECK("threaded comm script is refcounted", still && !w.isThreadedCommFile(comm)); }
    { // session: untitled tab before the active file
      MainWindow w; w.m_tabWidget->setCurrentIndex(0); ScriptViewer*a=w.addTab(); (void)a; ScriptViewer*b=w.addTab(script); (void)b; w.m_tabWidget->setCurrentWidget(b); w.saveSession(); QSettings s;
      QStringList paths=s.value("session/tabPaths").toStringList(); int act=s.value("session/activeTab").toInt(); CHECK("saved active tab index matches saved path list", act>=0 && act<paths.size() && paths.at(act)==script); }
  }
  std::cout<<(fails?"FAILURES: ":"ALL PASSED ")<<fails<<std::endl; return fails; }
