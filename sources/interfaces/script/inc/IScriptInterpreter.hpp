#ifndef ISCRIPTINTERPRETER_HPP
#define ISCRIPTINTERPRETER_HPP

#include <string>
#include <span>


using PFSEND = bool(*)(std::span<const uint8_t>);
using PFRECV = bool(*)(std::span<uint8_t>);

template <typename TScriptEntries = void>
class IScriptInterpreter
{
    public:

        IScriptInterpreter() = default;
        virtual ~IScriptInterpreter() = default;

        virtual bool interpretScript(TScriptEntries& sScriptEntries, PFSEND pfsend = nullptr, PFRECV = nullptr) = 0;

        // additional interfaces used to handle script elements from the shell
        virtual bool listItems() {
            return true;
        }

        virtual bool listCommands() {
            return true;
        }

        virtual bool loadPlugin(const std::string& strPluginName) {
            return true;
        }

        virtual bool executeCmd(const std::string& strCommand) {
            return true;
        }

};

#endif // ISCRIPTINTERPRETER_HPP
