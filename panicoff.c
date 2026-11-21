/*
 * PanicOff - instant power-off on Ctrl+Alt+Shift+A (Windows 10/11)
 *
 * Double-click:  asks whether to install it (starts at every login) or run it once.
 *                If it's already running, offers to uninstall it instead.
 *
 * Command line (optional):
 *   PanicOff.exe --install     copy to %LOCALAPPDATA%\PanicOff, add to autostart, start it
 *   PanicOff.exe --uninstall   stop it, remove the autostart entry, delete the installed copy
 *   PanicOff.exe --stop        stop the running instance (stays in autostart)
 *   PanicOff.exe --test        arm the hotkey, but only show a message box (no shutdown)
 *   PanicOff.exe --run         listen silently (this is what autostart uses)
 *
 * How it shuts down:
 *   NtShutdownSystem(ShutdownPowerOff) from ntdll, the kernel's own power-off path.
 *   It skips asking apps and services to close (the slow part of a normal shutdown),
 *   but still flushes file-system caches and the registry to disk before cutting power.
 *   Unsaved work in open apps is lost. If that call fails for any reason it falls
 *   back to ExitWindowsEx(EWX_POWEROFF | EWX_FORCE).
 *   No admin rights needed: normal user accounts have SeShutdownPrivilege.
 *
 * Change the hotkey: edit HOTKEY_MODS / HOTKEY_KEY / HOTKEY_TEXT below.
 *
 * Build (MinGW-w64, cross-compiling from Linux):
 *   x86_64-w64-mingw32-gcc -O2 -s -municode -mwindows panicoff.c -o PanicOff.exe -luser32 -ladvapi32
 * Build (on Windows with MSYS2 / MinGW gcc):
 *   gcc -O2 -s -municode -mwindows panicoff.c -o PanicOff.exe -luser32 -ladvapi32
 */
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <wchar.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

#define APP_NAME   L"PanicOff"
#define WND_CLASS  L"PanicOff_HotkeyWindow"
#define MUTEX_NAME L"Local\\PanicOff_SingleInstance"
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define HOTKEY_ID  1

/* ---- The hotkey. Change these three lines to use a different combo. ---- */
#define HOTKEY_MODS (MOD_CONTROL | MOD_ALT | MOD_SHIFT) /* MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN */
#define HOTKEY_KEY  'A'                                 /* uppercase letter/digit, or a VK_* code  */
#define HOTKEY_TEXT L"Ctrl+Alt+Shift+A"                  /* only used in the message boxes          */

typedef LONG (NTAPI *NtShutdownSystem_t)(int action);
#define SHUTDOWN_POWER_OFF 2 /* SHUTDOWN_ACTION::ShutdownPowerOff */

static NtShutdownSystem_t g_ntShutdown;
static BOOL g_testMode;

static void InfoBox(const wchar_t *text)
{
    MessageBoxW(NULL, text, APP_NAME, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

static void ErrorBox(const wchar_t *text)
{
    MessageBoxW(NULL, text, APP_NAME, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

/* ---------------------------------------------------------------- shutdown */

static void EnableShutdownPrivilege(void)
{
    HANDLE token;
    TOKEN_PRIVILEGES tp;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(token);
}

static void ShutdownNow(void)
{
    const DWORD reason = SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED;

    /* 1) Kernel power-off. Does not return if it works. */
    if (g_ntShutdown)
        g_ntShutdown(SHUTDOWN_POWER_OFF);

    /* 2) Fallbacks: forced Win32 shutdown (apps are killed, not asked). */
    EnableShutdownPrivilege();
    if (g_ntShutdown)
        g_ntShutdown(SHUTDOWN_POWER_OFF);
    if (!ExitWindowsEx(EWX_POWEROFF | EWX_FORCE, reason))
        InitiateSystemShutdownExW(NULL, NULL, 0, TRUE, FALSE, reason);
}

/* --------------------------------------------------------------- listener */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_HOTKEY:
        if (wp == HOTKEY_ID) {
            if (g_testMode) {
                MessageBoxW(NULL,
                    HOTKEY_TEXT L" works!\n\nThis was test mode, so the PC is NOT shutting down.",
                    APP_NAME, MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
                DestroyWindow(hwnd);
            } else {
                ShutdownNow();
            }
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int RunListener(BOOL testMode)
{
    HANDLE mutex;
    WNDCLASSW wc;
    HWND hwnd;
    MSG m;

    g_testMode = testMode;

    mutex = CreateMutexW(NULL, TRUE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (testMode)
            ErrorBox(L"PanicOff is already running in the background, so the test can't grab the hotkey.\n\n"
                     L"Stop it first with:  PanicOff.exe --stop");
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }

    /* Do all the setup work now so the hotkey path is as short as possible. */
    EnableShutdownPrivilege();
    g_ntShutdown = (NtShutdownSystem_t)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtShutdownSystem");

    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = WND_CLASS;
    RegisterClassW(&wc);

    /* Message-only window: invisible, not in the taskbar or Alt+Tab. */
    hwnd = CreateWindowExW(0, WND_CLASS, APP_NAME, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        ErrorBox(L"Couldn't create the background window.");
        return 1;
    }

    if (!RegisterHotKey(hwnd, HOTKEY_ID, HOTKEY_MODS | MOD_NOREPEAT, HOTKEY_KEY)) {
        ErrorBox(L"Couldn't grab " HOTKEY_TEXT L": another program has already registered it as a global hotkey.");
        DestroyWindow(hwnd);
        return 1;
    }

    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (mutex)
        CloseHandle(mutex);
    return 0;
}

/* ------------------------------------------------------- install/uninstall */

static BOOL GetInstallPaths(wchar_t *dir, wchar_t *exe) /* both MAX_PATH */
{
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (n == 0 || n > MAX_PATH - 32)
        return FALSE;
    lstrcatW(dir, L"\\PanicOff");
    lstrcpyW(exe, dir);
    lstrcatW(exe, L"\\PanicOff.exe");
    return TRUE;
}

static HWND FindRunningInstance(void)
{
    return FindWindowExW(HWND_MESSAGE, NULL, WND_CLASS, NULL);
}

static BOOL StopRunningInstance(void)
{
    HWND h = FindRunningInstance();
    DWORD pid = 0;
    HANDLE proc;

    if (!h)
        return FALSE;
    GetWindowThreadProcessId(h, &pid);
    proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(h, WM_CLOSE, 0, 0);
    if (proc) {
        WaitForSingleObject(proc, 5000);
        CloseHandle(proc);
    }
    return TRUE;
}

static int Install(void)
{
    wchar_t self[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH], cmd[MAX_PATH + 16];
    HKEY key;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    if (!GetModuleFileNameW(NULL, self, MAX_PATH) || !GetInstallPaths(dir, exe)) {
        ErrorBox(L"Couldn't work out the install location.");
        return 1;
    }

    StopRunningInstance();
    CreateDirectoryW(dir, NULL);
    if (lstrcmpiW(self, exe) != 0 && !CopyFileW(self, exe, FALSE)) {
        ErrorBox(L"Couldn't copy PanicOff to %LOCALAPPDATA%\\PanicOff.");
        return 1;
    }

    wsprintfW(cmd, L"\"%s\" --run", exe);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS ||
        RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)cmd,
                       (DWORD)((lstrlenW(cmd) + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) {
        ErrorBox(L"Couldn't add PanicOff to autostart.");
        return 1;
    }
    RegCloseKey(key);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        ErrorBox(L"Installed, but couldn't start it. It will start at your next login.");
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    InfoBox(L"PanicOff is installed and armed.\n\n"
            HOTKEY_TEXT L"  =  instant power-off.\n"
            L"It starts automatically every time you log in.\n\n"
            L"To remove it, just double-click PanicOff.exe again.");
    return 0;
}

static int Uninstall(BOOL quiet)
{
    wchar_t dir[MAX_PATH], exe[MAX_PATH], cmd[3 * MAX_PATH];
    HKEY key;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    StopRunningInstance();

    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, APP_NAME);
        RegCloseKey(key);
    }

    /* Show the message first: while a message box is open this exe stays locked. */
    if (!quiet)
        InfoBox(L"PanicOff is stopped and removed. " HOTKEY_TEXT L" is back to normal.");

    /* Delete the installed copy via cmd after a short delay, so this also works
       when the running exe *is* the installed copy (we exit right after this). */
    if (GetInstallPaths(dir, exe) && GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES) {
        wsprintfW(cmd, L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"%s\" & rmdir \"%s\"", exe, dir);
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------- main */

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR args, int show)
{
    wchar_t self[MAX_PATH], dir[MAX_PATH], exe[MAX_PATH];
    int answer;

    (void)hInst; (void)hPrev; (void)show;
    if (!args)
        args = L"";

    if (wcsstr(args, L"--uninstall")) return Uninstall(FALSE);
    if (wcsstr(args, L"--install"))   return Install();
    if (wcsstr(args, L"--run"))       return RunListener(FALSE);
    if (wcsstr(args, L"--test"))      return RunListener(TRUE);
    if (wcsstr(args, L"--stop")) {
        InfoBox(StopRunningInstance() ? L"PanicOff stopped (it will start again at next login if installed)."
                                      : L"PanicOff isn't running.");
        return 0;
    }

    /* Plain double-click. Already running? Offer to remove it. */
    if (FindRunningInstance()) {
        answer = MessageBoxW(NULL,
            L"PanicOff is already running (" HOTKEY_TEXT L" is armed).\n\n"
            L"Do you want to stop and uninstall it?",
            APP_NAME, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
        return answer == IDYES ? Uninstall(FALSE) : 0;
    }

    /* Launched from the install folder? Just listen. */
    if (GetModuleFileNameW(NULL, self, MAX_PATH) && GetInstallPaths(dir, exe) && lstrcmpiW(self, exe) == 0)
        return RunListener(FALSE);

    answer = MessageBoxW(NULL,
        L"While PanicOff runs, " HOTKEY_TEXT L" powers the PC off INSTANTLY.\n"
        L"Open apps don't get a chance to save anything.\n\n"
        L"Yes  =  install it (starts automatically at every login)\n"
        L"No  =  just run it until the next reboot\n"
        L"Cancel  =  do nothing",
        APP_NAME, MB_YESNOCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);
    if (answer == IDYES) return Install();
    if (answer == IDNO)  return RunListener(FALSE);
    return 0;
}
