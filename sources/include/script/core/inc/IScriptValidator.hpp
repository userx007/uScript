#ifndef ISCRIPTVALIDATOR_HPP
#define ISCRIPTVALIDATOR_HPP

#include "uScriptDataTypes.hpp"

#include <vector>

template <typename TScriptEntries>
class IScriptValidator
{
    public:
        IScriptValidator()                                                                                 = default;
        virtual ~IScriptValidator()                                                                        = default;

        /** Validates all raw script lines and fills @p sScriptEntries; returns false if any line is invalid. */
        virtual bool validateScript(std::vector<ScriptRawLine> &vRawLines, TScriptEntries &sScriptEntries) = 0;
};

#endif // ISCRIPTVALIDATOR_HPP
