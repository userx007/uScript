#ifndef SCRIPTREADER_HPP
#define SCRIPTREADER_HPP

#include "IScriptReader.hpp"
#include <vector>
#include <string>

class ScriptReader : public IScriptReader
{
public:

    ScriptReader(const std::string& strScriptPathName)
        : m_strScriptPathName(strScriptPathName)
    {}

    virtual bool readScript(std::vector<std::string>& vstrScriptLines) override;

private:

    std::string m_strScriptPathName;

};

#endif // SCRIPTREADER_HPP
