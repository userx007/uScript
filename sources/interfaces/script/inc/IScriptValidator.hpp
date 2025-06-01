#ifndef ISCRIPTVALIDATOR_HPP
#define ISCRIPTVALIDATOR_HPP

#include <string>
#include <vector>

// forward declaration
struct ScriptEntries;

class IScriptValidator
{
public:

    virtual bool validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntries& sScriptEntries) = 0;

    virtual ~IScriptValidator() = default;

};

#endif // ISCRIPTVALIDATOR_HPP
