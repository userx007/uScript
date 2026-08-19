/**
 * @file    uart_monitor_examples.cpp
 * @brief   Comprehensive examples for uart_monitor_complete.hpp
 */

#include "uart_monitor_complete.hpp"
#include <iostream>
#include <iomanip>
#include <thread>

using namespace std::chrono_literals;

///////////////////////////////////////////////////////////////////
//                   SIMPLE API EXAMPLES                         //
///////////////////////////////////////////////////////////////////

void example_01_list_ports() {
    std::cout << "\n=== Example 1: List Available Ports ===\n";
    
    auto ports = uart::list_ports();
    
    std::cout << "Found " << ports.size() << " UART port(s):\n";
    for (const auto& port : ports) {
        std::cout << "  • " << port << "\n";
    }
}

void example_02_count_ports() {
    std::cout << "\n=== Example 2: Count Ports ===\n";
    
    size_t count = uart::get_port_count();
    std::cout << "Number of available ports: " << count << "\n";
}

void example_03_wait_insertion_simple() {
    std::cout << "\n=== Example 3: Wait for Insertion (Simple API) ===\n";
    std::cout << "Please connect a UART device within 10 seconds...\n";
    
    // Simple one-liner
    if (auto port = uart::wait_for_insertion(10000, 100)) {
        std::cout << "✓ Port detected: " << *port << "\n";
    } else {
        std::cout << "✗ Timeout: No port was inserted\n";
    }
}

void example_04_wait_removal_simple() {
    std::cout << "\n=== Example 4: Wait for Removal (Simple API) ===\n";
    std::cout << "Please disconnect a UART device within 10 seconds...\n";
    
    if (auto port = uart::wait_for_removal(10000, 100)) {
        std::cout << "✓ Port removed: " << *port << "\n";
    } else {
        std::cout << "✗ Timeout: No port was removed\n";
    }
}

void example_05_custom_config() {
    std::cout << "\n=== Example 5: Custom Configuration (Simple API) ===\n";
    
    uart::ScanConfig config{
        std::chrono::milliseconds{200},  // Slower polling
        std::chrono::milliseconds{5000}  // 5 second timeout
    };
    
    uart::SimplePortHandler handler{config};
    
    std::cout << "Current ports: " << handler.get_port_count() << "\n";
    std::cout << "Waiting for insertion (5s timeout, 200ms polling)...\n";
    
    if (auto port = handler.wait_for_insertion()) {
        std::cout << "Port detected: " << *port << "\n";
    } else {
        std::cout << "Timeout\n";
    }
}

///////////////////////////////////////////////////////////////////
//                  ADVANCED API EXAMPLES                        //
///////////////////////////////////////////////////////////////////

void example_06_monitor_basic() {
    std::cout << "\n=== Example 6: Basic Monitoring (Advanced API) ===\n";
    
    uart::PortMonitor monitor;
    monitor.setPollingInterval(100);
    
    try {
        monitor.startMonitoring();
        std::cout << "Monitoring started. Try connecting/disconnecting devices...\n";
        std::cout << "Will monitor for 15 seconds.\n";
        
        // Use the monitor for 15 seconds
        auto end_time = std::chrono::steady_clock::now() + 15s;
        
        while (std::chrono::steady_clock::now() < end_time) {
            // Check for insertions with 1 second timeout
            auto insert_result = monitor.waitForInsert(1000ms);
            
            if (insert_result) {
                std::cout << "→ Inserted: " << insert_result.port_name << "\n";
            }
            
            // Check for removals with 1 second timeout
            auto remove_result = monitor.waitForRemoval(1000ms);
            
            if (remove_result) {
                std::cout << "← Removed:  " << remove_result.port_name << "\n";
            }
        }
        
        monitor.stopMonitoring();
        std::cout << "Monitoring stopped.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

void example_07_wait_with_result() {
    std::cout << "\n=== Example 7: Wait with Detailed Results ===\n";
    
    uart::PortMonitor monitor;
    
    try {
        monitor.startMonitoring();
        std::cout << "Waiting for port insertion (5s timeout)...\n";
        
        auto result = monitor.waitForInsert(5s);
        
        switch (result.result) {
            case uart::WaitResult::Success:
                std::cout << "✓ Success! Port: " << result.port_name << "\n";
                break;
                
            case uart::WaitResult::Timeout:
                std::cout << "⏱ Timeout: No port detected in 5 seconds\n";
                break;
                
            case uart::WaitResult::Stopped:
                std::cout << "⚠ Monitoring was stopped\n";
                break;
        }
        
        monitor.stopMonitoring();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

void example_08_continuous_monitoring() {
    std::cout << "\n=== Example 8: Continuous Monitoring ===\n";
    
    uart::PortMonitor monitor;
    
    try {
        monitor.startMonitoring();
        std::cout << "Continuous monitoring for 30 seconds...\n";
        std::cout << "Try connecting/disconnecting devices.\n\n";
        
        auto start_time = std::chrono::steady_clock::now();
        auto end_time = start_time + 30s;
        
        int insertion_count = 0;
        int removal_count = 0;
        
        while (std::chrono::steady_clock::now() < end_time) {
            // Wait for any event with 500ms timeout
            auto insert_result = monitor.waitForInsert(500ms);
            
            if (insert_result) {
                ++insertion_count;
                auto now = std::time(nullptr);
                std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") << "] ";
                std::cout << "➕ Inserted: " << insert_result.port_name;
                std::cout << " (Total: " << insertion_count << ")\n";
            }
            
            auto remove_result = monitor.waitForRemoval(500ms);
            
            if (remove_result) {
                ++removal_count;
                auto now = std::time(nullptr);
                std::cout << "[" << std::put_time(std::localtime(&now), "%H:%M:%S") << "] ";
                std::cout << "➖ Removed:  " << remove_result.port_name;
                std::cout << " (Total: " << removal_count << ")\n";
            }
        }
        
        monitor.stopMonitoring();
        
        std::cout << "\nStatistics:\n";
        std::cout << "  Insertions: " << insertion_count << "\n";
        std::cout << "  Removals:   " << removal_count << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

void example_09_error_handling() {
    std::cout << "\n=== Example 9: Error Handling ===\n";
    
    uart::PortMonitor monitor;
    monitor.setPollingInterval(100);
    
    try {
        monitor.startMonitoring();
        std::cout << "✓ Monitoring started\n";
        
        // Try to start again - should throw
        try {
            monitor.startMonitoring();
            std::cout << "✗ Should have thrown!\n";
        } catch (const std::logic_error& e) {
            std::cout << "✓ Caught expected error: " << e.what() << "\n";
        }
        
        // Try to change polling interval while monitoring - should throw
        try {
            monitor.setPollingInterval(200);
            std::cout << "✗ Should have thrown!\n";
        } catch (const std::logic_error& e) {
            std::cout << "✓ Caught expected error: " << e.what() << "\n";
        }
        
        monitor.stopMonitoring();
        std::cout << "✓ Monitoring stopped\n";
        
        // Now we can change polling interval
        monitor.setPollingInterval(200);
        std::cout << "✓ Polling interval changed to 200ms\n";
        
        // Stopping again is safe (idempotent)
        monitor.stopMonitoring();
        std::cout << "✓ Stop is idempotent - safe to call multiple times\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
    }
}

void example_10_thread_safety() {
    std::cout << "\n=== Example 10: Thread Safety Demo ===\n";
    
    uart::PortMonitor monitor;
    
    try {
        monitor.startMonitoring();
        std::cout << "Monitoring started. Launching multiple reader threads...\n";
        
        std::atomic<bool> running{true};
        
        // Thread 1: Watch for insertions
        std::thread insert_watcher([&]() {
            while (running.load()) {
                auto result = monitor.waitForInsert(500ms);
                if (result) {
                    std::cout << "[Thread 1] Insertion: " << result.port_name << "\n";
                }
            }
        });
        
        // Thread 2: Watch for removals
        std::thread remove_watcher([&]() {
            while (running.load()) {
                auto result = monitor.waitForRemoval(500ms);
                if (result) {
                    std::cout << "[Thread 2] Removal: " << result.port_name << "\n";
                }
            }
        });
        
        // Thread 3: Periodically list ports
        std::thread lister([&]() {
            while (running.load()) {
                std::this_thread::sleep_for(5s);
                auto ports = monitor.listPorts();
                std::cout << "[Thread 3] Current ports: " << ports.size() << "\n";
            }
        });
        
        std::cout << "Running for 20 seconds with 3 threads...\n";
        std::this_thread::sleep_for(20s);
        
        running.store(false);
        
        insert_watcher.join();
        remove_watcher.join();
        lister.join();
        
        monitor.stopMonitoring();
        std::cout << "All threads stopped safely.\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

///////////////////////////////////////////////////////////////////
//                   LEGACY API EXAMPLES                         //
///////////////////////////////////////////////////////////////////

void example_11_legacy_api() {
    std::cout << "\n=== Example 11: Legacy API (Backward Compatibility) ===\n";
    
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    
    char buffer[256];
    
    // Legacy function - still works but deprecated
    uint32_t port_count = uart::legacy::uart_get_available_ports_number();
    std::cout << "Port count (legacy API): " << port_count << "\n";
    
    std::cout << "Waiting for insertion (legacy API)...\n";
    bool result = uart::legacy::uart_wait_port_insert(buffer, sizeof(buffer), 5000, 100);
    
    if (result && buffer[0] != '\0') {
        std::cout << "Port inserted (legacy API): " << buffer << "\n";
    } else {
        std::cout << "No port inserted or timeout\n";
    }
    
    #pragma GCC diagnostic pop
}

///////////////////////////////////////////////////////////////////
//                   PRACTICAL USE CASES                         //
///////////////////////////////////////////////////////////////////

void example_12_wait_for_specific_device() {
    std::cout << "\n=== Example 12: Wait for Specific Device Type ===\n";
    
    uart::PortMonitor monitor;
    
    try {
        monitor.startMonitoring();
        std::cout << "Waiting for Arduino device (ACM or USB)...\n";
        
        bool found = false;
        auto timeout = std::chrono::steady_clock::now() + 30s;
        
        while (!found && std::chrono::steady_clock::now() < timeout) {
            auto result = monitor.waitForInsert(1s);
            
            if (result) {
                const auto& port = result.port_name;
                
                // Check if it's an Arduino-like device
                if (port.find("ACM") != std::string::npos || 
                    port.find("USB") != std::string::npos) {
                    std::cout << "✓ Found Arduino device: " << port << "\n";
                    found = true;
                } else {
                    std::cout << "Detected " << port << " (not Arduino, continuing...)\n";
                }
            }
        }
        
        if (!found) {
            std::cout << "✗ Arduino device not found within 30 seconds\n";
        }
        
        monitor.stopMonitoring();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

void example_13_auto_reconnect() {
    std::cout << "\n=== Example 13: Auto-Reconnect Pattern ===\n";
    
    uart::PortMonitor monitor;
    
    try {
        monitor.startMonitoring();
        std::cout << "Starting auto-reconnect monitor (15 seconds)...\n";
        
        std::string current_device;
        auto end_time = std::chrono::steady_clock::now() + 15s;
        
        while (std::chrono::steady_clock::now() < end_time) {
            if (current_device.empty()) {
                // No device connected, wait for one
                auto result = monitor.waitForInsert(1s);
                if (result) {
                    current_device = result.port_name;
                    std::cout << "✓ Connected to: " << current_device << "\n";
                    std::cout << "  [Now you can use this device]\n";
                }
            } else {
                // Device connected, watch for disconnection
                auto result = monitor.waitForRemoval(1s);
                if (result && result.port_name == current_device) {
                    std::cout << "✗ Lost connection to: " << current_device << "\n";
                    std::cout << "  [Attempting to reconnect...]\n";
                    current_device.clear();
                }
            }
        }
        
        monitor.stopMonitoring();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
}

///////////////////////////////////////////////////////////////////
//                      MAIN                                     //
///////////////////////////////////////////////////////////////////

int main() {
    std::cout << "╔═══════════════════════════════════════════════════╗\n";
    std::cout << "║   UART Port Monitor - Complete Implementation    ║\n";
    std::cout << "║          Comprehensive Usage Examples             ║\n";
    std::cout << "╚═══════════════════════════════════════════════════╝\n";
    
    // Run examples (uncomment the ones you want to try)
    
    example_01_list_ports();
    example_02_count_ports();
    
    // Uncomment to try interactive examples:
    // example_03_wait_insertion_simple();
    // example_04_wait_removal_simple();
    // example_05_custom_config();
    // example_06_monitor_basic();
    // example_07_wait_with_result();
    // example_08_continuous_monitoring();
    // example_09_error_handling();
    // example_10_thread_safety();
    // example_11_legacy_api();
    // example_12_wait_for_specific_device();
    // example_13_auto_reconnect();
    
    std::cout << "\n═══════════════════════════════════════════════════\n";
    std::cout << "Examples complete. Uncomment others in main() to try more.\n";
    std::cout << "═══════════════════════════════════════════════════\n";
    
    return 0;
}
