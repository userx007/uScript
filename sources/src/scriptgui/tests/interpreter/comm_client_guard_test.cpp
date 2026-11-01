#include "uCommScriptClient.hpp"
#include <iostream>
template class CommScriptClient<ICommDriver>;
int main(){
  std::shared_ptr<const ICommDriver> d;
  for (const char* n : {"uart.txt","uart.main","uart.comm","UART.COMM"}) {
    CommScriptClient<ICommDriver> c(n, d, "UART");
    std::cout<<n<<" -> execute(false)="<<c.execute(false)<<std::endl;
  }
}
