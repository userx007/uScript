#ifndef ISCRIPTREADER_HPP
#define ISCRIPTREADER_HPP

#include "uScriptDataTypes.hpp"

#include <vector>

class IScriptReader
{
public:

    virtual bool readScript(std::vector<ScriptRawLine>& vRawLines) = 0;

    virtual ~IScriptReader() = default;

};

#endif // ISCRIPTREADER_HPP
