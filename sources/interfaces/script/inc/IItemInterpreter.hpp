#ifndef IITEMINTERPRETER_HPP
#define IITEMINTERPRETER_HPP

#include "IDataTypes.hpp"

template <typename TItem = void, typename TDriver = void>
class IItemInterpreter
{
    public:

        using SendFunc = PFSEND<TDriver>;
        using RecvFunc = PFRECV<TDriver>;

        virtual ~IItemInterpreter() = default;
        virtual bool interpretItem(TItem& item) = 0;

    protected:

        explicit IItemInterpreter(SendFunc pfsend = SendFunc{}, RecvFunc pfrecv = RecvFunc{}, size_t szMaxRecvSize = 0) {}

};

#endif // IITEMINTERPRETER_HPP

