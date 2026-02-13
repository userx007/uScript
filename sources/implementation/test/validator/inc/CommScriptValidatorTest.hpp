#ifndef COMMSCRIPT_VALIDATOR_TEST_HPP
#define COMMSCRIPT_VALIDATOR_TEST_HPP

#include "CommScriptCommandValidator.hpp"
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <iomanip>

/**
 * @brief Test harness for CommScriptCommandValidator
 * 
 * Provides structured testing with expected results and detailed reporting
 */
class CommScriptValidatorTest
{
public:
    
    struct TestCase
    {
        std::string input;
        bool expectedValid;
        std::string description;
        CommCommandDirection expectedDirection;
        CommCommandTokenType expectedFirstToken;
        CommCommandTokenType expectedSecondToken;
        
        TestCase(const std::string& in, bool valid, const std::string& desc,
                 CommCommandDirection dir = CommCommandDirection::SEND_RECV,
                 CommCommandTokenType tok1 = CommCommandTokenType::INVALID,
                 CommCommandTokenType tok2 = CommCommandTokenType::INVALID)
            : input(in), expectedValid(valid), description(desc),
              expectedDirection(dir), expectedFirstToken(tok1), expectedSecondToken(tok2) {}
    };

    /**
     * @brief Run all test cases
     */
    static void runAllTests()
    {
        std::cout << "========================================" << std::endl;
        std::cout << "CommScriptCommandValidator Test Suite" << std::endl;
        std::cout << "========================================" << std::endl << std::endl;

        int totalTests = 0;
        int passedTests = 0;
        int failedTests = 0;

        auto testSets = {
            getValidSendTests(),
            getValidReceiveTests(),
            getEdgeCaseTests(),
            getInvalidDirectionTests(),
            getInvalidTokenTests(),
            getInvalidFieldTests(),
            getInvalidSemanticTests(),
            getComplexValidTests(),
            getBoundaryTests()
        };

        const char* sectionNames[] = {
            "Valid Send Operations",
            "Valid Receive Operations",
            "Edge Cases (Valid)",
            "Invalid Direction Errors",
            "Invalid Token Type Errors",
            "Invalid Field Errors",
            "Invalid Semantic Errors",
            "Complex Valid Scenarios",
            "Boundary Conditions"
        };

        int sectionIdx = 0;
        for (const auto& testSet : testSets)
        {
            std::cout << "\n--- " << sectionNames[sectionIdx++] << " ---\n" << std::endl;
            
            for (const auto& test : testSet)
            {
                bool result = runSingleTest(test);
                totalTests++;
                if (result) {
                    passedTests++;
                } else {
                    failedTests++;
                }
            }
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Results Summary" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "Total Tests:  " << totalTests << std::endl;
        std::cout << "Passed:       " << passedTests << " (" 
                  << std::fixed << std::setprecision(1) 
                  << (100.0 * passedTests / totalTests) << "%)" << std::endl;
        std::cout << "Failed:       " << failedTests << " (" 
                  << std::fixed << std::setprecision(1) 
                  << (100.0 * failedTests / totalTests) << "%)" << std::endl;
        std::cout << "========================================" << std::endl;
    }

private:

    /**
     * @brief Run a single test case
     */
    static bool runSingleTest(const TestCase& test)
    {
        CommScriptCommandValidator validator;
        CommCommand result;
        
        bool actualValid = validator.validateItem(test.input, result);
        bool testPassed = (actualValid == test.expectedValid);
        
        // Additional validation for successful parses
        if (testPassed && actualValid && test.expectedValid)
        {
            if (test.expectedDirection != CommCommandDirection::SEND_RECV || 
                test.expectedFirstToken != CommCommandTokenType::INVALID)
            {
                testPassed = testPassed && (result.direction == test.expectedDirection);
                testPassed = testPassed && (result.tokens.first == test.expectedFirstToken);
                
                if (test.expectedSecondToken != CommCommandTokenType::INVALID)
                {
                    testPassed = testPassed && (result.tokens.second == test.expectedSecondToken);
                }
            }
        }
        
        // Print result
        std::cout << (testPassed ? "[PASS] " : "[FAIL] ") 
                  << test.description << std::endl;
        
        if (!testPassed)
        {
            std::cout << "       Input: \"" << test.input << "\"" << std::endl;
            std::cout << "       Expected: " << (test.expectedValid ? "VALID" : "INVALID") << std::endl;
            std::cout << "       Actual:   " << (actualValid ? "VALID" : "INVALID") << std::endl;
            
            if (actualValid && test.expectedValid)
            {
                std::cout << "       Direction - Expected: " << static_cast<int>(test.expectedDirection)
                          << ", Actual: " << static_cast<int>(result.direction) << std::endl;
                std::cout << "       Token1 - Expected: " << static_cast<int>(test.expectedFirstToken)
                          << ", Actual: " << static_cast<int>(result.tokens.first) << std::endl;
            }
        }
        
        return testPassed;
    }

    // Test case generators
    
    static std::vector<TestCase> getValidSendTests()
    {
        return {
            TestCase("> hello", true, "Send raw string", 
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_RAW, CommCommandTokenType::EMPTY),
            TestCase("> \"Hello World\"", true, "Send delimited string",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::EMPTY),
            TestCase("> L\"This is a line\"", true, "Send line",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::LINE, CommCommandTokenType::EMPTY),
            TestCase("> H\"48656C6C6F\"", true, "Send hex stream",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::EMPTY),
            TestCase("> \"CONNECT\" | \"OK\"", true, "Send and receive string",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> \"AT+CMD\" | T\"OK\"", true, "Send string, receive token",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::TOKEN),
            TestCase("> \"GET STATUS\" | R\"^STATUS: [0-9]+\"", true, "Send string, receive regex",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::REGEX),
            TestCase("> H\"DEADBEEF\" | H\"CAFEBABE\"", true, "Send hex, receive hex",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::HEXSTREAM),
            TestCase("> \"REQUEST_SIZE\" | S\"1024\"", true, "Send string, receive size",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::SIZE),
            TestCase("> command | \"OK\"", true, "Send raw, receive delimited",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_RAW, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> F\"test_data/firmware.bin\" | F\"test_data/empty_file.bin\"", true, "Send file, receive file",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::FILENAME, CommCommandTokenType::FILENAME)

        };
    }

    static std::vector<TestCase> getValidReceiveTests()
    {
        return {
            TestCase("< hello", true, "Receive raw string",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::STRING_RAW, CommCommandTokenType::EMPTY),
            TestCase("< \"Hello World\"", true, "Receive delimited string",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::EMPTY),
            TestCase("< T\"OK\"", true, "Receive token",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::TOKEN, CommCommandTokenType::EMPTY),
            TestCase("< R\"^ERROR: [0-9]+\"", true, "Receive regex",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::REGEX, CommCommandTokenType::EMPTY),
            TestCase("< S\"512\"", true, "Receive size",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::SIZE, CommCommandTokenType::EMPTY),
            TestCase("< H\"12345678\"", true, "Receive hex stream",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::EMPTY),
            TestCase("< T\"READY\" | \"START\"", true, "Receive token, send string",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::TOKEN, CommCommandTokenType::STRING_DELIMITED),
            TestCase("< R\"^WAIT.*\" | \"CONTINUE\"", true, "Receive regex, send string",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::REGEX, CommCommandTokenType::STRING_DELIMITED),
            TestCase("< S\"256\" | \"data_payload\"", true, "Receive size, send data",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::SIZE, CommCommandTokenType::STRING_DELIMITED),
            TestCase("< \"prompt\" | \"response\"", true, "Receive string, send string",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("< F\"test_data/empty_file.bin\" | F\"test_data/firmware.bin\"", true, "Receive file, send file",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::FILENAME, CommCommandTokenType::FILENAME)

        };
    }

    static std::vector<TestCase> getEdgeCaseTests()
    {
        return {
            TestCase(">    \"data\"   |   \"response\"   ", true, "Whitespace handling",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> \"  spaced  data  \" | \"  response  \"", true, "Whitespace inside quotes",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> \"data with | pipe\" | \"response\"", true, "Pipe inside quotes",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> \"A\" | \"B\"", true, "Single character strings",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> \"12345\" | \"67890\"", true, "Numeric strings",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::STRING_DELIMITED)
        };
    }

    static std::vector<TestCase> getInvalidDirectionTests()
    {
        return {
            TestCase("\"hello\" | \"world\"", false, "Missing direction indicator"),
            TestCase("* \"hello\" | \"world\"", false, "Invalid direction character"),
            TestCase("= \"hello\" | \"world\"", false, "Wrong direction character"),
            TestCase("", false, "Empty input"),
            TestCase("hello > world", false, "Direction in middle")
        };
    }

    static std::vector<TestCase> getInvalidTokenTests()
    {
        return {
            TestCase("> T\"CANNOT_SEND\"", false, "Cannot send token"),
            TestCase("> R\"^pattern.*\"", false, "Cannot send regex"),
            TestCase("> S\"256\"", false, "Cannot send size"),
            TestCase(">", false, "Send empty"),
            TestCase("< ", false, "Receive empty"),
            TestCase("> H\"GHIJKL\"", false, "Invalid hex (non-hex chars)"),
            TestCase("< S\"abc\"", false, "Invalid size (non-numeric)"),
            TestCase("< T\"\"", false, "Empty token"),
            TestCase("< R\"\"", false, "Empty regex"),
            TestCase("> L\"\"", false, "Empty line"),
            TestCase("> H\"\"", false, "Empty hex")
        };
    }

    static std::vector<TestCase> getInvalidFieldTests()
    {
        return {
            TestCase(">  | ", false, "Both fields empty"),
            TestCase("> | \"response\"", false, "Empty first field"),
            TestCase("> \"send\" | ", false, "Empty second field"),
            TestCase("> \"data\" | \"middle\" | \"end\"", false, "Multiple separators"),
            TestCase("> | \"data\"", false, "Pipe at start"),
            TestCase("> |", false, "Only separator")
        };
    }

    static std::vector<TestCase> getInvalidSemanticTests()
    {
        return {
            TestCase("> T\"SEND_TOKEN\" | \"response\"", false, "Send token with response"),
            TestCase("> R\"^send.*\" | T\"OK\"", false, "Send regex"),
            TestCase("> S\"512\" | \"data\"", false, "Send size"),
            TestCase("> | T\"OK\"", false, "Send empty field"),
            TestCase("< | \"send\"", false, "Receive empty field"),
            TestCase(">  |  ", false, "Both empty fields"),
            TestCase("> \"\" | \"\"", false, "Both empty delimited")
        };
    }

    static std::vector<TestCase> getComplexValidTests()
    {
        return {
            TestCase("> H\"FFAA5501\" | T\"ACK\"", true, "Binary protocol",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::TOKEN),
            TestCase("> \"AT\" | T\"OK\"", true, "AT command",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::STRING_DELIMITED, CommCommandTokenType::TOKEN),
            TestCase("< R\"^GET_DATA:[0-9]+$\" | \"response\"", true, "Pattern match then send",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::REGEX, CommCommandTokenType::STRING_DELIMITED),
            TestCase("> L\"CONNECT server:port\" | T\"CONNECTED\"", true, "Line protocol",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::LINE, CommCommandTokenType::TOKEN),
            TestCase("< S\"4096\" | \"data_chunk\"", true, "Size-based receive",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::SIZE, CommCommandTokenType::STRING_DELIMITED)
        };
    }

    static std::vector<TestCase> getBoundaryTests()
    {
        return {
            TestCase("> H\"FF\"", true, "Single hex byte",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::EMPTY),
            TestCase("> H\"AABB\"", true, "Two hex bytes",
                     CommCommandDirection::SEND_RECV, CommCommandTokenType::HEXSTREAM, CommCommandTokenType::EMPTY),
            TestCase("< S\"0\"", true, "Size zero",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::SIZE, CommCommandTokenType::EMPTY),
            TestCase("< S\"1\"", true, "Size one",
                     CommCommandDirection::RECV_SEND, CommCommandTokenType::SIZE, CommCommandTokenType::EMPTY),
            TestCase("> H\"ABC\"", false, "Odd-length hex (invalid)")
        };
    }
};

#endif // COMMSCRIPT_VALIDATOR_TEST_HPP
