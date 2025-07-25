#ifndef ISCRIPTITEMINTERPRETER_HPP
#define ISCRIPTITEMINTERPRETER_HPP

#include "IDataTypes.hpp"

template <typename TItem = void, typename TDriver = void>
class IScriptItemInterpreter
{
    public:

        using SendFunc = PFSEND<TDriver>;
        using RecvFunc = PFRECV<TDriver>;

        virtual ~IScriptItemInterpreter() = default;
        virtual bool interpretItem(const TItem& item) = 0;

    protected:

        explicit IScriptItemInterpreter(SendFunc pfsend = SendFunc{}, RecvFunc pfrecv = RecvFunc{}, size_t szMaxRecvSize = 0) {}

};

#endif // ISCRIPTITEMINTERPRETER_HPP

