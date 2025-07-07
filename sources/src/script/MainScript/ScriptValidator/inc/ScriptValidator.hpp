#ifndef SCRIPTVALIDATOR_HPP
#define SCRIPTVALIDATOR_HPP

#include "IScriptValidator.hpp"
#include "IItemValidator.hpp"
#include "ScriptItemValidator.hpp"
#include "ScriptDataTypes.hpp"
#include "IPluginDataTypes.hpp"


#include <string>
#include <vector>
#include <variant>
#include <unordered_map>
#include <memory>



/////////////////////////////////////////////////////////////////////////////////
//                            PUBLIC INTERFACES                                //
/////////////////////////////////////////////////////////////////////////////////


class ScriptValidator : public IScriptValidator<ScriptEntriesType>
{
    public:

        explicit ScriptValidator(std::shared_ptr<IItemValidator<Token>> shpItemValidator)
            : m_shpItemValidator(std::move(shpItemValidator))
        {}

        bool validateScript(std::vector<std::string>& vstrScriptLines, ScriptEntriesType& sScriptEntries) override;

    private:

        bool m_validateScriptItems(std::vector<std::string>& vstrScriptLines) noexcept;
        bool m_HandleLoadPlugin ( const std::string& command ) noexcept;
        bool m_HandleConstantMacro ( const std::string& command ) noexcept;
        bool m_HandleVariableMacro ( const std::string& command ) noexcept;
        bool m_HandleCommand ( const std::string& command ) noexcept;
        bool m_HandleCondition ( const std::string& command ) noexcept;
        bool m_HandleLabel ( const std::string& command ) noexcept;

        bool m_preprocessScriptItems( const std::string& command, const Token token ) noexcept;
        void m_replaceConstantMacros ( std::string& str ) noexcept;
        bool m_validateConditions() noexcept;
        bool m_validatePlugins () noexcept;

        bool m_ListItems () noexcept;

        std::shared_ptr<IItemValidator<Token>> m_shpItemValidator;
        ScriptEntriesType *m_sScriptEntries = nullptr;
};

#endif // SCRIPTVALIDATOR_HPP
