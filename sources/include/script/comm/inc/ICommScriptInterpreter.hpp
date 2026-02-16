#ifndef I_COMM_SCRIPT_INTERPRETER_HPP
#define I_COMM_SCRIPT_INTERPRETER_HPP

#include "uIScriptInterpreter.hpp"
#include "ICommDriver.hpp"

#include <string>
#include <cstddef>

/**
 * @brief Script interpreter with communication capabilities
 * 
 * Extends the base interpreter with send/receive function types for
 * communication with external devices or systems.
 * Introduces TDriver template parameter at this level.
 * 
 * @tparam TScriptEntries Type representing script entries/commands
 * @tparam TDriver Type of communication driver
 */
template <typename TScriptEntries, typename TDriver>
class ICommScriptInterpreter : public IScriptInterpreter<TScriptEntries>
{
    public:

        using SendFunc = SendFunction<TDriver>;
        using RecvFunc = RecvFunction<TDriver>;        

        virtual ~ICommScriptInterpreter() = default;

    protected:

        /**
         * @brief Construct from INI configuration file
         * @param strIniPathName Path to INI configuration file
         */
        explicit ICommScriptInterpreter(const std::string& strIniPathName) {}

        /**
         * @brief Construct with explicit communication functions
         * @param pfsend Send/write function callback
         * @param pfrecv Receive/read function callback
         * @param szDelay Delay between operations (implementation-specific units)
         * @param szMaxRecvSize Maximum receive buffer size
         */
        explicit ICommScriptInterpreter(
            SendFunc pfsend = SendFunc{}, 
            RecvFunc pfrecv = RecvFunc{}, 
            size_t szDelay = 0, 
            size_t szMaxRecvSize = 0) {}
};

#endif // I_COMM_SCRIPT_INTERPRETER_HPP
