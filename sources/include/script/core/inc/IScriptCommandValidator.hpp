#ifndef ISCRIPTITEMVALIDATOR_HPP
#define ISCRIPTITEMVALIDATOR_HPP

#include <string>

template <typename TCommand>
class IScriptCommandValidator
{
    public:
        IScriptCommandValidator()                                                                    = default;
        virtual ~IScriptCommandValidator()                                                           = default;

        /** Validates one command line, filling @p type; returns false (and logs) if it is invalid. */
        virtual bool validateCommand(int iLineNumber, const std::string &strCommand, TCommand &type) = 0;
};

#endif // ISCRIPTITEMVALIDATOR_HPP
