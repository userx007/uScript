#ifndef ISCRIPTINTERPRETER_HPP
#define ISCRIPTINTERPRETER_HPP

#include "IDataTypes.hpp"

#include <functional>
#include <string>
#include <span>


template <typename TScriptEntries = void, typename TDriver = void>
class IScriptInterpreter
{
    public:

        using SendFunc = PFSEND<TDriver>;
        using RecvFunc = PFRECV<TDriver>;

        virtual ~IScriptInterpreter() = default;
        virtual bool interpretScript(TScriptEntries& sScriptEntries) = 0;

    protected:

        explicit IScriptInterpreter(const std::string& strIniPathName) {}
        explicit IScriptInterpreter(SendFunc pfsend = SendFunc{}, RecvFunc pfrecv = RecvFunc{}, size_t szDelay = 0, size_t szMaxRecvSize = 0) {}

    public:
        //--------------------------------------------------------------------
        // additional interfaces used to handle script elements from the shell
        //--------------------------------------------------------------------

        virtual bool listItems() { return true; }
        virtual bool listCommands() { return true;}
        virtual bool loadPlugin(const std::string& strPluginName) { return true; }
        virtual bool executeCmd(const std::string& strCommand) { return true; }

};

#endif // ISCRIPTINTERPRETER_HPP
