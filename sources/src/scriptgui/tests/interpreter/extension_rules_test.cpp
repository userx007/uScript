#include "uScriptReader.hpp"
#include "uSharedScriptExt.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
static int fails=0;
#define CHECK(n,c) do{bool ok=(c); std::cout<<(ok?"PASS ":"FAIL ")<<n<<std::endl; if(!ok)++fails;}while(0)
static void w(const std::string&p,const std::string&t){std::ofstream(p)<<t;}
int main(){
  using namespace scriptext;
  CHECK("main ext", isMainScript("a/b/script.main") && isMainScript("X.MAIN") && isMainScript("C:\\d\\u.Main"));
  CHECK("main rejects others", !isMainScript("a.txt") && !isMainScript("a.comm") && !isMainScript("a.main.txt") && !isMainScript(".main") && !isMainScript("dir.main/file") && !isMainScript("main"));
  CHECK("comm ext", isCommScript("uart.comm") && !isCommScript("uart.txt") && !isCommScript("uart.main"));
  CHECK("editor files: Windows rule", isMainEditorFileFor("a.main",true)&&isMainEditorFileFor("c.ini",true)&&isMainEditorFileFor("s.bat",true)&&isMainEditorFileFor("s.CMD",true)&&isMainEditorFileFor("s.ps1",true)&&!isMainEditorFileFor("s.sh",true)&&!isMainEditorFileFor("a.comm",true)&&!isMainEditorFileFor("a.txt",true));
  CHECK("editor files: Linux rule", isMainEditorFileFor("a.main",false)&&isMainEditorFileFor("c.ini",false)&&isMainEditorFileFor("s.sh",false)&&!isMainEditorFileFor("s.bat",false)&&!isMainEditorFileFor("s.cmd",false)&&!isMainEditorFileFor("s.ps1",false)&&!isMainEditorFileFor("a.comm",false)&&!isMainEditorFileFor("a.txt",false));
  CHECK("host rule follows the platform", isMainEditorFile("s.sh")==!kHostIsWindows && isMainEditorFile("s.cmd")==kHostIsWindows);
  std::filesystem::path d=std::filesystem::temp_directory_path()/"it_inc"; std::filesystem::remove_all(d); std::filesystem::create_directories(d);
  w((d/"lib.main").string(),"A := 1\n"); w((d/"lib.txt").string(),"B := 2\n"); w((d/"lib.comm").string(),"C := 3\n");
  { w((d/"ok.main").string(),"INCLUDE \"lib.main\"\nX := 1\n"); std::vector<ScriptRawLine> v; ScriptReader r((d/"ok.main").string()); CHECK("INCLUDE .main works", r.readScript(v) && v.size()==2); }
  { w((d/"bad1.main").string(),"INCLUDE \"lib.txt\"\n"); std::vector<ScriptRawLine> v; ScriptReader r((d/"bad1.main").string()); CHECK("INCLUDE .txt refused", !r.readScript(v)); }
  { w((d/"bad2.main").string(),"INCLUDE \"lib.comm\"\n"); std::vector<ScriptRawLine> v; ScriptReader r((d/"bad2.main").string()); CHECK("INCLUDE .comm refused", !r.readScript(v)); }
  { w((d/"bad3.main").string(),"INCLUDE \"lib\"\n"); std::vector<ScriptRawLine> v; ScriptReader r((d/"bad3.main").string()); CHECK("INCLUDE without extension refused", !r.readScript(v)); }
  std::cout<<(fails?"FAILURES ":"ALL PASSED ")<<fails<<std::endl; return fails; }
