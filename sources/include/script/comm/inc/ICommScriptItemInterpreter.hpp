#ifndef I_COMM_SCRIPT_ITEM_INTERPRETER_HPP
#define I_COMM_SCRIPT_ITEM_INTERPRETER_HPP

#include "IScriptItemInterpreter.hpp"
#include "ICommDriver.hpp"

#include <cstddef>

/**
 * @brief Script item interpreter with communication capabilities
 * 
 * Extends the base interpreter with send/receive function types for
 * communication with external devices or systems.
 * 
 * @tparam TItem Type representing a single script item/command
 * @tparam TDriver Type of communication driver
 */
template <typename TItem = void, typename TDriver = void>
class ICommScriptItemInterpreter : public IScriptItemInterpreter<TItem>
{
    public:

        using SendFunc = SendFunction<TDriver>;
        using RecvFunc = RecvFunction<TDriver>;     
        
        virtual ~ICommScriptItemInterpreter() = default;

    protected:

        /**
         * @brief Construct with explicit communication functions
         * @param pfsend Send/write function callback
         * @param pfrecv Receive/read function callback
         * @param szMaxRecvSize Maximum receive buffer size
         */
        explicit ICommScriptItemInterpreter(
            SendFunc pfsend = SendFunc{}, 
            RecvFunc pfrecv = RecvFunc{}, 
            size_t szMaxRecvSize = 0) {}
};

#endif // I_COMM_SCRIPT_ITEM_INTERPRETER_HPP
