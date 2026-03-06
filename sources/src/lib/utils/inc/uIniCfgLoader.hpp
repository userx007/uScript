#ifndef U_INI_CONFIG_LOADER_HPP
#define U_INI_CONFIG_LOADER_HPP

#include "uSharedConfig.hpp"
#include "uBoolExprEvaluator.hpp"
#include "uIniParserEx.hpp"
#include "uNumeric.hpp"
#include "uLogger.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif
#undef  LOG_HDR
#define LOG_HDR     LOG_STRING("INICFG_LOAD|");


/*-------------------------------------------------------------------------------
    IniCfgLoader — header-only helper that wraps IniParserEx and BoolExprEvaluator
    to provide a simple key/value query interface over a single active section.

    Typical usage:
        IniCfgLoader loader;
        if (!loader.load("/path/to/config.ini"))       { ... }
        if (!loader.loadSection("SectionName"))        { ... }
        loader.getBoolFromIni("some_flag",   myBool);
        loader.getNumFromIni ("some_number", mySize);
-------------------------------------------------------------------------------*/

class IniCfgLoader
{
public:

    // -------------------------------------------------------------------------
    //  load()
    //  Parses the .ini file at the given path.
    //  Must be called successfully before loadSection().
    // -------------------------------------------------------------------------
    bool load(std::string_view path) noexcept
    {
        m_bLoaded = false;
        m_mapSettings.clear();
        m_strActiveSection.clear();

        m_strIniPath = std::string(path);

        if (false == m_IniParser.load(m_strIniPath))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("Failed to load ini:");
                      LOG_STRING(m_strIniPath));
            return false;
        }

        m_bLoaded = true;
        return true;

    } /* load() */


    // -------------------------------------------------------------------------
    //  loadSection()
    //  Resolves the given section name into the internal key/value map.
    //  Replaces any previously active section.
    //  Returns false (with LOG_ERROR) if load() has not been called first.
    //  Returns true (with LOG_WARNING) if the section does not exist, leaving
    //  the internal map empty — callers should apply their own defaults.
    // -------------------------------------------------------------------------
    bool loadSection(std::string_view sectionName) noexcept
    {
        if (false == m_bLoaded)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("loadSection: ini not loaded");
                      LOG_STRING(sectionName));
            return false;
        }

        m_mapSettings.clear();
        m_strActiveSection = std::string(sectionName);

        if (false == m_IniParser.sectionExists(m_strActiveSection))
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING(m_strActiveSection);
                      LOG_STRING(": section not found in .ini file"));
            return true;  // not a hard error — caller applies defaults
        }

        if (false == m_IniParser.getResolvedSection(m_strActiveSection, m_mapSettings))
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING(m_strActiveSection);
                      LOG_STRING(": failed to resolve section from .ini file"));
            return false;
        }

        return true;

    } /* loadSection() */


    // -------------------------------------------------------------------------
    //  getBoolFromIni()
    //  Looks up 'key' in the active section and evaluates it as a boolean
    //  expression via BoolExprEvaluator.
    //  On missing key or evaluation failure the caller-supplied default in
    //  'value' is preserved and false is returned.
    // -------------------------------------------------------------------------
    bool getBoolFromIni(std::string_view key, bool& value) noexcept
    {
        const std::string strKey(key);

        if ((m_mapSettings.count(strKey) == 0) ||
            (false == m_beEvaluator.evaluate(m_mapSettings.at(strKey), value)))
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("Missing/wrong ini value for:");
                      LOG_STRING(key);
                      LOG_STRING(": using default value"));
            return false;
        }

        return true;

    } /* getBoolFromIni() */


    // -------------------------------------------------------------------------
    //  getNumFromIni()
    //  Looks up 'key' in the active section and converts it to size_t.
    //  On missing key or conversion failure the caller-supplied default in
    //  'value' is preserved and false is returned.
    // -------------------------------------------------------------------------
    bool getNumFromIni(std::string_view key, size_t& value) noexcept
    {
        const std::string strKey(key);

        if ((m_mapSettings.count(strKey) == 0) ||
            (false == numeric::str2sizet(m_mapSettings.at(strKey), value)))
        {
            LOG_PRINT(LOG_WARNING, LOG_HDR;
                      LOG_STRING("Missing/wrong ini value for:");
                      LOG_STRING(key);
                      LOG_STRING(": using default value"));
            return false;
        }

        return true;

    } /* getNumFromIni() */


    // -------------------------------------------------------------------------
    //  Accessors
    // -------------------------------------------------------------------------

    /*  Returns true if the named section is present in the loaded .ini file. */
    bool sectionExists(std::string_view sectionName) const noexcept
    {
        if (false == m_bLoaded)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("IniLoader: sectionExists() called before load() —");
                      LOG_STRING(sectionName));
            return false;
        }
        return m_IniParser.sectionExists(std::string(sectionName));
    }

    /*  Resolves a named section into a caller-supplied map. */
    bool resolveSection(std::string_view sectionName,
                        std::unordered_map<std::string, std::string>& outMap) const noexcept
    {
        if (false == m_bLoaded)
        {
            LOG_PRINT(LOG_ERROR, LOG_HDR;
                      LOG_STRING("IniLoader: resolveSection() called before load() —");
                      LOG_STRING(sectionName));
            return false;
        }
        return m_IniParser.getResolvedSection(std::string(sectionName), outMap);
    }

    /** Returns true if load() completed successfully. */
    bool isLoaded() const noexcept { return m_bLoaded; }

    /** Returns true if the active section map has at least one entry. */
    bool hasSectionContent() const noexcept { return !m_mapSettings.empty(); }

    /** Returns the name of the currently active section (empty if none). */
    const std::string& activeSection() const noexcept { return m_strActiveSection; }


private:

    IniParserEx     m_IniParser;
    BoolExprEvaluator m_beEvaluator;

    std::unordered_map<std::string, std::string> m_mapSettings;

    std::string m_strIniPath;
    std::string m_strActiveSection;
    bool        m_bLoaded { false };

}; /* class IniCfgLoader */

#endif /* U_INI_CONFIG_LOADER_HPP */
