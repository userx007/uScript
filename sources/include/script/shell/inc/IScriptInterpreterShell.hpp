#ifndef ISCRIPTINTERPRETER_SHELL_HPP
#define ISCRIPTINTERPRETER_SHELL_HPP

#include "uIScriptInterpreter.hpp"

#include <string>

/**
 * @brief Script interpreter with interactive shell capabilities
 * 
 * Extends the communication-enabled interpreter with shell/command-line
 * interfaces for interactive script management and execution.
 * 
 * @tparam TScriptEntries Type representing script entries/commands
 * @tparam TDriver Type of communication driver
 */
template <typename TScriptEntries>
class IScriptInterpreterShell : public IScriptInterpreter<TScriptEntries>
{
    public:

        virtual ~IScriptInterpreterShell() = default;

        //--------------------------------------------------------------------
        // Shell/Interactive interfaces for script element handling
        //--------------------------------------------------------------------

        /**
         * @brief List available items/scripts
         * @return true if listing succeeded, false otherwise
         */
        virtual bool listItems();

        /**
         * @brief List available commands
         * @return true if listing succeeded, false otherwise
         */
        virtual bool listCommands();

        /**
         * @brief Load a plugin by name
         * @param strPluginName Name of the plugin to load
         * @return true if plugin loaded successfully, false otherwise
         */
        virtual bool loadPlugin(const std::string& strPluginName);

        /**
         * @brief Execute a command string
         * @param strCommand Command string to execute
         * @return true if command executed successfully, false otherwise
         */
        virtual bool executeCmd(const std::string& strCommand);
};

#endif // ISCRIPTINTERPRETER_SHELL_HPP
