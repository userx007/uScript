#ifndef ISCRIPTRUNNER_HPP
#define ISCRIPTRUNNER_HPP

class IScriptRunner
{
    public:

        virtual bool runScript() = 0;

        virtual ~IScriptRunner() = default;
};

#endif // ISCRIPTRUNNER_HPP
