#include "uVolatileMacroStore.hpp"

namespace uvolatile
{

// Exactly one definition process-wide: this translation unit is compiled
// into the uVolatileMacroStore SHARED library (see CMakeLists.txt next to
// this file). Every consumer - the main "uscript" executable (statically
// linking uScriptInterpreter, which writes here via
// ScriptInterpreter::m_setRuntimeVarMacro()) and every dlopen()'d
// CYCLIC-capable plugin .so (which reads here via
// ucmdexec::generic_send_cyclic() -> resolveVolatileMacros()) - dynamically
// links against this same .so, so they all get the same function-local
// static below instead of each image growing its own private copy. See the
// header's rationale comment for the full "why" of this split.
VolatileMacroStore& VolatileMacroStore::instance()
{
    static VolatileMacroStore sInstance;
    return sInstance;
}

} // namespace uvolatile
