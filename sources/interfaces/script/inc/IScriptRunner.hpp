#ifndef ISCRIPTRUNNER_HPP
#define ISCRIPTRUNNER_HPP

#include <string>

class IScriptRunner
{
public:
    virtual bool runScript() = 0;

    virtual ~IScriptRunner() = default;
};

#endif // ISCRIPTRUNNER_HPP
