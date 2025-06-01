#ifndef IITEMVALIDATOR_HPP
#define IITEMVALIDATOR_HPP

#include <string>

template <typename T>
class IItemValidator
{
public:

    virtual bool validateItem(const std::string& item, T& type) = 0;
    virtual ~IItemValidator() = default;
};

#endif // IITEMVALIDATOR_HPP
