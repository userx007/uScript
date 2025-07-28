#ifndef FTDI245_HPP
#define FTDI245_HPP

#ifdef _WIN32
    #include "ftdi245_windows.hpp"
#else
    #include "ftdi245_linux.hpp"
#endif

#endif //FTDI245_HPP