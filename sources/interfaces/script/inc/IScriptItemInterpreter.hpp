#ifndef ISCRIPTITEMINTERPRETER_HPP
#define ISCRIPTITEMINTERPRETER_HPP

/**
 * @brief Minimal abstract interface for script item interpretation
 * 
 * @tparam TItem Type representing a single script item/command
 */
template <typename TItem = void>
class IScriptItemInterpreter
{
    public:

        virtual ~IScriptItemInterpreter() = default;

        /**
         * @brief Interpret and execute a single script item
         * @param item Script item to interpret
         * @return true if interpretation succeeded, false otherwise
         */
        virtual bool interpretItem(const TItem& item) = 0;

    protected:

        IScriptItemInterpreter() = default;
};

#endif // ISCRIPTITEMINTERPRETER_HPP
