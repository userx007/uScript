#ifndef ISCRIPTREADER_HPP
#define ISCRIPTREADER_HPP

#include <vector>
#include <string>

class IScriptReader
{
public:

    virtual bool readScript(std::vector<std::string>& vstrScriptLines) = 0;

    virtual ~IScriptReader() = default;

};

#endif // ISCRIPTREADER_HPP
