#ifndef PLUGIN_EXPORT_HPP
#define PLUGIN_EXPORT_HPP


#if defined _WIN32 || defined __CYGWIN__
    #ifdef __GNUC__
        #define EXPORTED __attribute__ ((dllexport))
    #else
        #define EXPORTED __declspec(dllexport)
    #endif
    #define NOT_EXPORTED
#else
    #if __GNUC__ >= 4
        #define EXPORTED __attribute__ ((visibility ("default")))
        #define NOT_EXPORTED  __attribute__ ((visibility ("hidden")))
    #else
        #define EXPORTED
        #define NOT_EXPORTED
    #endif
#endif

#endif /* PLUGIN_EXPORT_HPP */