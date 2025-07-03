#ifndef PLUGINITEMVALIDATOR_HPP
#define PLUGINITEMVALIDATOR_HPP

#include "IItemValidator.hpp"

#include <iostream>
#include <regex>
#include <string>


/* Tokens type */
enum class Token {
    COMMAND,
    MACRO,
    INVALID
};

/////////////////////////////////////////////////////////////////////////////////
//                            CLASS IMPLEMENTATION                             //
/////////////////////////////////////////////////////////////////////////////////

class PluginItemValidator : public IItemValidator<Token>
{
    public:

        bool validateItem ( const std::string& item, Token& token ) noexcept override
        {
            static const std::regex pattern(R"(^[A-Za-z][A-Za-z0-9_]*(\s+\w+)*(\s*\|\s*.+\S)?$)");
            bool bRetVal = std::regex_match(item, pattern);

            token = bRetVal ? Token::COMMAND : Token::INVALID;
            return bRetVal;
        }
}


#endif // PLUGINITEMVALIDATOR_HPP