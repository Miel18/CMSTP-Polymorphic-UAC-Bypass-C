#ifndef CMSTP_BYPASS_H
#define CMSTP_BYPASS_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// Return codes for the bypass function
typedef enum {
    CMSTP_SUCCESS = 0,
    CMSTP_ERROR_INVALID_PARAMS = 1,
    CMSTP_ERROR_FILE_CREATION = 2,
    CMSTP_ERROR_PROCESS_START = 3,
    CMSTP_ERROR_WINDOW_NOT_FOUND = 4,
    CMSTP_ERROR_CONFIRMATION_FAILED = 5,
    CMSTP_ERROR_TIMEOUT = 6
} CMSTPResult;

// Configuration structure for the bypass
typedef struct {
    const char* command;           // Command to execute (required)
    DWORD windowTimeout;          // Timeout for window detection in ms (default: 7500)
    DWORD confirmationDelay;      // Delay before confirmation in ms (default: 300)
    BOOL enableLogging;           // Enable logging output (default: TRUE)
    BOOL cleanupFiles;            // Auto-cleanup temp files (default: TRUE)
    const char* tempDirectory;    // Custom temp directory (optional, uses system temp if NULL)
} CMSTPConfig;

/**
 * Executes a command with elevated privileges using CMSTP bypass technique
 * 
 * @param config Configuration structure containing command and options
 * @return CMSTPResult indicating success or specific error type
 * 
 * Example usage:
 *   CMSTPConfig config = {0};
 *   config.command = "powershell.exe -Command \"Write-Host 'Hello World'\"";
 *   config.enableLogging = TRUE;
 *   
 *   CMSTPResult result = ExecuteWithCMSTPBypass(&config);
 *   if (result == CMSTP_SUCCESS) {
 *       printf("Command executed successfully\n");
 *   }
 */
CMSTPResult ExecuteWithCMSTPBypass(const CMSTPConfig* config);

/**
 * Initialize a CMSTPConfig structure with default values
 * 
 * @param config Pointer to config structure to initialize
 */
void InitializeCMSTPConfig(CMSTPConfig* config);

/**
 * Get a human-readable description of a CMSTPResult error code
 * 
 * @param result The result code to describe
 * @return Constant string describing the error
 */
const char* GetCMSTPResultDescription(CMSTPResult result);

#ifdef __cplusplus
}
#endif

#endif // CMSTP_BYPASS_H
