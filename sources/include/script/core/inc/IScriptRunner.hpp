#ifndef ISCRIPTRUNNER_HPP
#define ISCRIPTRUNNER_HPP

class IScriptRunner
{
    public:

        virtual bool runScript(bool bValidateOnly = false) = 0;

        virtual ~IScriptRunner() = default;
};

#endif // ISCRIPTRUNNER_HPP
