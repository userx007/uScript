#ifndef ISCRIPTVALIDATOR_HPP
#define ISCRIPTVALIDATOR_HPP

#include <string>
#include <vector>

// forward declaration
struct ScriptEntries;

class IScriptValidator
{
public:

    IScriptValidator() = default;
    virtual ~IScriptValidator() = default;

    virtual bool validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntries& sScriptEntries) = 0;

};

#endif // ISCRIPTVALIDATOR_HPP
