#ifndef ISCRIPTINTERPRETER_COMM_HPP
#define ISCRIPTINTERPRETER_COMM_HPP

#include "IScriptInterpreter.hpp"
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
class IScriptInterpreterComm : public IScriptInterpreter<TScriptEntries>
{
    public:

        using SendFunc = ICommDriver::SendFunc<TDriver>;  
        using RecvFunc = ICommDriver::RecvFunc<TDriver>;          

        virtual ~IScriptInterpreterComm() = default;

    protected:

        /**
         * @brief Construct from INI configuration file
         * @param strIniPathName Path to INI configuration file
         */
        explicit IScriptInterpreterComm(const std::string& strIniPathName) {}

        /**
         * @brief Construct with explicit communication functions
         * @param pfsend Send/write function callback
         * @param pfrecv Receive/read function callback
         * @param szDelay Delay between operations (implementation-specific units)
         * @param szMaxRecvSize Maximum receive buffer size
         */
        explicit IScriptInterpreterComm(
            SendFunc pfsend = SendFunc{}, 
            RecvFunc pfrecv = RecvFunc{}, 
            size_t szDelay = 0, 
            size_t szMaxRecvSize = 0) {}
};

#endif // ISCRIPTINTERPRETER_COMM_HPP
