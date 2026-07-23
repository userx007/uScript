#ifndef U_PLUGIN_SETTINGS_HPP
#define U_PLUGIN_SETTINGS_HPP

#include "uNumeric.hpp"
#include "uBoolEvaluator.hpp"

#include <string>
#include <variant>
#include <vector>
#include <functional>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <utility>


/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "PLUGSETTINGS|"
#define LOG_HDR    LOG_STRING(LT_HDR)


///////////////////////////////////////////////////////////////////
//                 PUBLIC INTERFACES DEFINITIONS                 //
///////////////////////////////////////////////////////////////////

/**
  * \brief Generic key -> class-member binder, meant to replace the repeated
  *        per-key "if (mapSettings.count(KEY)) { convert; assign; }" blocks
  *        found in every plugin's m_LocalSetParams().
  *
  * \note  Two kinds of bindings are supported:
  *          - a direct pointer to a class member (std::string, bool, uint8_t,
  *            uint16_t, uint32_t, size_t) -- the raw ini string is converted
  *            to the member's type and assigned directly.
  *          - a setter function (bool(const std::string&)) -- for members
  *            that go through validated setters (e.g. TCPIPPlugin::setTcpPort(),
  *            which range-checks the port before assigning). Use this form
  *            whenever the existing setter does more than a bare conversion.
  *
  * \note  To support an additional plain member type, add it to the Target
  *        variant and add a matching Convert() overload below.
  *
  * Usage (direct members, e.g. UARTPlugin):
  * \code
  *     PluginSettingsBinder sSettings;
  *     sSettings.Bind(UART_PORT, m_strUartPort);
  *     sSettings.Bind(BAUDRATE,  m_u32UartBaudrate);
  *     return sSettings.Apply(psSetParams->mapSettings);
  * \endcode
  *
  * Usage (validated setters, e.g. TCPIPPlugin):
  * \code
  *     PluginSettingsBinder sSettings;
  *     sSettings.Bind(TCP_PORT, [this](const std::string& v){ return setTcpPort(v); });
  *     return sSettings.Apply(psSetParams->mapSettings);
  * \endcode
*/
class PluginSettingsBinder
{
    public:

        using Setter = std::function<bool(const std::string&)>;

        // NOTE: on platforms where size_t and uint32_t are the same type (e.g. 32-bit
        // Windows/Linux builds) this variant would have two identical alternatives,
        // which fails to compile. This codebase targets 64-bit (std::span, C++20), where
        // size_t is 64-bit and distinct from uint32_t, so this is not an issue in practice.
        // If a 32-bit target is ever needed, drop the size_t* alternative and use uint32_t
        // for buffer/size members instead (as most plugins already do).
        using Target = std::variant<std::string*, bool*, uint8_t*, uint16_t*, uint32_t*, size_t*, Setter>;

        /**
          * \brief bind an ini/PluginDataSet key directly to the class member it should initialize
          * \note one overload per supported member type keeps call sites type-safe --
          *       there is no way to accidentally bind a key to an unsupported type
        */
        void Bind (const std::string& strKey, std::string& member) { m_vectEntries.push_back({strKey, &member}); }
        void Bind (const std::string& strKey, bool& member)        { m_vectEntries.push_back({strKey, &member}); }
        void Bind (const std::string& strKey, uint8_t& member)     { m_vectEntries.push_back({strKey, &member}); }
        void Bind (const std::string& strKey, uint16_t& member)    { m_vectEntries.push_back({strKey, &member}); }
        void Bind (const std::string& strKey, uint32_t& member)    { m_vectEntries.push_back({strKey, &member}); }
        void Bind (const std::string& strKey, size_t& member)      { m_vectEntries.push_back({strKey, &member}); }

        /**
          * \brief bind an ini/PluginDataSet key to an existing validated setter
          *        (e.g. one that range-checks the converted value before assigning)
        */
        void Bind (const std::string& strKey, Setter fnSetter)
        {
            m_vectEntries.push_back({strKey, std::move(fnSetter)});
        }

        /**
          * \brief apply values from mapSettings (as populated from the ini file) to every bound target
          * \param[in] mapSettings key/value pairs, values always as string
          * \param[in] fnOnApplied optional callback invoked with (key, rawValue) for every key found
          *            and successfully applied -- lets a plugin keep its own LOG_PRINT(LOG_VERBOSE, ...) trail
          * \param[in] bStopOnFirstError two behaviours are found across the existing plugins:
          *              - true  (default): stop at the first *present* key that fails conversion/validation,
          *                leaving any keys not yet processed untouched. Mirrors the
          *                "do { ...; break; ... } while(false)" early-exit pattern used by most plugins.
          *              - false: process every bound key regardless of earlier failures (each target is
          *                still only written on success), and return whether *all* of them succeeded.
          *                Mirrors the "ok &= convert(...)" accumulation pattern used by a few plugins
          *                (e.g. FT2232/FT245/FT232H/FT4232/CH347/CP2112/Hydrabus).
          * \return false if a *present* key failed conversion/validation (subject to bStopOnFirstError
          *         above); a key simply absent from mapSettings is never an error, the target just keeps
          *         its current/default value
        */
        bool Apply (const std::unordered_map<std::string, std::string>& mapSettings,
                    const std::function<void(const std::string& strKey, const std::string& strRawValue)>& fnOnApplied = nullptr,
                    bool bStopOnFirstError = true) const
        {
            bool bRetVal = true;

            for (const auto& sEntry : m_vectEntries)
            {
                auto it = mapSettings.find(sEntry.strKey);
                if (mapSettings.end() == it)
                {
                    continue; // key absent -> leave target at its current/default value
                }

                const bool bOk = std::visit(
                    [&it](auto&& target) -> bool
                    {
                        using T = std::decay_t<decltype(target)>;
                        if constexpr (std::is_same_v<T, Setter>)
                        {
                            return target(it->second);
                        }
                        else
                        {
                            return PluginSettingsBinder::Convert(it->second, *target);
                        }
                    },
                    sEntry.target);

                if (false == bOk)
                {
                    bRetVal = false;
                    if (true == bStopOnFirstError)
                    {
                        break;
                    }
                    continue; // accumulate-mode: keep processing the remaining keys
                }

                if (fnOnApplied)
                {
                    fnOnApplied(sEntry.strKey, it->second);
                }
            }

            return bRetVal;

        } /* Apply() */

    private:

        struct Entry
        {
            std::string strKey;
            Target      target;
        };

        // vector, not map: preserves Bind() call order so Apply()'s
        // break-on-first-failure behaviour matches the original per-plugin ordering
        std::vector<Entry> m_vectEntries;

        static bool Convert (const std::string& strVal, std::string& out)
        {
            out = strVal;
            return true;
        }

        static bool Convert (const std::string& strVal, bool& out)
        {
            BoolExprEvaluator sEvaluator;
            return sEvaluator.evaluate(strVal, out);
        }

        static bool Convert (const std::string& strVal, uint8_t& out)  { return numeric::str2uint8 (strVal, out); }
        static bool Convert (const std::string& strVal, uint16_t& out) { return numeric::str2uint16(strVal, out); }
        static bool Convert (const std::string& strVal, uint32_t& out) { return numeric::str2uint32(strVal, out); }
        static bool Convert (const std::string& strVal, size_t& out)   { return numeric::str2sizet (strVal, out); }
};

#endif /* U_PLUGIN_SETTINGS_HPP */
