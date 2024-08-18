#include "pch.h"
#include <Python.h>
#define Py_LIMITED_API 0x03060000
#include <pybind11/pybind11.h>
#include <pybind11/embed.h>
#include <atomic>
#include <thread>
#include <iostream>
#include <filesystem>

#include <fstream>

namespace fs = std::filesystem;
namespace py = pybind11;
static std::atomic<bool> isRunning(false);
static std::thread interpreterThread;


std::string getCurrentModulePath() {
    char modulePath[MAX_PATH];
    HMODULE hModule = nullptr;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&getCurrentModulePath), &hModule)) {
        GetModuleFileNameA(hModule, modulePath, sizeof(modulePath));
    }
    return std::string(modulePath);
}

class ConfigParser {
public:
    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) return false;

        std::string line, currentSection;
        while (std::getline(file, line)) {
            // Remove comments and trim whitespace
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;

            // Check for section headers
            if (line.front() == '[' && line.back() == ']') {
                currentSection = line.substr(1, line.size() - 2);
                continue;
            }

            // Split key-value pairs
            size_t delimiterPos = line.find('=');
            if (delimiterPos != std::string::npos) {
                std::string key = trim(line.substr(0, delimiterPos));
                std::string value = trim(line.substr(delimiterPos + 1));

                // Prefix the key with the section name to avoid conflicts
                if (!currentSection.empty()) {
                    key = currentSection + "." + key;
                }
                configData[key] = value;
            }
        }
        return true;
    }

    std::string get(const std::string& key, const std::string& defaultValue = "") const {
        auto it = configData.find(key);
        return (it != configData.end()) ? it->second : defaultValue;
    }

private:
    std::unordered_map<std::string, std::string> configData;

    static std::string trim(const std::string& str) {
        const std::string whitespace = " \t\n\r";
        size_t start = str.find_first_not_of(whitespace);
        size_t end = str.find_last_not_of(whitespace);
        return (start == std::string::npos) ? "" : str.substr(start, end - start + 1);
    }
};

void interpreterFunction() {
    ConfigParser parser;
    std::string basePath = getCurrentModulePath();  // Path to the DLL
    fs::path configPath = fs::path(basePath).parent_path() / "PyFX.config";
    if (!parser.load(configPath.string())) {
        std::cerr << "Failed to load configuration file from " << configPath << std::endl;
    }

    // Get paths from the config
    std::string pythonHome = parser.get("Paths.python_home_dir");
    std::string libDir = parser.get("Paths.lib_dir");
    std::string sitePackages = parser.get("Paths.site_packages_dir");
    std::string pythonZipPath = pythonHome + "\\python311.zip"; // Path to the zip file

    // Set PYTHONHOME and PYTHONPATH environment variables
    std::string pythonPath = pythonZipPath + ";" + libDir + ";" + sitePackages;
    _putenv(("PYTHONHOME=" + pythonHome).c_str());
    _putenv(("PYTHONPATH=" + pythonPath).c_str());

    // Initialize the PyConfig structure
    PyConfig config;
    PyConfig_InitPythonConfig(&config);  // Initialize with default values

    // Set the home directory in PyConfig
    PyStatus status = PyConfig_SetString(&config, &config.home, std::wstring(pythonHome.begin(), pythonHome.end()).c_str());
    if (PyStatus_Exception(status)) {
        std::cerr << "Failed to set Python home directory: " << status.err_msg << std::endl;
        PyConfig_Clear(&config);
        throw std::runtime_error("Failed to set Python home directory");
    }

    // Add the zip file path to the sys.path
    status = PyWideStringList_Append(&config.module_search_paths, std::wstring(pythonZipPath.begin(), pythonZipPath.end()).c_str());
    if (PyStatus_Exception(status)) {
        std::cerr << "Failed to append Python module search paths: " << status.err_msg << std::endl;
        PyConfig_Clear(&config);
        throw std::runtime_error("Failed to append Python module search paths");
    }

    // Add other paths as necessary
    status = PyWideStringList_Append(&config.module_search_paths, std::wstring(sitePackages.begin(), sitePackages.end()).c_str());
    if (PyStatus_Exception(status)) {
        std::cerr << "Failed to append Python module search paths: " << status.err_msg << std::endl;
        PyConfig_Clear(&config);
        throw std::runtime_error("Failed to append Python module search paths");
    }

    // Initialize the interpreter with the custom configuration
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        std::cerr << "Failed to initialize Python interpreter: " << status.err_msg << std::endl;
        PyConfig_Clear(&config);
        throw std::runtime_error("Failed to initialize Python interpreter");
    }

    PyConfig_Clear(&config);

    try {
        while (isRunning) {
            {
                py::gil_scoped_release release;  // Release the GIL
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            // Additional Python-related operations can be added here if needed
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception occurred in Python interpreter thread: " << e.what() << std::endl;
    }
    catch (...) {
        std::cerr << "Unknown exception occurred in Python interpreter thread." << std::endl;
    }

    py::finalize_interpreter();  // Finalize the Python interpreter
}


bool pyfx::running()
{
    return isRunning;
}

void pyfx::start()
{
    if (!isRunning) {
        isRunning = true;
        interpreterThread = std::thread(interpreterFunction);
    }
}

void pyfx::stop()
{
    if (isRunning) {
        isRunning = false;
        if (interpreterThread.joinable()) {
            interpreterThread.join();
        }
    }
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        try {
            isRunning = true;
            interpreterThread = std::thread(interpreterFunction); // Start the interpreter thread
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to start interpreter thread: " << e.what() << std::endl;
            return FALSE;
        }
        break;

    case DLL_PROCESS_DETACH:
        isRunning = false;
        if (interpreterThread.joinable()) {
            interpreterThread.join(); // Ensure the thread completes before unloading the DLL
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        // Currently not handling thread attach/detach explicitly
        break;
    }
    return TRUE;
}
