#define _WIN32_IE 0x0400
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "resources.h"

#define UNUSED_PARAMETER(x) (void)(x)

#define WM_NOTIFYICON WM_APP

#define HOOK_DLL_NAME "snaphook.dll"
#define HOOK_FUNC_NAME "snaphook_event_proc@28"

enum
{
    ID_COMMAND_ENABLE_SNAP,
	ID_COMMAND_RUN_AT_STARTUP,
	ID_COMMAND_ABOUT,
	ID_COMMAND_QUIT,
};

static HMENU hPopupMenu;
static NOTIFYICONDATA notifyIcon;
static HMODULE hDll;
static WINEVENTPROC hookEventProc;
static BOOL snapEnabled;
static HWINEVENTHOOK hEventHook;
static HWND hwndAboutDialog;
static BOOL runAtStartup;

static void errmsg(const char *text)
{
	MessageBox(NULL, text, NULL, MB_ICONERROR | MB_OK);
}

static void enable_snap(BOOL enabled)
{
	if (enabled)
	{
		hEventHook = SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND, hDll, hookEventProc, 0, 0, WINEVENT_INCONTEXT);
		if (hEventHook != NULL)
			snapEnabled = TRUE;
		else
			errmsg("Failed to set event hook.");
	}
	else
	{
		if (hEventHook != NULL)
		{
			if (UnhookWinEvent(hEventHook))
			{
				hEventHook = NULL;
				snapEnabled = FALSE;
			}
			else
			{
				errmsg("Failed to remove event hook.");
			}
		}
	}

	CheckMenuItem(hPopupMenu, ID_COMMAND_ENABLE_SNAP, snapEnabled ? MF_CHECKED : MF_UNCHECKED);
}

static HKEY open_run_key(void)
{
	HKEY hkey;
	LONG result = RegOpenKeyEx(
	  HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
	  0, KEY_QUERY_VALUE | KEY_SET_VALUE, &hkey);
	return (result == ERROR_SUCCESS) ? hkey : NULL;
}

static void enable_startup_entry(BOOL enable)
{
	HKEY hRunKey = open_run_key();

	if (enable)
	{
		char path[MAX_PATH + 1];
		GetModuleFileName(NULL, path, sizeof(path));
		RegSetValueEx(hRunKey, "Snap XP", 0, REG_SZ, (BYTE *)path, strlen(path) + 1);
	}
	else
	{
		RegDeleteValue(hRunKey, "Snap XP");
	}
	RegCloseKey(hRunKey);
	runAtStartup = enable;
	CheckMenuItem(hPopupMenu, ID_COMMAND_RUN_AT_STARTUP, enable ? MF_CHECKED : MF_UNCHECKED);
}

static BOOL is_startup_entry_present(void)
{
	DWORD valueType;
	HKEY hRunKey = open_run_key();
	BOOL ret = (RegQueryValueEx(hRunKey, "Snap XP", NULL, &valueType, NULL, NULL) == ERROR_SUCCESS && valueType == REG_SZ);
	
	RegCloseKey(hRunKey);
	return ret;
}

static INT_PTR CALLBACK about_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_COMMAND)
	{
		EndDialog(hwnd, 0);
		hwndAboutDialog = NULL;
		return TRUE;
	}
	return FALSE;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static UINT taskbarRestartMsg;

    switch (msg)
    {
	  case WM_NOTIFYICON:
		if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
		{
			POINT cursorPos;
			
			GetCursorPos(&cursorPos);
			SetForegroundWindow(hwnd);
			TrackPopupMenu(hPopupMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY,
			  cursorPos.x, cursorPos.y, 0, hwnd, NULL);
		}
		break;
	  case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		  case ID_COMMAND_ENABLE_SNAP:
			enable_snap(!snapEnabled);
			break;
		  case ID_COMMAND_RUN_AT_STARTUP:
			enable_startup_entry(!runAtStartup);
			break;
		  case ID_COMMAND_ABOUT:
			if (hwndAboutDialog == NULL)
			{
				hwndAboutDialog = CreateDialog(NULL, MAKEINTRESOURCE(ID_RES_DLG_ABOUT), NULL, about_dlg_proc);
				ShowWindow(hwndAboutDialog, SW_SHOW);
			}
			break;
		  case ID_COMMAND_QUIT:
			PostQuitMessage(0);
			break;
		}
		break;
	  case WM_CREATE:
		taskbarRestartMsg = RegisterWindowMessage("TaskbarCreated");
		break;
	  default:
		if (msg == taskbarRestartMsg)
		{
			// The icon doesn't come back automatically when Explorer is restarted, so we must
			// add it again.
			Shell_NotifyIcon(NIM_ADD, &notifyIcon);
			break;
		}
		return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASS wc = {0};
    MSG msg;

	InitCommonControls();

    hDll = LoadLibrary(HOOK_DLL_NAME);
    if (hDll == NULL)
    {
        MessageBox(NULL, "Failed to load library " HOOK_DLL_NAME ".", NULL, MB_ICONERROR | MB_OK);
        return -1;
    }
	hookEventProc = (WINEVENTPROC)GetProcAddress(hDll, HOOK_FUNC_NAME);
	if (hookEventProc == NULL)
	{
		MessageBox(NULL, "Failed to load function " HOOK_FUNC_NAME " from " HOOK_DLL_NAME ".", NULL, MB_ICONERROR | MB_OK);
        return -1;
	}

	runAtStartup = is_startup_entry_present();

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "snapxpnotify";
    RegisterClass(&wc);
   
    // This is a message-only window to process notification icon messages.
    HWND hWnd = CreateWindow(wc.lpszClassName, NULL, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    
    notifyIcon.cbSize = sizeof(NOTIFYICONDATA);
    notifyIcon.hWnd = hWnd;
    notifyIcon.uID = 0;
    notifyIcon.uFlags = NIF_MESSAGE | NIF_ICON;
    notifyIcon.uCallbackMessage = WM_NOTIFYICON;
    notifyIcon.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(ID_RES_APPICON));
    notifyIcon.uVersion = 0;
    Shell_NotifyIcon(NIM_ADD, &notifyIcon);

    hPopupMenu = CreatePopupMenu();
    AppendMenu(hPopupMenu, MF_STRING, ID_COMMAND_ENABLE_SNAP, "&Enable Snap");
	AppendMenu(hPopupMenu, MF_STRING | (runAtStartup ? MF_CHECKED : 0), ID_COMMAND_RUN_AT_STARTUP, "&Run at Startup");
    AppendMenu(hPopupMenu, MF_STRING, ID_COMMAND_ABOUT, "&About SnapXP");
	AppendMenu(hPopupMenu, MF_STRING, ID_COMMAND_QUIT, "&Quit");

	enable_snap(TRUE);

    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &notifyIcon);
	
	enable_snap(FALSE);

    return 0;
}