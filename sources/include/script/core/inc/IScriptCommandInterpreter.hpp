#ifndef I_SCRIPT_COMMAND_INTERPRETER_HPP
#define I_SCRIPT_COMMAND_INTERPRETER_HPP

/**
 * @brief Minimal abstract interface for script com interpretation
 * 
 * @tparam TCommand Type representing a single script command
 */
template <typename TCommand = void>
class IScriptCommandInterpreter
{
    public:

        virtual ~IScriptCommandInterpreter() = default;

        /**
         * @brief Interpret and execute a single script command
         * @param command Script command to interpret
         * @return true if interpretation succeeded, false otherwise
         */
        virtual bool interpretItem(const TCommand& command) = 0;

    protected:

        IScriptCommandInterpreter() = default;
};

#endif // I_SCRIPT_COMMAND_INTERPRETER_HPP
