/* ****************************************** 
   Changes history 
   Maximus5: ����� ��� static
   2009-01-31 Maximus5
	*) ���������� ��� ������ InfoW.Control() � ��� ���������� ���������� � ��������� ��������� �������.
	*) ACTL_GETWINDOWINFO �������� �� ����� ����������� ACTL_GETSHORTWINDOWINFO, ����� � �� ���� FREE ��������. ��� ���, ��� ��������� ������ �� ���� �� ������������. � ��� ������������, ����� ACTL_GETWINDOWINFO ���� �����������.
	*) ����� ECTL_GETINFO ���� �������� ECTL_FREEINFO.
****************************************** */

//#include <stdio.h>
#include <windows.h>
//#include <windowsx.h>
//#include <string.h>
//#include <tchar.h>
#include "..\common\common.hpp"
#include "..\common\pluginW789.hpp"
#include "PluginHeader.h"

#ifdef _DEBUG
#include <crtdbg.h>
#else
#define _ASSERT(f)
#define _ASSERTE(f)
#endif

extern "C" {
#include "../common/ConEmuCheck.h"
}

#if !defined(__GNUC__)
WARNING("����������, ��� � gszRootKey �� ����������� ��� ������������/������������");
#endif

#define MAKEFARVERSION(major,minor,build) ( ((major)<<8) | (minor) | ((build)<<16))

// minimal(?) FAR version 2.0 alpha build 757
int WINAPI _export GetMinFarVersionW(void)
{
	return MAKEFARVERSION(2,0,757);
}

/* COMMON - ���� ��������� �� ����������� */
void WINAPI _export GetPluginInfoW(struct PluginInfo *pi)
{
    static WCHAR *szMenu[1], szMenu1[255];
    szMenu[0]=szMenu1; //lstrcpyW(szMenu[0], L"[&\x2560] ConEmu"); -> 0x2584
    //szMenu[0][1] = L'&';
    //szMenu[0][2] = 0x2560;

	// ���������, �� ���������� �� ������� ������� �������, � ���� �� - ����������� �������
	IsKeyChanged(TRUE);

	if (gcPlugKey) szMenu1[0]=0; else lstrcpyW(szMenu1, L"[&\x2584] ");
	lstrcpynW(szMenu1+lstrlenW(szMenu1), GetMsgW(2), 240);


	pi->StructSize = sizeof(struct PluginInfo);
	pi->Flags = PF_EDITOR | PF_VIEWER | PF_DIALOG | PF_PRELOAD;
	pi->DiskMenuStrings = NULL;
	pi->DiskMenuNumbers = 0;
	pi->PluginMenuStrings = szMenu;
	pi->PluginMenuStringsNumber = 1;
	pi->PluginConfigStrings = NULL;
	pi->PluginConfigStringsNumber = 0;
	pi->CommandPrefix = NULL;
	pi->Reserved = 0;	
}

HANDLE WINAPI _export OpenPluginW(int OpenFrom,INT_PTR Item)
{
	if (gnReqCommand != (DWORD)-1) {
		ProcessCommand(gnReqCommand, FALSE/*bReqMainThread*/, gpReqCommandData);
	}
	return INVALID_HANDLE_VALUE;
}
/* COMMON - end */


HWND ConEmuHwnd=NULL; // �������� ����� ���� ���������. ��� �������� ����.
BOOL TerminalMode = FALSE;
HWND FarHwnd=NULL;
HANDLE hEventCmd[MAXCMDCOUNT], hEventAlive=NULL, hEventReady=NULL;
HANDLE hThread=NULL;
FarVersion gFarVersion;
WCHAR gszDir1[CONEMUTABMAX], gszDir2[CONEMUTABMAX];
WCHAR gszRootKey[MAX_PATH];
int maxTabCount = 0, lastWindowCount = 0, gnCurTabCount = 0;
ConEmuTab* tabs = NULL; //(ConEmuTab*) calloc(maxTabCount, sizeof(ConEmuTab));
LPBYTE gpData = NULL, gpCursor = NULL;
CESERVER_REQ* gpCmdRet = NULL;
DWORD  gnDataSize=0;
HANDLE ghMapping = NULL;
DWORD gnReqCommand = -1;
LPVOID gpReqCommandData = NULL;
HANDLE ghReqCommandEvent = NULL;
UINT gnMsgTabChanged = 0;
CRITICAL_SECTION csTabs, csData;
WCHAR gcPlugKey=0;
BOOL  gbPlugKeyChanged=FALSE;
HKEY  ghRegMonitorKey=NULL; HANDLE ghRegMonitorEvt=NULL;
HMODULE ghFarHintsFix = NULL;
WCHAR gszPluginServerPipe[MAX_PATH];
#define MAX_SERVER_THREADS 3
HANDLE ghServerThreads[MAX_SERVER_THREADS] = {NULL,NULL,NULL};
HANDLE ghActiveServerThread = NULL;
DWORD  gnServerThreadsId[MAX_SERVER_THREADS] = {0,0,0};
HANDLE ghServerTerminateEvent = NULL;
HANDLE ghServerSemaphore = NULL;

#if defined(__GNUC__)
typedef HWND (APIENTRY *FGetConsoleWindow)();
FGetConsoleWindow GetConsoleWindow = NULL;
#endif
extern void SetConsoleFontSizeTo(HWND inConWnd, int inSizeX, int inSizeY);

BOOL APIENTRY DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved )
{
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH:
			{
				#ifdef _DEBUG
				//if (!IsDebuggerPresent()) MessageBoxA(GetForegroundWindow(), "ConEmu.dll loaded", "ConEmu", 0);
				#endif
				#if defined(__GNUC__)
				GetConsoleWindow = (FGetConsoleWindow)GetProcAddress(GetModuleHandle(L"kernel32.dll"),"GetConsoleWindow");
				#endif
				HWND hConWnd = GetConsoleWindow();
				InitHWND(hConWnd);
				
			    // Check Terminal mode
			    TCHAR szVarValue[MAX_PATH];
			    szVarValue[0] = 0;
			    if (GetEnvironmentVariable(L"TERM", szVarValue, 63)) {
				    TerminalMode = TRUE;
			    }
			    
			    if (!TerminalMode) {
					// FarHints fix for multiconsole mode...
					if (GetModuleFileName((HMODULE)hModule, szVarValue, MAX_PATH)) {
						WCHAR *pszSlash = wcsrchr(szVarValue, L'\\');
						if (pszSlash) pszSlash++; else pszSlash = szVarValue;
						lstrcpyW(pszSlash, L"infis.dll");
						ghFarHintsFix = LoadLibrary(szVarValue);
					}
			    }
			}
			break;
		case DLL_PROCESS_DETACH:
			if (ghFarHintsFix) {
				FreeLibrary(ghFarHintsFix);
				ghFarHintsFix = NULL;
			}
			break;
	}
	return TRUE;
}

#if defined(__GNUC__)
#ifdef __cplusplus
extern "C"{
#endif
  BOOL WINAPI DllMainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved);
  HWND WINAPI GetFarHWND();
  HWND WINAPI GetFarHWND2(BOOL abConEmuOnly);
  void WINAPI GetFarVersion ( FarVersion* pfv );
  int  WINAPI ProcessEditorInputW(void* Rec);
  void WINAPI SetStartupInfoW(void *aInfo);
  BOOL WINAPI IsTerminalMode();
  BOOL WINAPI IsConsoleActive();
#ifdef __cplusplus
};
#endif

BOOL WINAPI DllMainCRTStartup(HANDLE hDll,DWORD dwReason,LPVOID lpReserved)
{
  /*(void) lpReserved;
  (void) dwReason;
  (void) hDll;*/
  return DllMain(hDll, dwReason,lpReserved);
}
#endif

BOOL WINAPI IsConsoleActive()
{
	if (ConEmuHwnd) {
		if (IsWindow(ConEmuHwnd)) {
			HWND hParent = GetParent(ConEmuHwnd);
			if (hParent) {
				HWND hTest = (HWND)GetWindowLong(hParent, GWL_USERDATA);
				return (hTest == FarHwnd);
			}
		}
	}
	return TRUE;
}

HWND WINAPI GetFarHWND2(BOOL abConEmuOnly)
{
	if (ConEmuHwnd) {
		if (IsWindow(ConEmuHwnd))
			return ConEmuHwnd;
		ConEmuHwnd = NULL;
	}
	if (abConEmuOnly)
		return NULL;
	return FarHwnd;
}

HWND WINAPI _export GetFarHWND()
{
    return GetFarHWND2(FALSE);
}

BOOL WINAPI IsTerminalMode()
{
    return TerminalMode;
}

void WINAPI _export GetFarVersion ( FarVersion* pfv )
{
	if (!pfv)
		return;

	*pfv = gFarVersion;
}

BOOL LoadFarVersion()
{
    BOOL lbRc=FALSE;
    WCHAR FarPath[MAX_PATH+1];
    if (GetModuleFileName(0,FarPath,MAX_PATH)) {
		DWORD dwRsrvd = 0;
		DWORD dwSize = GetFileVersionInfoSize(FarPath, &dwRsrvd);
		if (dwSize>0) {
			void *pVerData = calloc(dwSize, 1);
			if (pVerData) {
				VS_FIXEDFILEINFO *lvs = NULL;
				UINT nLen = sizeof(lvs);
				if (GetFileVersionInfo(FarPath, 0, dwSize, pVerData)) {
					TCHAR szSlash[3]; lstrcpyW(szSlash, L"\\");
					if (VerQueryValue ((void*)pVerData, szSlash, (void**)&lvs, &nLen)) {
						gFarVersion.dwVer = lvs->dwFileVersionMS;
						gFarVersion.dwBuild = lvs->dwFileVersionLS;
						lbRc = TRUE;
					}
				}
				free(pVerData);
			}
		}
	}

	if (!lbRc) {
		gFarVersion.dwVerMajor = 2;
		gFarVersion.dwVerMinor = 0;
		gFarVersion.dwBuild = 789;
	}

	return lbRc;
}

BOOL IsKeyChanged(BOOL abAllowReload)
{
	BOOL lbKeyChanged = FALSE;
	if (ghRegMonitorEvt) {
		if (WaitForSingleObject(ghRegMonitorEvt, 0) == WAIT_OBJECT_0) {
			lbKeyChanged = CheckPlugKey();
			if (lbKeyChanged) gbPlugKeyChanged = TRUE;
		}
	}

	if (abAllowReload && gbPlugKeyChanged) {
		// ������-�� ��� �� �������� � ������� ����...
		CheckMacro(TRUE);
		gbPlugKeyChanged = FALSE;
	}
	return lbKeyChanged;
}

void ProcessCommand(DWORD nCmd, BOOL bReqMainThread, LPVOID pCommandData)
{
	if (gpCmdRet) free(gpCmdRet);
	gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;

	if (bReqMainThread) {
		gnReqCommand = nCmd;
		gpReqCommandData = pCommandData;
		if (!ghReqCommandEvent) {
			ghReqCommandEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
			if (!ghReqCommandEvent) return;
		}
		ResetEvent(ghReqCommandEvent);
		
		// ���������, �� ���������� �� ������� ������� �������, � ���� �� - ����������� �������
		IsKeyChanged(TRUE);



		// ����� ����� ������� � ��������� ����
		WARNING("���������� �� WriteConsoleInput");
		SendMessage(FarHwnd, WM_KEYDOWN, VK_F14, 0);
		SendMessage(FarHwnd, WM_KEYUP, VK_F14, (LPARAM)(3<<30));

		
		HANDLE hEvents[2] = {ghReqCommandEvent, hEventCmd[CMD_EXIT]};
		//DuplicateHandle(GetCurrentProcess(), ghReqCommandEvent, GetCurrentProcess(), hEvents, 0, 0, DUPLICATE_SAME_ACCESS);
		DWORD dwWait = WaitForMultipleObjects ( 2, hEvents, FALSE, CONEMUFARTIMEOUT );
		if (dwWait) ResetEvent(ghReqCommandEvent); // ����� �������, ����� �� ���������?
		//CloseHandle(hEvents[0]);

		gpReqCommandData = NULL;
		gnReqCommand = -1;
		return;
	}
	
	/*if (gbPlugKeyChanged) {
		gbPlugKeyChanged = FALSE;
		CheckMacro(TRUE);
		gbPlugKeyChanged = FALSE;
	}*/

	EnterCriticalSection(&csData);

	switch (nCmd)
	{
		case (CMD_DRAGFROM):
		{
			if (gFarVersion.dwVerMajor==1)
				ProcessDragFromA();
			else if (gFarVersion.dwBuild>=789)
				ProcessDragFrom789();
			else
				ProcessDragFrom757();
			break;
		}
		case (CMD_DRAGTO):
		{
			if (gFarVersion.dwVerMajor==1)
				ProcessDragToA();
			else if (gFarVersion.dwBuild>=789)
				ProcessDragTo789();
			else
				ProcessDragTo757();
			break;
		}
		case (CMD_SETWINDOW):
		{
			int nTab = 0;
			if (pCommandData) {
				nTab = *((DWORD*)pCommandData);
			} else {
				nTab = GetWindowLong(ConEmuHwnd, GWL_TABINDEX);
			}
			if (gFarVersion.dwVerMajor==1)
				SetWindowA(nTab);
			else if (gFarVersion.dwBuild>=789)
				SetWindow789(nTab);
			else
				SetWindow757(nTab);
			break;
		}
		case (CMD_POSTMACRO):
		{
			if (pCommandData) {
				PostMacro((wchar_t*)pCommandData);
			} else {
				HKEY hKey = NULL;
				if (0==RegOpenKeyEx(HKEY_CURRENT_USER, L"Software\\ConEmu", 0, KEY_ALL_ACCESS, &hKey))
				{
					DWORD dwSize = 0;
					if (0==RegQueryValueEx(hKey, L"PostMacroString", NULL, NULL, NULL, &dwSize)) {
						wchar_t* pszMacro = (wchar_t*)calloc(dwSize+2,1);
						if (0==RegQueryValueEx(hKey, L"PostMacroString", NULL, NULL, (LPBYTE)pszMacro, &dwSize)) {
							PostMacro(pszMacro);
						}
						RegDeleteValue(hKey, L"PostMacroString");
					}
					RegCloseKey(hKey);
				}
			}
			break;
		}
	}

	LeaveCriticalSection(&csData);

	if (ghReqCommandEvent)
		SetEvent(ghReqCommandEvent);
}

// ��� ���� ����� ��������, ����� ���� ����������� ���������� ������� ��� ������� ConEmu
DWORD WINAPI ThreadProcW(LPVOID lpParameter)
{
	DWORD dwProcId = GetCurrentProcessId();

	while (true)
	{
		DWORD dwWait = 0;
		DWORD dwTimeout = 1000;
		/*#ifdef _DEBUG
		dwTimeout = INFINITE;
		#endif*/

		dwWait = WaitForMultipleObjects(MAXCMDCOUNT, hEventCmd, FALSE, dwTimeout);

		// ������������, ���� ��������� ����� ����������� � ��� ConEmuHwnd (��� ��������)
	    if (ConEmuHwnd && FarHwnd && (dwWait>=(WAIT_OBJECT_0+MAXCMDCOUNT))) {
			// ��� ���� ������ Detach! (CtrlAltTab)
			HWND hConWnd = GetConsoleWindow();
			
		    if (!IsWindow(ConEmuHwnd) || hConWnd!=FarHwnd) {
			    ConEmuHwnd = NULL;

				if (hConWnd!=FarHwnd || !IsWindow(FarHwnd))
				{
					if (hConWnd != FarHwnd && IsWindow(hConWnd)) {
						FarHwnd = hConWnd;
						int nBtn = ShowMessage(1, 2);
						if (nBtn == 0) {
							// Create process, with flag /Attach GetCurrentProcessId()
							// Sleep for sometimes, try InitHWND(hConWnd); several times
							WCHAR  szExe[0x200];
							WCHAR  szCurArgs[0x600];
							
							PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
							STARTUPINFO si; memset(&si, 0, sizeof(si));
							si.cb = sizeof(si);
							
							//TODO: ConEmu.exe
							int nLen = 0;
							if ((nLen=GetModuleFileName(0, szExe, 0x190))==0) {
								goto closethread;
							}
							WCHAR* pszSlash = szExe+nLen-1;
			                while (pszSlash>szExe && *(pszSlash-1)!=L'\\') pszSlash--;
			                lstrcpyW(pszSlash, L"ConEmu.exe");
			                
							DWORD dwPID = GetCurrentProcessId();
							wsprintf(szCurArgs, L"\"%s\" /Attach %i ", szExe, dwPID);
							
							GetEnvironmentVariableW(L"ConEmuArgs", szCurArgs+lstrlenW(szCurArgs), 0x380);
			                
							
							if (!CreateProcess(NULL, szCurArgs, NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL,
									NULL, &si, &pi))
							{
								// ������ �� ������ ��������?
								goto closethread;
							}
							
							// ����
							while (TRUE)
							{
								dwWait = WaitForSingleObject(hEventCmd[CMD_EXIT], 200);
								// ��� �����������
								if (dwWait == WAIT_OBJECT_0) {
									CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
									goto closethread;
								}
								if (!GetExitCodeProcess(pi.hProcess, &dwPID) || dwPID!=STILL_ACTIVE) {
									CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
									goto closethread;
								}
								InitHWND(hConWnd);
								if (ConEmuHwnd) {
									// ����������, �� ����� ConEmu ������ ����� �������� �� ����...
									SetConsoleFontSizeTo(FarHwnd, 4, 6);
									MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...
									break;
								}
							}
							
							// ������� ������
							CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
							// ������ �� ����� ���� ��������...
							SendTabs(gnCurTabCount, TRUE);
							continue;
						} else {
							// ������������ ���������, ������� �� ���� ���������
							goto closethread;
						}
					} else {
						MessageBox(0, L"ConEmu was abnormally termintated!\r\nExiting from FAR", L"ConEmu plugin", MB_OK|MB_ICONSTOP|MB_SETFOREGROUND);
						TerminateProcess(GetCurrentProcess(), 100);
					}
				}
				else 
				{
					//if (bWasSetParent) { -- ��� ����� ��� �� �������, ���� ��� ���� �������� - ������
					//	SetParent(FarHwnd, NULL);
					//}
				    
					ShowWindowAsync(FarHwnd, SW_SHOWNORMAL);
					EnableWindow(FarHwnd, true);
				}
			    
				goto closethread;
		    }
	    }

		if (dwWait == (WAIT_OBJECT_0+CMD_EXIT))
		{
			goto closethread;
		}

		if (dwWait>=(WAIT_OBJECT_0+MAXCMDCOUNT))
		{
			continue;
		}

		if (!ConEmuHwnd) {
			// ConEmu ����� �����������
			//int nChk = 0;
			ConEmuHwnd = GetConEmuHWND ( FALSE/*abRoot*/  /*, &nChk*/ );
		}

		SafeCloseHandle(ghMapping);
		// �������� ������, ��� �� ���������� � ���������
		// ���� ���� � �� ����������� �� ����, � ������������ � ������� ����, �� �������� ��� ������� �������
		//if (dwWait != (WAIT_OBJECT_0+CMD_REQTABS)) // ���������� - ������ �����. �� �����������
		SetEvent(hEventAlive);


		switch (dwWait)
		{
			case (WAIT_OBJECT_0+CMD_REQTABS):
			{
				/*if (!gnCurTabCount || !tabs) {
					CreateTabs(1);
					int nTabs=0; --�����! ��� ������ ���� ����� CriticalSection
					AddTab(nTabs, false, false, WTYPE_PANELS, 0,0,1,0); 
				}*/
				SendTabs(gnCurTabCount, TRUE);
				// ��������� ���� ������� �������
				//// ���������� - ������ �����. �� �����������
				//continue;
				break;
			}
			
			case (WAIT_OBJECT_0+CMD_DEFFONT):
			{
				// ���������� - �����������, ��������� �� ���������
				ResetEvent(hEventAlive);
				SetConsoleFontSizeTo(FarHwnd, 4, 6);
				MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...
				continue;
			}
			
			case (WAIT_OBJECT_0+CMD_LANGCHANGE):
			{
				// ���������� - �����������, ��������� �� ���������
				ResetEvent(hEventAlive);
				HKL hkl = (HKL)GetWindowLong(ConEmuHwnd, GWL_LANGCHANGE);
				if (hkl) {
					DWORD dwLastError = 0;
					WCHAR szLoc[10]; wsprintf(szLoc, L"%08x", (DWORD)(((DWORD)hkl) & 0xFFFF));
					HKL hkl1 = LoadKeyboardLayout(szLoc, KLF_ACTIVATE|KLF_REORDER|KLF_SUBSTITUTE_OK|KLF_SETFORPROCESS);
					HKL hkl2 = ActivateKeyboardLayout(hkl1, KLF_SETFORPROCESS|KLF_REORDER);
					if (!hkl2) {
						dwLastError = GetLastError();
					}
					dwLastError = dwLastError;
				}
				continue;
			}
			
			default:
			// ��� ��������� ������� ����� ��������� � ���� FAR'�
			ProcessCommand(dwWait/*nCmd*/, TRUE/*bReqMainThread*/, NULL);
		}

		// ������������� � �������� ������
		EnterCriticalSection(&csData);
		wsprintf(gszDir1, CONEMUMAPPING, dwProcId);
		gnDataSize = gpData ? (gpCursor - gpData) : 0;
		#ifdef _DEBUG
		int nSize = gnDataSize; // ����-�� ��� ����������...
		#endif
		SetLastError(0);
		ghMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, gnDataSize+4, gszDir1);
		#ifdef _DEBUG
		DWORD dwCreateError = GetLastError();
		#endif
		if (ghMapping) {
			LPBYTE lpMap = (LPBYTE)MapViewOfFile(ghMapping, FILE_MAP_ALL_ACCESS, 0,0,0);
			if (!lpMap) {
				// ������
				SafeCloseHandle(ghMapping);
			} else {
				// copy data
				if (gpData && gnDataSize) {
					*((DWORD*)lpMap) = gnDataSize+4;
					#ifdef _DEBUG
					LPBYTE dst=lpMap+4; LPBYTE src=gpData;
					for (DWORD n=gnDataSize;n>0;n--)
						*(dst++) = *(src++);
					#else
					memcpy(lpMap+4, gpData, gnDataSize);
					#endif
				} else {
					*((DWORD*)lpMap) = 0;
				}

				//unmaps a mapped view of a file from the calling process's address space
				UnmapViewOfFile(lpMap);
			}
		}
		if (gpData) {
			free(gpCmdRet);
			gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;
		}
		LeaveCriticalSection(&csData);

		// �������� ������, ��� ������ ��� � ��������
		SetEvent(hEventReady);

		//Sleep(1);
	}


closethread:
	// ��������� ��� ������ � �������
	for (int i=0; i<MAXCMDCOUNT; i++)
		SafeCloseHandle(hEventCmd[i]);
	SafeCloseHandle(hEventAlive);
	SafeCloseHandle(hEventReady);
	SafeCloseHandle(ghMapping);

	return 0;
}

void WINAPI _export SetStartupInfoW(void *aInfo)
{
	if (!gFarVersion.dwVerMajor) LoadFarVersion();

	if (gFarVersion.dwBuild>=789)
		SetStartupInfoW789(aInfo);
	else
		SetStartupInfoW757(aInfo);

	CheckMacro(TRUE);
}

#define CREATEEVENT(fmt,h) \
		wsprintf(szEventName, fmt, dwCurProcId ); \
		h = CreateEvent(NULL, FALSE, FALSE, szEventName); \
		if (h==INVALID_HANDLE_VALUE) h=NULL;
	
void InitHWND(HWND ahFarHwnd)
{
	InitializeCriticalSection(&csTabs);
	InitializeCriticalSection(&csData);
	LoadFarVersion(); // ���������� ��� �����!

	// ��������� �������������. � SetStartupInfo ��������
	wsprintfW(gszRootKey, L"Software\\%s", (gFarVersion.dwVerMajor==2) ? L"FAR2" : L"FAR");

	ConEmuHwnd = NULL;
	FarHwnd = ahFarHwnd;

	memset(hEventCmd, 0, sizeof(HANDLE)*MAXCMDCOUNT);
	
	//int nChk = 0;
	ConEmuHwnd = GetConEmuHWND ( FALSE/*abRoot*/  /*, &nChk*/ );

	gnMsgTabChanged = RegisterWindowMessage(CONEMUTABCHANGED);

	// ���� ���� �� �� � ConEmu - ��� ����� ��������� ����, �.�. � ConEmu ������ ���� ����������� /Attach!
	WCHAR szEventName[128];
	DWORD dwCurProcId = GetCurrentProcessId();

	// ��������� ������ ������
	wsprintf(gszPluginServerPipe, CEPLUGINPIPENAME, L".", dwCurProcId);
	ghServerTerminateEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
	_ASSERTE(ghServerTerminateEvent!=NULL);
	if (ghServerTerminateEvent) ResetEvent(ghServerTerminateEvent);
	ghServerSemaphore = CreateSemaphore(NULL, 1, 1, NULL);
    for (int i=0; i<MAX_SERVER_THREADS; i++) {
        gnServerThreadsId[i] = 0;
        ghServerThreads[i] = CreateThread(NULL, 0, ServerThread, (LPVOID)NULL, 0, &gnServerThreadsId[i]);
        _ASSERTE(ghServerThreads[i]!=NULL);
    }


	// ���� �������
	CREATEEVENT(CONEMUDRAGFROM, hEventCmd[CMD_DRAGFROM]);
	CREATEEVENT(CONEMUDRAGTO, hEventCmd[CMD_DRAGTO]);
	CREATEEVENT(CONEMUREQTABS, hEventCmd[CMD_REQTABS]);
	CREATEEVENT(CONEMUSETWINDOW, hEventCmd[CMD_SETWINDOW]);
	CREATEEVENT(CONEMUPOSTMACRO, hEventCmd[CMD_POSTMACRO]);
	CREATEEVENT(CONEMUDEFFONT, hEventCmd[CMD_DEFFONT]);
	CREATEEVENT(CONEMULANGCHANGE, hEventCmd[CMD_LANGCHANGE]);
	CREATEEVENT(CONEMUEXIT, hEventCmd[CMD_EXIT]);
	CREATEEVENT(CONEMUALIVE, hEventAlive);
	CREATEEVENT(CONEMUREADY, hEventReady);

	hThread=CreateThread(NULL, 0, &ThreadProcW, 0, 0, 0);

	// ���� �� �� ��� ���������� - ������ ������ ������ �� �����
	if (ConEmuHwnd) {
		//
		DWORD dwPID, dwThread;
		dwThread = GetWindowThreadProcessId(ConEmuHwnd, &dwPID);
		typedef BOOL (WINAPI* AllowSetForegroundWindowT)(DWORD);
		HMODULE hUser32 = GetModuleHandle(L"user32.dll");
		if (hUser32) {
			AllowSetForegroundWindowT AllowSetForegroundWindowF = (AllowSetForegroundWindowT)GetProcAddress(hUser32, "AllowSetForegroundWindow");
			if (AllowSetForegroundWindowF) AllowSetForegroundWindowF(dwPID);
		}
		// ������� ����, ���� ��� �����
		int tabCount = 0;
		CreateTabs(1);
		AddTab(tabCount, true, false, WTYPE_PANELS, NULL, NULL, 0, 0);
		SendTabs(tabCount=1);
	}
}

void NotifyChangeKey()
{
	if (ghRegMonitorKey) { RegCloseKey(ghRegMonitorKey); ghRegMonitorKey = NULL; }
	if (ghRegMonitorEvt) ResetEvent(ghRegMonitorEvt);
	
	WCHAR szKeyName[MAX_PATH*2];
	lstrcpyW(szKeyName, gszRootKey);
	lstrcatW(szKeyName, L"\\PluginHotkeys");
	// ����� ����� � �� ����, ���� �� ��� ������ ������� �� ���� ���������������� ������� �������
	if (0 == RegOpenKeyEx(HKEY_CURRENT_USER, szKeyName, 0, KEY_NOTIFY, &ghRegMonitorKey)) {
		if (!ghRegMonitorEvt) ghRegMonitorEvt = CreateEvent(NULL,FALSE,FALSE,NULL);
		RegNotifyChangeKeyValue(ghRegMonitorKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, ghRegMonitorEvt, TRUE);
		return;
	}
	// ���� �� ���� ��� - ������� ����������� � ��������� �����
	if (0 == RegOpenKeyEx(HKEY_CURRENT_USER, gszRootKey, 0, KEY_NOTIFY, &ghRegMonitorKey)) {
		if (!ghRegMonitorEvt) ghRegMonitorEvt = CreateEvent(NULL,FALSE,FALSE,NULL);
		RegNotifyChangeKeyValue(ghRegMonitorKey, TRUE, REG_NOTIFY_CHANGE_LAST_SET, ghRegMonitorEvt, TRUE);
		return;
	}
}

//abCompare=TRUE ���������� ����� �������� �������, ���� ���� ������� ������� �������...
BOOL CheckPlugKey()
{
	WCHAR cCurKey = gcPlugKey;
	gcPlugKey = 0;
	BOOL lbChanged = FALSE;
	HKEY hkey=NULL;
	WCHAR szMacroKey[2][MAX_PATH], szCheckKey[32];
	
	//��������� ����������� �������� �������, � ���� ��� ConEmu.dll ������� ������� ��������� - ��������� ��
	wsprintfW(szMacroKey[0], L"%s\\PluginHotkeys", gszRootKey/*, szCheckKey*/);
	if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[0], 0, KEY_READ, &hkey))
	{
		DWORD dwIndex = 0, dwSize; FILETIME ft;
		while (0==RegEnumKeyEx(hkey, dwIndex++, szMacroKey[1], &(dwSize=MAX_PATH), NULL, NULL, NULL, &ft)) {
			WCHAR* pszSlash = szMacroKey[1]+lstrlenW(szMacroKey[1])-1;
			while (pszSlash>szMacroKey[1] && *pszSlash!=L'/') pszSlash--;
			#pragma warning(disable : 6400)
			if (lstrcmpiW(pszSlash, L"/conemu.dll")==0) {
				WCHAR lsFullPath[MAX_PATH*2];
				lstrcpy(lsFullPath, szMacroKey[0]);
				lstrcat(lsFullPath, L"\\");
				lstrcat(lsFullPath, szMacroKey[1]);

				RegCloseKey(hkey); hkey=NULL;

				if (0==RegOpenKeyExW(HKEY_CURRENT_USER, lsFullPath, 0, KEY_READ, &hkey)) {
					dwSize = sizeof(szCheckKey);
					if (0==RegQueryValueExW(hkey, L"Hotkey", NULL, NULL, (LPBYTE)szCheckKey, &dwSize)) {
						gcPlugKey = szCheckKey[0];
					}
				}
				//
				//
				break;
			}
		}

		// ���������
		if (hkey) {RegCloseKey(hkey); hkey=NULL;}
	}
	
	lbChanged = (gcPlugKey != cCurKey);
	
	return lbChanged;
}

void CheckMacro(BOOL abAllowAPI)
{
	// � �� ��� ���������� ����� ��������� �������, ����� ����� ��������� �� ���������...
	//// ���� �� �� ��� ���������� - ������ ������ ������ �� �����
	//if (!ConEmuHwnd) return;


	// �������� ������� �������
	BOOL lbMacroAdded = FALSE, lbNeedMacro = FALSE;
	HKEY hkey=NULL;
	#define MODCOUNT 4
	int n;
	char szValue[1024];
	WCHAR szMacroKey[MODCOUNT][MAX_PATH], szCheckKey[32];
	DWORD dwSize = 0;
	//bool lbMacroDontCheck = false;

	//��������� ����������� �������� �������, � ���� ��� ConEmu.dll ������� ������� ��������� - ��������� ��
	CheckPlugKey();
	//wsprintfW(szMacroKey[0], L"%s\\PluginHotkeys",
	//		gszRootKey, szCheckKey);
	//if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[0], 0, KEY_READ, &hkey))
	//{
	//	DWORD dwIndex = 0, dwSize; FILETIME ft;
	//	while (0==RegEnumKeyEx(hkey, dwIndex++, szMacroKey[1], &(dwSize=MAX_PATH), NULL, NULL, NULL, &ft)) {
	//		WCHAR* pszSlash = szMacroKey[1]+lstrlenW(szMacroKey[1])-1;
	//		while (pszSlash>szMacroKey[1] && *pszSlash!=L'/') pszSlash--;
	//		if (lstrcmpiW(pszSlash, L"/conemu.dll")==0) {
	//			WCHAR lsFullPath[MAX_PATH*2];
	//			lstrcpy(lsFullPath, szMacroKey[0]);
	//			lstrcat(lsFullPath, L"\\");
	//			lstrcat(lsFullPath, szMacroKey[1]);
	//
	//			RegCloseKey(hkey); hkey=NULL;
	//
	//			if (0==RegOpenKeyExW(HKEY_CURRENT_USER, lsFullPath, 0, KEY_READ, &hkey)) {
	//				dwSize = sizeof(szCheckKey);
	//				if (0==RegQueryValueExW(hkey, L"Hotkey", NULL, NULL, (LPBYTE)szCheckKey, &dwSize)) {
	//					if (gFarVersion.dwVerMajor==1) {
	//						char cAnsi; // ����� �� �������� ������� � ����������� � WCHAR
	//						WideCharToMultiByte(CP_OEMCP, 0, szCheckKey, 1, &cAnsi, 1, 0,0);
	//						gcPlugKey = cAnsi;
	//					} else {
	//						gcPlugKey = szCheckKey[0];
	//					}
	//				}
	//			}
	//			//
	//			//
	//			break;
	//		}
	//	}
	//
	//	// ���������
	//	if (hkey) {RegCloseKey(hkey); hkey=NULL;}
	//}


	for (n=0; n<MODCOUNT; n++) {
		switch(n){
			case 0: lstrcpyW(szCheckKey, L"F14"); break;
			case 1: lstrcpyW(szCheckKey, L"CtrlF14"); break;
			case 2: lstrcpyW(szCheckKey, L"AltF14"); break;
			case 3: lstrcpyW(szCheckKey, L"ShiftF14"); break;
		}
		wsprintfW(szMacroKey[n], L"%s\\KeyMacros\\Common\\%s", gszRootKey, szCheckKey);
	}
	//lstrcpyA(szCheckKey, "F14DontCheck2");

	//if (0==RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\ConEmu", 0, KEY_ALL_ACCESS, &hkey))
	//{
	//
	//	if (RegQueryValueExA(hkey, szCheckKey, 0, 0, (LPBYTE)&lbMacroDontCheck, &(dwSize=sizeof(lbMacroDontCheck))))
	//		lbMacroDontCheck = false;
	//	RegCloseKey(hkey); hkey=NULL;
	//}

	/*if (gFarVersion.dwVerMajor==1) {
		lstrcpyA((char*)szCheckKey, "F11  ");
		((char*)szCheckKey)[4] = (char)((gcPlugKey ? gcPlugKey : 0xCC) & 0xFF);
		//lstrcpyW((wchar_t*)szCheckKey, L"F11  ");
		//szCheckKey[4] = (wchar_t)(gcPlugKey ? gcPlugKey : 0xCC);
	} else {*/
	lstrcpyW((wchar_t*)szCheckKey, L"F11  "); //TODO: ��� ANSI ����� ������ ��� �� ���������?
	szCheckKey[4] = (wchar_t)(gcPlugKey ? gcPlugKey : ((gFarVersion.dwVerMajor==1) ? 0x42C/*0xDC - ������ ��� OEM*/ : 0x2584));
	//}

	//if (!lbMacroDontCheck)
	for (n=0; n<MODCOUNT && !lbNeedMacro; n++)
	{
		if (0==RegOpenKeyExW(HKEY_CURRENT_USER, szMacroKey[n], 0, KEY_READ, &hkey))
		{
			/*if (gFarVersion.dwVerMajor==1) {
				if (0!=RegQueryValueExA(hkey, "Sequence", 0, 0, (LPBYTE)szValue, &(dwSize=1022))) {
					lbNeedMacro = TRUE; // �������� ����������
				} else {
					lbNeedMacro = lstrcmpA(szValue, (char*)szCheckKey)!=0;
				}
			} else {*/
				if (0!=RegQueryValueExW(hkey, L"Sequence", 0, 0, (LPBYTE)szValue, &(dwSize=1022))) {
					lbNeedMacro = TRUE; // �������� ����������
				} else {
					//TODO: ���������, ��� ���� ����� VC & GCC �� 2� �������� ��������?
					lbNeedMacro = lstrcmpW((WCHAR*)szValue, szCheckKey)!=0;
				}
			//}
			//	szValue[dwSize]=0;
			//	#pragma message("ERROR: ����� ��������. � Ansi � Unicode ��� ������ ������!")
			//	//if (strcmpW(szValue, "F11 \xCC")==0)
			//		lbNeedMacro = TRUE; // �������� ������������
			//}
			RegCloseKey(hkey); hkey=NULL;
		} else {
			lbNeedMacro = TRUE;
		}
	}

	if (lbNeedMacro) {
		//int nBtn = ShowMessage(0, 3);
		//if (nBtn == 1) { // Don't disturb
		//	DWORD disp=0;
		//	lbMacroDontCheck = true;
		//	if (0==RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\ConEmu", 0, 0, 
		//		0, KEY_ALL_ACCESS, 0, &hkey, &disp))
		//	{
		//		RegSetValueExA(hkey, szCheckKey, 0, REG_BINARY, (LPBYTE)&lbMacroDontCheck, (dwSize=sizeof(lbMacroDontCheck)));
		//		RegCloseKey(hkey); hkey=NULL;
		//	}
		//} else if (nBtn == 0) 
		for (n=0; n<MODCOUNT; n++)
		{
			DWORD disp=0;
			lbMacroAdded = TRUE;
			if (0==RegCreateKeyExW(HKEY_CURRENT_USER, szMacroKey[n], 0, 0, 
				0, KEY_ALL_ACCESS, 0, &hkey, &disp))
			{
				lstrcpyA(szValue, "ConEmu support");
				RegSetValueExA(hkey, "", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));

				//lstrcpyA(szValue, 
				//	"$If (Shell || Info || QView || Tree || Viewer || Editor) F12 $Else waitkey(100) $End");
				//RegSetValueExA(hkey, "Sequence", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));
				
				/*if (gFarVersion.dwVerMajor==1) {
					RegSetValueExA(hkey, "Sequence", 0, REG_SZ, (LPBYTE)szCheckKey, (dwSize=strlen((char*)szCheckKey)+1));
				} else {*/
					RegSetValueExW(hkey, L"Sequence", 0, REG_SZ, (LPBYTE)szCheckKey, (dwSize=2*(lstrlenW((WCHAR*)szCheckKey)+1)));
				//}

				lstrcpyA(szValue, "For ConEmu - plugin activation from listening thread");
				RegSetValueExA(hkey, "Description", 0, REG_SZ, (LPBYTE)szValue, (dwSize=strlen(szValue)+1));

				RegSetValueExA(hkey, "DisableOutput", 0, REG_DWORD, (LPBYTE)&(disp=1), (dwSize=4));

				RegCloseKey(hkey); hkey=NULL;
			}
		}
	}


	// ���������� ������� � FAR?
	if (lbMacroAdded && abAllowAPI) {
		if (gFarVersion.dwVerMajor==1)
			ReloadMacroA();
		else if (gFarVersion.dwBuild>=789)
			ReloadMacro789();
		else
			ReloadMacro757();
	}

	// First call
	if (abAllowAPI) {
		NotifyChangeKey();
	}
}

void UpdateConEmuTabsW(int event, bool losingFocus, bool editorSave)
{
	if (gFarVersion.dwBuild>=789)
		UpdateConEmuTabsW789(event, losingFocus, editorSave);
	else
		UpdateConEmuTabsW757(event, losingFocus, editorSave);
}

BOOL CreateTabs(int windowCount)
{
	EnterCriticalSection(&csTabs);

	if ((tabs==NULL) || (maxTabCount <= (windowCount + 1)))
	{
		maxTabCount = windowCount + 10; // � �������
		if (tabs) {
			free(tabs); tabs = NULL;
		}
		
		tabs = (ConEmuTab*) calloc(maxTabCount, sizeof(ConEmuTab));
	}
	
	lastWindowCount = windowCount;
	
	if (!tabs)
		LeaveCriticalSection(&csTabs);
	return tabs!=NULL;
}

BOOL AddTab(int &tabCount, bool losingFocus, bool editorSave, 
			int Type, LPCWSTR Name, LPCWSTR FileName, int Current, int Modified)
{
    BOOL lbCh = FALSE;
    
	if (Type == WTYPE_PANELS) {
	    lbCh = (tabs[0].Current != (losingFocus ? 1 : 0)) ||
	           (tabs[0].Type != WTYPE_PANELS);
		tabs[0].Current = losingFocus ? 1 : 0;
		//lstrcpyn(tabs[0].Name, GetMsgW789(0), CONEMUTABMAX-1);
		tabs[0].Name[0] = 0;
		tabs[0].Pos = 0;
		tabs[0].Type = WTYPE_PANELS;
	} else
	if (Type == WTYPE_EDITOR || Type == WTYPE_VIEWER)
	{
		// when receiving saving event receiver is still reported as modified
		if (editorSave && lstrcmpi(FileName, Name) == 0)
			Modified = 0;
	
	    lbCh = (tabs[tabCount].Current != (losingFocus ? 0 : Current)) ||
	           (tabs[tabCount].Type != Type) ||
	           (tabs[tabCount].Modified != Modified) ||
	           (lstrcmp(tabs[tabCount].Name, Name) != 0);
	
		// when receiving losing focus event receiver is still reported as current
		tabs[tabCount].Type = Type;
		tabs[tabCount].Current = losingFocus ? 0 : Current;
		tabs[tabCount].Modified = Modified;

		if (tabs[tabCount].Current != 0)
		{
			lastModifiedStateW = Modified != 0 ? 1 : 0;
		}
		else
		{
			lastModifiedStateW = -1;
		}

		int nLen=min(lstrlen(Name),(CONEMUTABMAX-1));
		lstrcpyn(tabs[tabCount].Name, Name, nLen+1);
		tabs[tabCount].Name[nLen]=0;

		tabs[tabCount].Pos = tabCount;
		tabCount++;
	}
	
	return lbCh;
}

void SendTabs(int tabCount, BOOL abFillDataOnly/*=FALSE*/)
{
	//if (abWritePipe) { //2009-06-01 ������ ����� ������ ����������� �����...
	EnterCriticalSection(&csTabs);
	BOOL bReleased = FALSE;
	//}
#ifdef _DEBUG
	WCHAR szDbg[100]; wsprintf(szDbg, L"-SendTabs(%i,%s), prev=%i\n", tabCount, 
		abFillDataOnly ? L"FillDataOnly" : L"Post", gnCurTabCount);
	OUTPUTDEBUGSTRING(szDbg);
#endif
	if (tabs 
		&& (abFillDataOnly || (ConEmuHwnd && IsWindow(ConEmuHwnd)))
		)
	{
		COPYDATASTRUCT cds;
		if (tabs[0].Type == WTYPE_PANELS) {
			cds.dwData = tabCount;
			cds.lpData = tabs;
		} else {
			// ������� ��� - ��� ��� ������ � ������ ���������!
			cds.dwData = --tabCount;
			cds.lpData = tabs+1;
		}
		// ���� abFillDataOnly - ������ ���������������� ��� ������ � Pipe - ����� ��������� �������� ������ ����

		cds.cbData = cds.dwData * sizeof(ConEmuTab);
		EnterCriticalSection(&csData);
		OutDataAlloc(sizeof(cds.dwData) + cds.cbData);
		OutDataWrite(&cds.dwData, sizeof(cds.dwData));
		OutDataWrite(cds.lpData, cds.cbData);
		LeaveCriticalSection(&csData);

		LeaveCriticalSection(&csTabs); bReleased = TRUE;

		//TODO: ��������, ��� ��� ������������ ���� �������� �� ������ ����� ����� ��������� ����?
		_ASSERT(gpCmdRet!=NULL);
		if (!abFillDataOnly && tabCount && gnReqCommand==(DWORD)-1) {
			//cds.cbData = tabCount * sizeof(ConEmuTab);
			//SendMessage(ConEmuHwnd, WM_COPYDATA, (WPARAM)FarHwnd, (LPARAM)&cds);
			// ��� ����� ������ ������ ���� ������������ �����. ���� ������ ������� ConEmu - �� ��������...
			if (gnCurTabCount != tabCount || tabCount > 1) {
				gnCurTabCount = tabCount; // ����� ��������!, � �� ��� ������� ����� ���������� ��� ������ �����...
				//PostMessage(ConEmuHwnd, gnMsgTabChanged, tabCount, 0);
				gpCmdRet->nCmd = CECMD_TABSCHANGED;
				CESERVER_REQ* pOut =
					ExecuteGuiCmd(FarHwnd, gpCmdRet);
				if (pOut) free(pOut);
			}
		}
	}
	//free(tabs); - ������������� � ExitFARW
    gnCurTabCount = tabCount;
	if (!bReleased) {
		LeaveCriticalSection(&csTabs); bReleased = TRUE;
	}
}

// watch non-modified -> modified editor status change

int lastModifiedStateW = -1;

int WINAPI _export ProcessEditorInputW(void* Rec)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	if (gFarVersion.dwBuild>=789)
		return ProcessEditorInputW789((LPCVOID)Rec);
	else
		return ProcessEditorInputW757((LPCVOID)Rec);
}

int WINAPI _export ProcessEditorEventW(int Event, void *Param)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	/*if (gFarVersion.dwBuild>=789)
		return ProcessEditorEventW789(Event,Param);
	else
		return ProcessEditorEventW757(Event,Param);*/
	// ����� ���� ������� �� �����������, �� � �� ANSI �� ����������...
	switch (Event)
	{
	case EE_CLOSE:
		OUTPUTDEBUGSTRING(L"EE_CLOSE"); break;
	case EE_GOTFOCUS:
		OUTPUTDEBUGSTRING(L"EE_GOTFOCUS"); break;
	case EE_KILLFOCUS:
		OUTPUTDEBUGSTRING(L"EE_KILLFOCUS"); break;
	case EE_SAVE:
		OUTPUTDEBUGSTRING(L"EE_SAVE"); break;
	//case EE_READ: -- � ���� ������ ���������� ���� ��� �� ����������
	default:
		return 0;
	}
	// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
	UpdateConEmuTabsW(Event, Event == EE_KILLFOCUS, Event == EE_SAVE);
	return 0;
}

int WINAPI _export ProcessViewerEventW(int Event, void *Param)
{
	if (!ConEmuHwnd)
		return 0; // ���� �� �� ��� ���������� - ������
	/*if (gFarVersion.dwBuild>=789)
		return ProcessViewerEventW789(Event,Param);
	else
		return ProcessViewerEventW757(Event,Param);*/
	// ����� ���� ������� �� �����������, �� � �� ANSI �� ����������...
	switch (Event)
	{
	case VE_CLOSE:
		OUTPUTDEBUGSTRING(L"VE_CLOSE"); break;
	//case VE_READ:
	//	OUTPUTDEBUGSTRING(L"VE_CLOSE"); break;
	case VE_KILLFOCUS:
		OUTPUTDEBUGSTRING(L"VE_KILLFOCUS"); break;
	case VE_GOTFOCUS:
		OUTPUTDEBUGSTRING(L"VE_GOTFOCUS"); break;
	default:
		return 0;
	}
	// !!! ������ UpdateConEmuTabsW, ��� ������ !!!
	UpdateConEmuTabsW(Event, Event == VE_KILLFOCUS, false);
	return 0;
}

void StopThread(void)
{
	if (hEventCmd[CMD_EXIT])
		SetEvent(hEventCmd[CMD_EXIT]); // ��������� ����

	if (ghServerTerminateEvent) {
		SetEvent(ghServerTerminateEvent);

        HANDLE hPipe = INVALID_HANDLE_VALUE;
		DWORD dwWait = 0;
        // ����������� �����, ����� ���� ������� �����������
        for (int i=0; i<MAX_SERVER_THREADS; i++) {
			OutputDebugString(L"Plugin: Touching our server pipe\n");
			HANDLE hServer = ghActiveServerThread;
            hPipe = CreateFile(gszPluginServerPipe,GENERIC_WRITE,0,NULL,OPEN_EXISTING,0,NULL);
			if (hPipe == INVALID_HANDLE_VALUE) {
				OutputDebugString(L"Plugin: All pipe instances closed?\n");
                break;
			}
			OutputDebugString(L"Plugin: Waiting server pipe thread\n");
			dwWait = WaitForSingleObject(hServer, 200); // �������� ���������, ���� ���� ����������
            // ������ ������� ���� - ��� ����� ���� �����������
            CloseHandle(hPipe);
            hPipe = INVALID_HANDLE_VALUE;
        }
        // ������� ��������, ���� ��� ���� ����������
		OutputDebugString(L"Plugin: Checking server pipe threads are closed\n");
        WaitForMultipleObjects(MAX_SERVER_THREADS, ghServerThreads, TRUE, 500);
        for (int i=0; i<MAX_SERVER_THREADS; i++) {
			if (WaitForSingleObject(ghServerThreads[i],0) != WAIT_OBJECT_0) {
				OutputDebugString(L"### Plugin: Terminating mh_ServerThreads\n");
                TerminateThread(ghServerThreads[i],0);
			}
            CloseHandle(ghServerThreads[i]);
            ghServerThreads[i] = NULL;
        }

		SafeCloseHandle(ghServerTerminateEvent);
		SafeCloseHandle(ghServerSemaphore);
	}

	//CloseTabs(); -- ConEmu ���� ����������
	if (hThread) { // �������� ����-����, ��� ������������� ������� ���� ��������
		if (WaitForSingleObject(hThread,1000)) {
			#pragma warning (disable : 6258)
			TerminateThread(hThread, 100);
		}
		CloseHandle(hThread); hThread = NULL;
	}
	
    if (tabs) {
	    free(tabs);
	    tabs = NULL;
    }
    
    if (ghReqCommandEvent) {
	    CloseHandle(ghReqCommandEvent); ghReqCommandEvent = NULL;
    }

	DeleteCriticalSection(&csTabs); memset(&csTabs,0,sizeof(csTabs));
	DeleteCriticalSection(&csData); memset(&csData,0,sizeof(csData));
	
	if (ghRegMonitorKey) { RegCloseKey(ghRegMonitorKey); ghRegMonitorKey = NULL; }
	SafeCloseHandle(ghRegMonitorEvt);
}

void   WINAPI _export ExitFARW(void)
{
	StopThread();

	if (gFarVersion.dwBuild>=789)
		ExitFARW789();
	else
		ExitFARW757();
}

void CloseTabs()
{
	if (ConEmuHwnd && IsWindow(ConEmuHwnd)) {
		COPYDATASTRUCT cds;
		cds.dwData = 0;
		cds.lpData = &cds.dwData;
		cds.cbData = sizeof(cds.dwData);
		SendMessage(ConEmuHwnd, WM_COPYDATA, (WPARAM)FarHwnd, (LPARAM)&cds);
	}
}

// ���� �� �������� - ����� ������������� �������������. ������ � ������
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataAlloc(DWORD anSize)
{
	// + ������ ��������� gpCmdRet
	gpCmdRet = (CESERVER_REQ*)calloc(12+anSize,1);
	if (!gpCmdRet)
		return FALSE;

	gpCmdRet->nSize = anSize+12;
	gpCmdRet->nVersion = CESERVER_REQ_VER;

	gpData = gpCmdRet->Data;
	gnDataSize = anSize;
	gpCursor = gpData;

	return TRUE;
}

// ������ � ������. ���������� ������������� �� OutDataWrite
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataRealloc(DWORD anNewSize)
{
	if (!gpCmdRet)
		return OutDataAlloc(anNewSize);

	if (anNewSize < gnDataSize)
		return FALSE; // ������ �������� ������ ������, ��� ��� ����

	// realloc ������ �� ��������, ��� ��� ���� � �� ��������
	CESERVER_REQ* lpNewCmdRet = (CESERVER_REQ*)calloc(12+anNewSize,1);
	if (!lpNewCmdRet)
		return FALSE;
	lpNewCmdRet->nCmd = gpCmdRet->nCmd;
	lpNewCmdRet->nSize = anNewSize+12;
	lpNewCmdRet->nVersion = gpCmdRet->nVersion;

	LPBYTE lpNewData = lpNewCmdRet->Data;
	if (!lpNewData)
		return FALSE;

	// ����������� ������������ ������
	memcpy(lpNewData, gpData, gnDataSize);
	// ��������� ����� ������� �������
	gpCursor = lpNewData + (gpCursor - gpData);
	// � ����� ����� � ��������
	free(gpCmdRet);
	gpCmdRet = lpNewCmdRet;
	gpData = lpNewData;
	gnDataSize = anNewSize;

	return TRUE;
}

// ������ � ������
// ���������� FALSE ��� ������� ��������� ������
BOOL OutDataWrite(LPVOID apData, DWORD anSize)
{
	if (!gpData) {
		if (!OutDataAlloc(max(1024, (anSize+128))))
			return FALSE;
	} else if (((gpCursor-gpData)+anSize)>gnDataSize) {
		if (!OutDataRealloc(gnDataSize+max(1024, (anSize+128))))
			return FALSE;
	}

	// ����������� ������
	memcpy(gpCursor, apData, anSize);
	gpCursor += anSize;

	return TRUE;
}

int ShowMessage(int aiMsg, int aiButtons)
{
	if (gFarVersion.dwVerMajor==1)
		return ShowMessageA(aiMsg, aiButtons);
	else if (gFarVersion.dwBuild>=789)
		return ShowMessage789(aiMsg, aiButtons);
	else
		return ShowMessage757(aiMsg, aiButtons);
}

LPCWSTR GetMsgW(int aiMsg)
{
	if (gFarVersion.dwVerMajor==1)
		return L"";
	else if (gFarVersion.dwBuild>=789)
		return GetMsg789(aiMsg);
	else
		return GetMsg757(aiMsg);
}

void PostMacro(wchar_t* asMacro)
{
	if (!asMacro || !*asMacro)
		return;
		
	if (gFarVersion.dwVerMajor==1) {
		int nLen = lstrlenW(asMacro);
		char* pszMacro = (char*)calloc(nLen+1,1);
		if (pszMacro) {
			WideCharToMultiByte(CP_OEMCP,0,asMacro,nLen+1,pszMacro,nLen+1,0,0);
			PostMacroA(pszMacro);
			free(pszMacro);
		}
	} else if (gFarVersion.dwBuild>=789) {
		PostMacro789(asMacro);
	} else {
		PostMacro757(asMacro);
	}
}

DWORD WINAPI ServerThread(LPVOID lpvParam) 
{ 
    BOOL fConnected = FALSE;
    DWORD dwErr = 0;
    HANDLE hPipe = NULL; 
    HANDLE hWait[2] = {NULL,NULL};
	DWORD dwTID = GetCurrentThreadId();

    _ASSERTE(gszPluginServerPipe[0]!=0);
    _ASSERTE(ghServerSemaphore!=NULL);

    // The main loop creates an instance of the named pipe and 
    // then waits for a client to connect to it. When the client 
    // connects, a thread is created to handle communications 
    // with that client, and the loop is repeated. 
    
    hWait[0] = ghServerTerminateEvent;
    hWait[1] = ghServerSemaphore;

    // ���� �� ����������� ���������� �������
    do {
        while (!fConnected)
        { 
            _ASSERTE(hPipe == NULL);

            // ��������� ���������� ��������, ��� �������� �������
            dwErr = WaitForMultipleObjects ( 2, hWait, FALSE, INFINITE );
            if (dwErr == WAIT_OBJECT_0) {
                return 0; // ������� �����������
            }

			for (int i=0; i<MAX_SERVER_THREADS; i++) {
				if (gnServerThreadsId[i] == dwTID) {
					ghActiveServerThread = ghServerThreads[i]; break;
				}
			}

            hPipe = CreateNamedPipe( 
                gszPluginServerPipe,      // pipe name 
                PIPE_ACCESS_DUPLEX,       // read/write access 
                PIPE_TYPE_MESSAGE |       // message type pipe 
                PIPE_READMODE_MESSAGE |   // message-read mode 
                PIPE_WAIT,                // blocking mode 
                PIPE_UNLIMITED_INSTANCES, // max. instances  
                PIPEBUFSIZE,              // output buffer size 
                PIPEBUFSIZE,              // input buffer size 
                0,                        // client time-out 
                NULL);                    // default security attribute 

            _ASSERTE(hPipe != INVALID_HANDLE_VALUE);

            if (hPipe == INVALID_HANDLE_VALUE) 
            {
                //DisplayLastError(L"CreatePipe failed"); 
                hPipe = NULL;
                Sleep(50);
                continue;
            }

            // Wait for the client to connect; if it succeeds, 
            // the function returns a nonzero value. If the function
            // returns zero, GetLastError returns ERROR_PIPE_CONNECTED. 

            fConnected = ConnectNamedPipe(hPipe, NULL) ? TRUE : ((dwErr = GetLastError()) == ERROR_PIPE_CONNECTED); 

            // ������� �����������!
            if (WaitForSingleObject ( ghServerTerminateEvent, 0 ) == WAIT_OBJECT_0) {
                //FlushFileBuffers(hPipe); -- ��� �� �����, �� ������ �� ����������
                //DisconnectNamedPipe(hPipe); 
				ReleaseSemaphore(ghServerSemaphore, 1, NULL);
                SafeCloseHandle(hPipe);
                return 0;
            }

            if (!fConnected)
                SafeCloseHandle(hPipe);
        }

        if (fConnected) {
            // ����� �������, ����� �� ������
            fConnected = FALSE;
            // ��������� ������ ���� ������� �����
            ReleaseSemaphore(ghServerSemaphore, 1, NULL);
            
            ServerThreadCommand ( hPipe ); // ��� ������������� - ���������� � ���� ��������� ����
        }

        FlushFileBuffers(hPipe); 
        //DisconnectNamedPipe(hPipe); 
        SafeCloseHandle(hPipe);
    } // ������� � �������� ������ instance �����
    while (WaitForSingleObject ( ghServerTerminateEvent, 0 ) != WAIT_OBJECT_0);

    return 0; 
}

void ServerThreadCommand(HANDLE hPipe)
{
    CESERVER_REQ in={0}, *pIn=NULL;
    DWORD cbRead = 0, cbWritten = 0, dwErr = 0;
    BOOL fSuccess = FALSE;

    // Send a message to the pipe server and read the response. 
    fSuccess = ReadFile( 
        hPipe,            // pipe handle 
        &in,              // buffer to receive reply
        sizeof(in),       // size of read buffer
        &cbRead,          // bytes read
        NULL);            // not overlapped 

    if (!fSuccess && ((dwErr = GetLastError()) != ERROR_MORE_DATA)) 
    {
        _ASSERTE("ReadFile(pipe) failed"==NULL);
        //CloseHandle(hPipe);
        return;
    }
    _ASSERTE(in.nSize>=12 && cbRead>=12);
    _ASSERTE(in.nVersion == CESERVER_REQ_VER);
    if (cbRead < 12 || /*in.nSize < cbRead ||*/ in.nVersion != CESERVER_REQ_VER) {
        //CloseHandle(hPipe);
        return;
    }

    int nAllSize = in.nSize;
    pIn = (CESERVER_REQ*)calloc(nAllSize,1);
    _ASSERTE(pIn!=NULL);
    memmove(pIn, &in, cbRead);
    _ASSERTE(pIn->nVersion==CESERVER_REQ_VER);

    LPBYTE ptrData = ((LPBYTE)pIn)+cbRead;
    nAllSize -= cbRead;

    while(nAllSize>0)
    { 
        //_tprintf(TEXT("%s\n"), chReadBuf);

        // Break if TransactNamedPipe or ReadFile is successful
        if(fSuccess)
            break;

        // Read from the pipe if there is more data in the message.
        fSuccess = ReadFile( 
            hPipe,      // pipe handle 
            ptrData,    // buffer to receive reply 
            nAllSize,   // size of buffer 
            &cbRead,    // number of bytes read 
            NULL);      // not overlapped 

        // Exit if an error other than ERROR_MORE_DATA occurs.
        if( !fSuccess && ((dwErr = GetLastError()) != ERROR_MORE_DATA)) 
            break;
        ptrData += cbRead;
        nAllSize -= cbRead;
    }

    TODO("����� ���������� ASSERT, ���� ������� ���� ������� � �������� ������");
    _ASSERTE(nAllSize==0);
    if (nAllSize>0) {
        //CloseHandle(hPipe);
        return; // ������� ������� �� ��� ������
    }

	UINT nDataSize = pIn->nSize - sizeof(CESERVER_REQ) + 1;

    // ��� ������ �� ����� ��������, ������������ ������� � ���������� (���� �����) ���������
	//fSuccess = WriteFile( hPipe, pOut, pOut->nSize, &cbWritten, NULL);

	if (pIn->nCmd == CMD_LANGCHANGE) {
		_ASSERTE(nDataSize>=4);
		HKL hkl = (HKL)*((DWORD*)pIn->Data);
		if (hkl) {
			DWORD dwLastError = 0;
			WCHAR szLoc[10]; wsprintf(szLoc, L"%08x", (DWORD)(((DWORD)hkl) & 0xFFFF));
			HKL hkl1 = LoadKeyboardLayout(szLoc, KLF_ACTIVATE|KLF_REORDER|KLF_SUBSTITUTE_OK|KLF_SETFORPROCESS);
			HKL hkl2 = ActivateKeyboardLayout(hkl1, KLF_SETFORPROCESS|KLF_REORDER);
			if (!hkl2) {
				dwLastError = GetLastError();
			}
			dwLastError = dwLastError;
		}

	} else if (pIn->nCmd == CMD_DEFFONT) {
		// ���������� - �����������, ��������� �� ���������
		SetConsoleFontSizeTo(FarHwnd, 4, 6);
		MoveWindow(FarHwnd, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 1); // ����� ������ ��������� ������ ���������...

	} else if (pIn->nCmd == CMD_REQTABS) {
		SendTabs(gnCurTabCount, TRUE);

	} else {
		ProcessCommand(pIn->nCmd, TRUE/*bReqMainThread*/, pIn->Data);

		if (gpCmdRet) {
			fSuccess = WriteFile( hPipe, gpCmdRet, gpCmdRet->nSize, &cbWritten, NULL);
			free(gpCmdRet);
			gpCmdRet = NULL; gpData = NULL; gpCursor = NULL;
		}
	}


    // ���������� ������
    free(pIn);

    //CloseHandle(hPipe); -- ��������� ���������� �������
    return;
}
