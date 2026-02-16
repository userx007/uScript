#ifndef ISCRIPTVALIDATOR_HPP
#define ISCRIPTVALIDATOR_HPP

#include <string>
#include <vector>


template <typename TScriptEntries>
class IScriptValidator
{
public:

    IScriptValidator() = default;
    virtual ~IScriptValidator() = default;

    virtual bool validateScript(std::vector<std::string>& vstrScriptLines, TScriptEntries& sScriptEntries) = 0;

};

#endif // ISCRIPTVALIDATOR_HPP
