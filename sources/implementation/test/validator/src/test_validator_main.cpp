#include "CommScriptValidatorTest.hpp"
#include <iostream>
#include <vector>
#include <unordered_map>

/**
 * @brief Main test driver for CommScriptCommandValidator
 * 
 * This program runs comprehensive tests on the validator to ensure
 * all valid commands are accepted and all invalid commands are rejected.
 */
int main(int argc, char* argv[])
{
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  CommScriptCommandValidator - Comprehensive Test Suite     ║\n";
    std::cout << "╚════════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    try
    {
        // Run all automated tests
        CommScriptValidatorTest::runAllTests();
        
        std::cout << "\n";
        std::cout << "Test execution completed successfully.\n";
        std::cout << "\n";
        
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "\nFATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "\nFATAL ERROR: Unknown exception occurred" << std::endl;
        return 2;
    }
}
