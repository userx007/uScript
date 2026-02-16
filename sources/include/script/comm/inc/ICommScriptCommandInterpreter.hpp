#ifndef I_COMM_SCRIPT_ITEM_INTERPRETER_HPP
#define I_COMM_SCRIPT_ITEM_INTERPRETER_HPP

#include "IScriptCommandInterpreter.hpp"
#include "ICommDriver.hpp"

#include <cstddef>

/**
 * @brief Script command interpreter with communication capabilities
 * 
 * Extends the base interpreter with send/receive function types for
 * communication with external devices or systems.
 * 
 * @tparam TCommand Type representing a single script command
 * @tparam TDriver Type of communication driver
 */
template <typename TCommand = void, typename TDriver = void>
class ICommScriptCommandInterpreter : public IScriptCommandInterpreter<TCommand>
{
    public:

        using SendFunc = SendFunction<TDriver>;
        using RecvFunc = RecvFunction<TDriver>;     
        
        virtual ~ICommScriptCommandInterpreter() = default;

    protected:

        /**
         * @brief Construct with explicit communication functions
         * @param pfsend Send/write function callback
         * @param pfrecv Receive/read function callback
         * @param szMaxRecvSize Maximum receive buffer size
         */
        explicit ICommScriptCommandInterpreter(
            SendFunc pfsend = SendFunc{}, 
            RecvFunc pfrecv = RecvFunc{}, 
            size_t szMaxRecvSize = 0) {}
};

#endif // I_COMM_SCRIPT_ITEM_INTERPRETER_HPP
