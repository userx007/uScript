#include "uScriptClient.hpp"
#include <iostream>
int main(){
  { IniCfgLoader l; ScriptClient c("script.txt", std::move(l)); std::cout<<"txt -> execute(false)="<<c.execute(false)<<std::endl; }
  { IniCfgLoader l; ScriptClient c("script.comm", std::move(l)); std::cout<<"comm -> execute(false)="<<c.execute(false)<<std::endl; }
  return 0; }
