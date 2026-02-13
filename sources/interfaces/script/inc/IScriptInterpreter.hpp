#ifndef ISCRIPTINTERPRETER_HPP
#define ISCRIPTINTERPRETER_HPP

/**
 * @brief Minimal abstract interface for script interpretation
 * 
 * @tparam TScriptEntries Type representing script entries/commands
 */
template <typename TScriptEntries = void>
class IScriptInterpreter
{
    public:

        virtual ~IScriptInterpreter() = default;

        /**
         * @brief Interpret and execute a script
         * @param sScriptEntries Script entries to interpret
         * @return true if interpretation succeeded, false otherwise
         */
        virtual bool interpretScript(TScriptEntries& sScriptEntries) = 0;

    protected:

        IScriptInterpreter() = default;
};

#endif // ISCRIPTINTERPRETER_HPP
