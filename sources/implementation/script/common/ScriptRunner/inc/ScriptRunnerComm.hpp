#ifndef SCRIPTRUNNER_COMM_HPP
#define SCRIPTRUNNER_COMM_HPP

#include "ScriptRunner.hpp"
#include "IScriptInterpreterComm.hpp"

#include <memory>

/////////////////////////////////////////////////////////////////////////////////
//                            LOCAL DEFINITIONS                                //
/////////////////////////////////////////////////////////////////////////////////

#ifdef LT_HDR
    #undef LT_HDR
#endif
#ifdef LOG_HDR
    #undef LOG_HDR
#endif

#define LT_HDR     "S_RUNNER_C :"
#define LOG_HDR    LOG_STRING(LT_HDR)


/////////////////////////////////////////////////////////////////////////////////
//                    CLASS DECLARATION / DEFINITION                           //
/////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Level 2: Script runner with communication driver support
 * 
 * Extends the basic script runner to work with communication-enabled
 * interpreters for scripts that interact with devices/drivers.
 * Introduces TDriver template parameter at this level.
 * 
 * @tparam TScriptEntries Type representing script entries/commands
 * @tparam TDriver Type of communication driver
 */
template<typename TScriptEntries, typename TDriver = void>
class ScriptRunnerComm : public ScriptRunner<TScriptEntries>
{
public:

    /**
     * @brief Construct a communication-enabled script runner
     * @param shpScriptReader Script reader component
     * @param shvScriptValidator Script validator component
     * @param shvScriptInterpreter Communication-enabled script interpreter (Level 2+)
     */
    explicit ScriptRunnerComm( std::shared_ptr<IScriptReader> shpScriptReader,
                               std::shared_ptr<IScriptValidator<TScriptEntries>> shvScriptValidator,
                               std::shared_ptr<IScriptInterpreterComm<TScriptEntries, TDriver>> shvScriptInterpreter )
        : ScriptRunner<TScriptEntries>(shpScriptReader, shvScriptValidator, shvScriptInterpreter)
        , m_shpScriptInterpreterComm(std::move(shvScriptInterpreter))
    {}

    /**
     * @brief Get the communication-enabled interpreter
     * @return Shared pointer to the communication interpreter
     */
    std::shared_ptr<IScriptInterpreterComm<TScriptEntries, TDriver>> getCommInterpreter() const
    {
        return m_shpScriptInterpreterComm;
    }

private:

    std::shared_ptr<IScriptInterpreterComm<TScriptEntries, TDriver>> m_shpScriptInterpreterComm;
};

#endif // SCRIPTRUNNER_COMM_HPP
