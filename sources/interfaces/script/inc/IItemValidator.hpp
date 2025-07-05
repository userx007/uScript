#ifndef IITEMVALIDATOR_HPP
#define IITEMVALIDATOR_HPP

#include <string>

template <typename T>
class IItemValidator
{
    public:

        IItemValidator() = default;
        virtual ~IItemValidator() = default;

        virtual bool validateItem(const std::string& item, T& type) = 0;

};

#endif // IITEMVALIDATOR_HPP
