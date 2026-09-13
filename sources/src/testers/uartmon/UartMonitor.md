# UART Monitor - Complete Implementation

## Overview

This is a **complete, self-contained, header-only** UART port monitoring library that consolidates and improves upon multiple previous implementations:

- ✅ **Zero external dependencies** (except standard library)
- ✅ **Thread-safe** with proper synchronization
- ✅ **Cross-platform** (Windows & Linux)
- ✅ **Two API levels**: Simple (synchronous) and Advanced (threaded)
- ✅ **Modern C++17** with clean design
- ✅ **Backward compatible** with legacy code

## What Was Merged

### Consolidated from 3 Files into 1

| Original File | Purpose | Status in Final |
|--------------|---------|----------------|
| `uDeviceHandling.hpp` | Device tracking logic | ✅ Integrated as `detail::DeviceTracker` with improvements |
| `uUartMonitor.hpp` | Original buggy implementation | ❌ Replaced entirely |
| `uUartMonitorExt.hpp` | Clean refactored version | ✅ Used as base, enhanced with threading |

### Key Improvements Over Original

1. **Fixed Critical Bugs**:
   - ❌ Old: Const-correctness abuse with `mutable` everywhere
   - ✅ New: Proper const semantics, only mutexes are mutable
   
   - ❌ Old: Thread safety violations, data races
   - ✅ New: Properly synchronized with mutexes and atomics
   
   - ❌ Old: Resource leaks from multiple thread starts
   - ✅ New: Thread lifecycle properly managed
   
   - ❌ Old: Ambiguous error handling (empty strings)
   - ✅ New: Explicit result types with `WaitResult` enum

2. **Device Tracking Improvements**:
   - **Old** (`uDeviceHandling.hpp`): Linear search through vector - O(n)
   - **New** (`detail::DeviceTracker`): Unordered set - O(1) lookups
   - Better memory efficiency
   - Cleaner API

3. **Added Two API Levels**:
   - **Simple API** (`SimplePortHandler`): For straightforward use cases
   - **Advanced API** (`PortMonitor`): For continuous monitoring with events

## Architecture

```
uart_monitor_complete.hpp
│
├─ namespace uart
│  │
│  ├─ Type Definitions
│  │  ├─ OperationType enum
│  │  ├─ WaitResult enum
│  │  ├─ PortEvent struct
│  │  ├─ PortWaitResult struct
│  │  └─ ScanConfig struct
│  │
│  ├─ namespace detail (Internal)
│  │  ├─ DeviceTracker class (replaces uDeviceHandling)
│  │  ├─ scan_windows_ports()
│  │  ├─ scan_linux_ports()
│  │  └─ glob_pattern()
│  │
│  ├─ Simple API (Synchronous)
│  │  ├─ SimplePortHandler class
│  │  ├─ list_ports()
│  │  ├─ get_port_count()
│  │  ├─ wait_for_insertion()
│  │  └─ wait_for_removal()
│  │
│  ├─ Advanced API (Threaded)
│  │  └─ PortMonitor class
│  │     ├─ startMonitoring()
│  │     ├─ stopMonitoring()
│  │     ├─ waitForInsert()
│  │     ├─ waitForRemoval()
│  │     └─ listPorts()
│  │
│  └─ namespace legacy (Backward Compatibility)
│     ├─ uart_wait_port_insert()
│     ├─ uart_wait_port_remove()
│     └─ uart_get_available_ports_number()
│
└─ [All implementations are inline/header-only]
```

## Quick Start

### Installation

Just include the header - that's it!

```cpp
#include "uart_monitor_complete.hpp"
```

### Basic Usage

```cpp
// List available ports
auto ports = uart::list_ports();
for (const auto& port : ports) {
    std::cout << port << "\n";
}

// Wait for device insertion (10 second timeout)
if (auto port = uart::wait_for_insertion(10000, 100)) {
    std::cout << "Device connected: " << *port << "\n";
}
```

## API Reference

### Simple API (Recommended for Most Use Cases)

#### Convenience Functions

```cpp
// Get list of ports
std::vector<std::string> uart::list_ports();

// Get port count
size_t uart::get_port_count();

// Wait for insertion
std::optional<std::string> uart::wait_for_insertion(
    uint32_t timeout_ms = 0,          // 0 = wait forever
    uint32_t polling_interval_ms = 100
);

// Wait for removal
std::optional<std::string> uart::wait_for_removal(
    uint32_t timeout_ms = 0,
    uint32_t polling_interval_ms = 100
);
```

#### SimplePortHandler Class

```cpp
uart::SimplePortHandler handler;

// Or with custom config:
uart::ScanConfig config{
    std::chrono::milliseconds{200},  // polling interval
    std::chrono::milliseconds{5000}  // timeout
};
uart::SimplePortHandler handler{config};

// Methods:
std::vector<std::string> get_available_ports();
size_t get_port_count();
std::optional<std::string> wait_for_insertion();
std::optional<std::string> wait_for_removal();
```

**When to use Simple API:**
- Quick scripts or utilities
- One-time port detection
- Simple wait operations
- No need for continuous monitoring

### Advanced API (For Complex Applications)

#### PortMonitor Class

```cpp
uart::PortMonitor monitor;

// Configuration
monitor.setPollingInterval(100);  // milliseconds

// Lifecycle
monitor.startMonitoring();  // Starts background thread
monitor.stopMonitoring();   // Stops and joins thread
bool isMonitoring();        // Check status

// Waiting for events
uart::PortWaitResult waitForInsert(
    std::optional<std::chrono::milliseconds> timeout = std::nullopt
);

uart::PortWaitResult waitForRemoval(
    std::optional<std::chrono::milliseconds> timeout = std::nullopt
);

// Other
std::vector<std::string> listPorts();
uint32_t countAvailablePorts();
```

**When to use Advanced API:**
- Long-running applications
- Need to detect multiple events
- Multiple threads waiting on events
- Continuous monitoring required

### Result Types

#### PortWaitResult

```cpp
struct PortWaitResult {
    WaitResult result;      // Success, Timeout, or Stopped
    std::string port_name;  // Port name (empty if not Success)
    
    explicit operator bool() const;  // true if Success
};

enum class WaitResult {
    Success,   // Port event detected
    Timeout,   // Timeout occurred
    Stopped    // Monitoring was stopped
};
```

**Usage:**

```cpp
auto result = monitor.waitForInsert(5s);

// Method 1: Switch on result type
switch (result.result) {
    case uart::WaitResult::Success:
        std::cout << "Port: " << result.port_name << "\n";
        break;
    case uart::WaitResult::Timeout:
        std::cout << "Timeout\n";
        break;
    case uart::WaitResult::Stopped:
        std::cout << "Monitoring stopped\n";
        break;
}

// Method 2: Simple bool check
if (result) {  // Checks for Success
    std::cout << "Port: " << result.port_name << "\n";
}
```

## Comparison: Simple vs Advanced API

| Feature | Simple API | Advanced API |
|---------|-----------|-------------|
| **Threading** | Blocks calling thread | Background thread |
| **Multiple Events** | One at a time | Queue of events |
| **Wait Strategy** | Polling loop | Condition variables |
| **Memory** | Minimal | Small overhead (thread + queues) |
| **Complexity** | Very simple | More features |
| **Use Case** | Scripts, simple apps | Long-running apps |

### Example Comparison

**Simple API:**
```cpp
// Blocks until port is inserted or timeout
if (auto port = uart::wait_for_insertion(10000, 100)) {
    use_port(*port);
}
```

**Advanced API:**
```cpp
uart::PortMonitor monitor;
monitor.startMonitoring();

// Can do other work while monitoring in background
do_other_work();

// Check for events when ready
if (auto result = monitor.waitForInsert(1s)) {
    use_port(result.port_name);
}

monitor.stopMonitoring();
```

## Thread Safety

### Simple API
- ❌ **Not thread-safe**: Don't share `SimplePortHandler` between threads
- ✅ **Thread-per-instance**: Each thread can have its own handler

### Advanced API
- ✅ **Thread-safe**: `PortMonitor` can be shared between threads
- ✅ **Multiple readers**: Multiple threads can call `waitForInsert()`/`waitForRemoval()`
- ✅ **Safe lifecycle**: `startMonitoring()`/`stopMonitoring()` are thread-safe

## Migration Guide

### From `uDeviceHandling.hpp`

**Old:**
```cpp
#include "uDeviceHandling.hpp"

DeviceHandling handler;
handler.init();

std::string output;
handler.process(port_name, output, OperationType::Insert);
```

**New:**
```cpp
#include "uart_monitor_complete.hpp"

// DeviceHandling is now internal - use high-level API
auto ports = uart::list_ports();
// or
if (auto port = uart::wait_for_insertion(5000, 100)) {
    // use port
}
```

### From `uUartMonitor.hpp` (buggy version)

**Old:**
```cpp
#include "uUartMonitor.hpp"

UartMonitor monitor;
monitor.setPollingInterval(200);  // Dangerous const function
monitor.startMonitoring();         // Can leak threads

std::string port = monitor.waitForInsert(5000ms);
// Ambiguous: timeout? stopped? error?
```

**New:**
```cpp
#include "uart_monitor_complete.hpp"

uart::PortMonitor monitor;
monitor.setPollingInterval(200);  // Safe, throws if monitoring
monitor.startMonitoring();         // Thread-safe, prevents duplicates

auto result = monitor.waitForInsert(5s);
switch (result.result) {
    case uart::WaitResult::Success: /* ... */ break;
    case uart::WaitResult::Timeout: /* ... */ break;
    case uart::WaitResult::Stopped: /* ... */ break;
}
```

### From `uUartMonitorExt.hpp`

**Old:**
```cpp
#include "uUartMonitorExt.hpp"

uart::PortHandler handler;
auto port = handler.wait_for_insertion();
```

**New:**
```cpp
#include "uart_monitor_complete.hpp"

// Same namespace, slightly different name
uart::SimplePortHandler handler;  // was PortHandler
auto port = handler.wait_for_insertion();

// Or use Advanced API for more features
uart::PortMonitor monitor;
monitor.startMonitoring();
auto result = monitor.waitForInsert(5s);
```

## Platform Support

### Windows
- Uses `QueryDosDeviceA` to enumerate COM1-COM255
- Header: `<windows.h>`

### Linux
- Uses `glob()` to match `/dev/ttyACM*` and `/dev/ttyUSB*`
- Header: `<glob.h>`
- Customizable patterns via `ScanConfig`

## Examples

See `uart_monitor_examples.cpp` for 13 comprehensive examples:

1. List ports
2. Count ports
3. Wait for insertion (simple)
4. Wait for removal (simple)
5. Custom configuration
6. Basic monitoring
7. Wait with detailed results
8. Continuous monitoring
9. Error handling
10. Thread safety demo
11. Legacy API compatibility
12. Wait for specific device
13. Auto-reconnect pattern

## Building

### Compilation

```bash
# Linux
g++ -std=c++17 -pthread uart_monitor_examples.cpp -o examples

# Windows (MinGW)
g++ -std=c++17 uart_monitor_examples.cpp -o examples.exe

# Windows (MSVC)
cl /EHsc /std:c++17 uart_monitor_examples.cpp
```

### Requirements

- **C++17** compiler or later
- **Linux**: POSIX glob support (standard)
- **Windows**: Windows SDK (for QueryDosDevice)

## Best Practices

### 1. Choose the Right API

```cpp
// For simple tasks - use Simple API
if (auto port = uart::wait_for_insertion(10000, 100)) {
    // Quick and easy
}

// For complex apps - use Advanced API
uart::PortMonitor monitor;
monitor.startMonitoring();
// Monitor can run for hours/days
```

### 2. Always Handle Timeouts

```cpp
// Simple API
if (auto port = uart::wait_for_insertion(5000, 100)) {
    // Got port
} else {
    // Handle timeout
}

// Advanced API
auto result = monitor.waitForInsert(5s);
if (result.result == uart::WaitResult::Timeout) {
    // Handle timeout
}
```

### 3. Stop Monitoring Properly

```cpp
uart::PortMonitor monitor;
monitor.startMonitoring();

// ... do work ...

// Always stop before destruction
monitor.stopMonitoring();  // Or destructor will do it
```

### 4. Exception Safety

```cpp
try {
    uart::PortMonitor monitor;
    monitor.startMonitoring();
    
    // ... work ...
    
    monitor.stopMonitoring();
} catch (const std::exception& e) {
    // Destructor still cleans up properly
}
```

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|----------------|-------|
| Port lookup | O(1) | Uses unordered_set |
| Port scan | O(n) | n = number of system ports (fixed, usually <256) |
| Insert detection | O(1) | Hash-based |
| Remove detection | O(m) | m = known ports (typically small) |
| Memory usage | O(k) | k = unique ports seen |

## Troubleshooting

### Linux: Permission Denied

```bash
# Add user to dialout group
sudo usermod -a -G dialout $USER
# Log out and back in

# Or run with sudo (not recommended)
sudo ./your_program
```

### Windows: No Ports Found

1. Check Device Manager for COM ports
2. Ensure device drivers are installed
3. Try running as Administrator

### Compilation Errors

```bash
# Ensure C++17
g++ -std=c++17 ...

# Update compiler if needed:
# GCC 7+, Clang 5+, MSVC 2017+
```

## License

SPDX-FileCopyrightText: Copyright (C) 2021-2026 Continental AG and subsidiaries  
SPDX-License-Identifier: LicenseRef-Continental-1.0

## Changelog

### Version 2.0.0 (2026-02-16)

- ✅ Merged multiple implementations into one
- ✅ Eliminated external dependencies
- ✅ Fixed all thread safety issues
- ✅ Added two-level API (Simple + Advanced)
- ✅ Improved error handling with result types
- ✅ Modern C++17 implementation
- ✅ Comprehensive documentation and examples

### Previous Versions

- v1.x: Original implementations (multiple files, had issues)
- v0.x: Initial development

## Contributing

When contributing:
1. Maintain C++17 compatibility
2. Keep header-only design
3. Add tests for new features
4. Update documentation
5. Follow existing code style
6. Ensure thread safety

## Summary

This complete implementation:
- **Consolidates** 3 files into 1
- **Fixes** all critical bugs from original
- **Improves** performance with better algorithms
- **Adds** two API levels for different needs
- **Maintains** backward compatibility
- **Provides** comprehensive examples
- **Is** production-ready and thread-safe

Use `uart_monitor_complete.hpp` for all new code. Legacy APIs are provided for compatibility only.
