#ifndef ISCRIPTITEMVALIDATOR_HPP
#define ISCRIPTITEMVALIDATOR_HPP

#include <string>

template <typename TItem>
class IScriptItemValidator
{
    public:

        IScriptItemValidator() = default;
        virtual ~IScriptItemValidator() = default;

        virtual bool validateItem(const std::string& item, TItem& type) = 0;

};

#endif // ISCRIPTITEMVALIDATOR_HPP
