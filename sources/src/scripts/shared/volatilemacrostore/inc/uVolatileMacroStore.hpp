#ifndef U_VOLATILE_MACRO_STORE_HPP
#define U_VOLATILE_MACRO_STORE_HPP

#include <mutex>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>

/////////////////////////////////////////////////////////////////////////////////
//                                  RATIONALE                                  //
/////////////////////////////////////////////////////////////////////////////////
//
// ScriptInterpreter owns a private, per-instance map of "?=" variable-macro
// values (m_RuntimeVarMacros, guarded by m_runtimeVarMutex - see
// uScriptInterpreter.hpp/.cpp). That map is exactly what a threaded
// "VAL ?= PLUGIN.CMD args &" background command keeps refreshing, and it is
// exactly what ucmdexec::generic_send_cyclic() (see uCommandExec.hpp) needs
// to re-read on every tick of a long-running "un-cached" CYCLIC session, so
// that a $NAME reference used as one entry's val/id keeps tracking whatever
// the background command most recently produced instead of being frozen at
// whatever value it held the moment the CYCLIC line was merely dispatched.
//
// The interpreter's map can't be reached directly from there: plugin command
// handlers only ever see a PluginInterface::doDispatch(strCmd, strParams, st)
// call - the same fixed, shared-by-every-command signature documented in
// uExecContext.hpp's own rationale for isDryRun()/DryRunScope. Threading a
// "current interpreter instance" pointer (or a resolver callback) through
// doDispatch would mean changing that signature, PluginOperations.hpp's
// MFP<T>, and every single plugin command handler in the codebase - not just
// the CYCLIC ones this feature cares about.
//
// This header is the same kind of narrow, non-invasive alternative
// uExecContext.hpp already established for the dry-run flag: a small
// process-wide singleton, mutex-guarded (unlike isDryRun()'s thread_local
// flag, this one is genuinely written from one thread - the background
// command's jthread, via ScriptInterpreter::m_setRuntimeVarMacro() - and read
// from another - a threaded CYCLIC session's own jthread - so it needs real
// synchronization, not just thread-local isolation).
//
// ScriptInterpreter::m_setRuntimeVarMacro() mirrors every write here (see its
// definition in uScriptInterpreter.cpp) in addition to updating its own
// private map, so this store always holds the same values the interpreter
// would resolve a bare $NAME to right now - just reachable from plugin-common
// code that never sees the interpreter itself.
//
// IMPORTANT - why instance() is NOT defined inline in this header anymore:
// every CYCLIC-capable plugin (kcan_plugin, pcan_plugin, slcan_plugin, ...)
// is built as its own SHARED library, dlopen()'d at runtime by the main
// "uscript" executable via LOAD_PLUGIN, while uScriptInterpreter (the writer
// side, via m_setRuntimeVarMacro()) is a STATIC library baked directly into
// that same executable. Both sides reach this header only through header-only
// (INTERFACE) CMake targets (uUtils / uCommandExec). A Meyers singleton whose
// function-local static lives inside an `inline` function defined in such a
// header gets a SEPARATE instance compiled into every one of those images:
// the executable's own copy (written to by GENERATOR/"?=" background
// commands) and each plugin .so's own copy (read from by
// resolveVolatileMacros()) end up being different objects in different
// address spaces, so a plugin's read can never see the executable's write -
// a $NAME reference then looks "not (yet) a known volatile macro" forever,
// regardless of the CYCLIC_CACHED setting.
//
// Fixing this requires exactly one definition of instance() process-wide,
// which on both ELF and PE/COFF only a real shared library guarantees: the
// class declaration below stays in this shared header (so every consumer
// still just does `#include "uVolatileMacroStore.hpp"` and calls
// VolatileMacroStore::instance()), but instance() itself is only *declared*
// here and *defined* once in uVolatileMacroStore.cpp, compiled into the
// uVolatileMacroStore SHARED library that uUtils now links (see
// src/lib/volatilemacrostore/CMakeLists.txt). Every consumer of uUtils -
// the main executable and every plugin alike - dynamically links against
// that one .so/.dll at load time, so there is exactly one m_map/m_mutex for
// the whole process no matter how many separate images include this header.
/////////////////////////////////////////////////////////////////////////////////

namespace uvolatile
{

    /**
     * \brief Process-wide, thread-safe mirror of every "?=" variable macro's
     *        most recently assigned value.
     */
    class VolatileMacroStore
    {
        public:
            /** \brief Returns the single, process-wide instance. Defined exactly once,
             *        out-of-line, in uVolatileMacroStore.cpp (part of the
             *        uVolatileMacroStore SHARED library) - see this header's
             *        rationale above for why that matters across plugin .so
             *        boundaries. Do not move this back to an inline definition. */
            static VolatileMacroStore &instance();

            /** \brief Record/update a macro's current value (called by
             *        ScriptInterpreter::m_setRuntimeVarMacro() on every assignment). */
            void set(const std::string &strName, std::string strValue)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_map[strName] = std::move(strValue);
            }

            /** \brief Look up a macro's current value.
             * \return {true, value} if strName is a known volatile macro, {false, {}} otherwise. */
            std::pair<bool, std::string> get(const std::string &strName) const
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_map.find(strName);
                if (it != m_map.end()) {
                    return {true, it->second};
                }
                return {false, {}};
            }

            /** \brief Substitute every bare $NAME in strInOut that is a known volatile macro, taking
             *        the store's lock ONCE for the whole string (not once per reference).
             * \return true if at least one reference was substituted; strInOut is untouched otherwise. */
            bool resolve(std::string &strInOut) const
            {
                // Fast path: no '$' at all (typical for literal CYCLIC entries) - no lock, no scan.
                size_t iDollar = strInOut.find('$');
                if (iDollar == std::string::npos) {
                    return false;
                }

                auto isIdStart = [](char c) {
                    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
                };
                auto isIdChar = [&](char c) { return isIdStart(c) || (c >= '0' && c <= '9'); };

                bool bAnyReplaced = false;
                std::string strResult;
                size_t iCopied = 0; // strInOut[0, iCopied) already handled

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    for (; iDollar != std::string::npos; iDollar = strInOut.find('$', iDollar + 1)) {
                        if (iDollar + 1 >= strInOut.size() || !isIdStart(strInOut[iDollar + 1])) {
                            continue; // lone '$' or "$1": not a macro reference
                        }
                        size_t iEnd = iDollar + 2;
                        while (iEnd < strInOut.size() && isIdChar(strInOut[iEnd])) {
                            ++iEnd;
                        }

                        const auto it = m_map.find(std::string_view(strInOut).substr(iDollar + 1, iEnd - iDollar - 1));
                        if (it == m_map.end()) {
                            iDollar = iEnd - 1; // unknown here: leave "$NAME" unexpanded (copied later as-is)
                            continue;
                        }

                        if (!bAnyReplaced) {
                            strResult.reserve(strInOut.size() + it->second.size());
                        }
                        strResult.append(strInOut, iCopied, iDollar - iCopied);
                        strResult.append(it->second);
                        iCopied      = iEnd;
                        iDollar      = iEnd - 1;
                        bAnyReplaced = true;
                    }
                }

                if (bAnyReplaced) {
                    strResult.append(strInOut, iCopied, std::string::npos);
                    strInOut = std::move(strResult);
                }
                return bAnyReplaced;
            }

            VolatileMacroStore(const VolatileMacroStore &)            = delete;
            VolatileMacroStore &operator=(const VolatileMacroStore &) = delete;

        private:
            VolatileMacroStore() = default;

            // Transparent hash/equality: lookups by std::string_view build no temporary key string.
            struct TransparentHash
            {
                    using is_transparent = void;
                    size_t operator()(std::string_view sv) const noexcept
                    {
                        return std::hash<std::string_view>{}(sv);
                    }
            };

            mutable std::mutex m_mutex;
            std::unordered_map<std::string, std::string, TransparentHash, std::equal_to<>> m_map;
    };

    /*--------------------------------------------------------------------------------------------------------*/
    /**
     * \brief Re-substitute every bare $NAME reference in strInOut with its current value from
     *        VolatileMacroStore, in place. Deliberately simpler than ScriptInterpreter's own
     *        m_replaceVariableMacros(): only the plain "$NAME" form (no ".SIZE"/".N"/".$idx" array
     *        suffixes - those select an ARRAY_MACRO element and are already resolved once, up
     *        front, by the interpreter before a CYCLIC line is ever dispatched, since they define
     *        the entry list's *structure*, which must stay fixed for the lifetime of a CYCLIC
     *        session; see ucmdexec::generic_send_cyclic()'s cached/un-cached split). A name that
     *        isn't (yet) a known volatile macro is left unexpanded, exactly like the interpreter's
     *        own "leave unexpanded, next pass will retry" behaviour for a not-yet-resolvable
     *        reference.
     *
     * \param[in,out] strInOut  the entry text (a CYCLIC item's val or id field) to re-resolve
     *
     * \return true if at least one $NAME reference was substituted, false if none were (strInOut
     *          is left completely unchanged in that case, so callers can skip re-validating an
     *          entry that has no volatile content at all)
     */
    /*--------------------------------------------------------------------------------------------------------*/
    inline bool resolveVolatileMacros(std::string &strInOut)
    {
        // Same grammar as before (bare "$NAME": [A-Za-z_][A-Za-z0-9_]*), now a hand-written scan
        // with a no-'$' fast path and a single lock acquisition per call: see VolatileMacroStore::resolve().
        return VolatileMacroStore::instance().resolve(strInOut);
    }

} // namespace uvolatile

#endif // U_VOLATILE_MACRO_STORE_HPP
