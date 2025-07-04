#ifndef SCRIPTRUNNER_HPP
#define SCRIPTRUNNER_HPP

#include "IScriptRunner.hpp"
#include "IScriptReader.hpp"
#include "IScriptValidator.hpp"
#include "IScriptInterpreter.hpp"

#include <string>
#include <memory>


class ScriptRunner : public IScriptRunner
{
public:

    ScriptRunner( std::shared_ptr<IScriptReader> shpScriptReader,
                  std::shared_ptr<IScriptValidator> shvScriptValidator,
                  std::shared_ptr<IScriptInterpreter> shvScriptInterpreter )
        : m_shpScriptReader(std::move(shpScriptReader))
        , m_shvScriptValidator(std::move(shvScriptValidator))
        , m_shvScriptInterpreter(std::move(shvScriptInterpreter))
    {}

    bool runScript() override;

private:

    std::shared_ptr<IScriptReader> m_shpScriptReader;
    std::shared_ptr<IScriptValidator> m_shvScriptValidator;
    std::shared_ptr<IScriptInterpreter> m_shvScriptInterpreter;
};

#endif // SCRIPTRUNNER_HPP
