#ifndef ISCRIPTINTERPRETER_COMM_SHELL_HPP
#define ISCRIPTINTERPRETER_COMM_SHELL_HPP

#include "IScriptInterpreterComm.hpp"

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
template <typename TScriptEntries = void, typename TDriver = void>
class IScriptInterpreterCommShell : public IScriptInterpreterComm<TScriptEntries, TDriver>
{
    public:

        using SendFunc = ICommDriver::SendFunc<TDriver>;  
        using RecvFunc = ICommDriver::RecvFunc<TDriver>;          

        virtual ~IScriptInterpreterCommShell() = default;

        //--------------------------------------------------------------------
        // Shell/Interactive interfaces for script element handling
        //--------------------------------------------------------------------

        /**
         * @brief List available items/scripts
         * @return true if listing succeeded, false otherwise
         */
        virtual bool listItems() { return true; }

        /**
         * @brief List available commands
         * @return true if listing succeeded, false otherwise
         */
        virtual bool listCommands() { return true; }

        /**
         * @brief Load a plugin by name
         * @param strPluginName Name of the plugin to load
         * @return true if plugin loaded successfully, false otherwise
         */
        virtual bool loadPlugin(const std::string& strPluginName) { return true; }

        /**
         * @brief Execute a command string
         * @param strCommand Command string to execute
         * @return true if command executed successfully, false otherwise
         */
        virtual bool executeCmd(const std::string& strCommand) { return true; }

    protected:

        /**
         * @brief Construct from INI configuration file
         * @param strIniPathName Path to INI configuration file
         */
        explicit IScriptInterpreterCommShell(const std::string& strIniPathName)
            : IScriptInterpreterComm<TScriptEntries, TDriver>(strIniPathName) {}

        /**
         * @brief Construct with explicit communication functions
         * @param pfsend Send/write function callback
         * @param pfrecv Receive/read function callback
         * @param szDelay Delay between operations (implementation-specific units)
         * @param szMaxRecvSize Maximum receive buffer size
         */
        explicit IScriptInterpreterCommShell(
            SendFunc pfsend = SendFunc{}, 
            RecvFunc pfrecv = RecvFunc{}, 
            size_t szDelay = 0, 
            size_t szMaxRecvSize = 0)
            : IScriptInterpreterComm<TScriptEntries, TDriver>(pfsend, pfrecv, szDelay, szMaxRecvSize) {}
};

#endif // ISCRIPTINTERPRETER_COMM_SHELL_HPP
