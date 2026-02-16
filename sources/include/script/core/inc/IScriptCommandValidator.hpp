#ifndef ISCRIPTITEMVALIDATOR_HPP
#define ISCRIPTITEMVALIDATOR_HPP

#include <string>

template <typename TCommand>
class IScriptCommandValidator
{
    public:

        IScriptCommandValidator() = default;
        virtual ~IScriptCommandValidator() = default;

        virtual bool validateCommand(const std::string& command, TCommand& type) = 0;

};

#endif // ISCRIPTITEMVALIDATOR_HPP
