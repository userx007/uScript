#include "ScriptClient.hpp"

int main()
{
    ScriptClient client("script.txt");

    bool bRetVal = client.execute();

    return (true == bRetVal) ? 0 : 1;

}
