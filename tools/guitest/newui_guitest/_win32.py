"""The Win32 calls the driver needs, through ctypes (standard library only).

Every function has its argtypes / restype set: ctypes defaults every argument and result to a 32-bit int, which silently
truncates handles (HWND, HANDLE) on 64-bit Windows.
"""
import ctypes
from ctypes import wintypes

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)

ULONG_PTR = ctypes.c_size_t
HANDLE = wintypes.HANDLE
HWND = wintypes.HWND

# ---- constants -----------------------------------------------------------------------------------------------

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1

MOUSEEVENTF_MOVE = 0x0001
MOUSEEVENTF_LEFTDOWN = 0x0002
MOUSEEVENTF_LEFTUP = 0x0004
MOUSEEVENTF_RIGHTDOWN = 0x0008
MOUSEEVENTF_RIGHTUP = 0x0010
MOUSEEVENTF_WHEEL = 0x0800
MOUSEEVENTF_VIRTUALDESK = 0x4000
MOUSEEVENTF_ABSOLUTE = 0x8000

KEYEVENTF_EXTENDEDKEY = 0x0001
KEYEVENTF_KEYUP = 0x0002
KEYEVENTF_UNICODE = 0x0004

WHEEL_DELTA = 120

SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN = 76, 77, 78, 79

SW_MINIMIZE, SW_RESTORE = 6, 9
GA_ROOT = 2
MAPVK_VK_TO_VSC = 0

CREATE_SUSPENDED = 0x00000004
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000
JobObjectExtendedLimitInformation = 9

WAIT_TIMEOUT = 0x00000102
INVALID_HANDLE_VALUE = HANDLE(-1).value
GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
OPEN_EXISTING = 3

# ---- structures ----------------------------------------------------------------------------------------------


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ULONG_PTR)]


class HARDWAREINPUT(ctypes.Structure):
    _fields_ = [("uMsg", wintypes.DWORD), ("wParamL", wintypes.WORD), ("wParamH", wintypes.WORD)]


class _INPUT_UNION(ctypes.Union):
    # MOUSEINPUT is the largest member: it fixes sizeof(INPUT) (40 bytes on x64), which SendInput checks.
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT), ("hi", HARDWAREINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _INPUT_UNION)]


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", wintypes.UINT), ("dwTime", wintypes.DWORD)]


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR), ("lpDesktop", wintypes.LPWSTR),
                ("lpTitle", wintypes.LPWSTR), ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
                ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD), ("dwXCountChars", wintypes.DWORD),
                ("dwYCountChars", wintypes.DWORD), ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", HANDLE), ("hStdOutput", HANDLE), ("hStdError", HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", HANDLE), ("hThread", HANDLE), ("dwProcessId", wintypes.DWORD),
                ("dwThreadId", wintypes.DWORD)]


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ULONG_PTR), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                      "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION), ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def _sig(function, restype, *argtypes):
    function.restype = restype
    function.argtypes = list(argtypes)
    return function


# ---- kernel32 ------------------------------------------------------------------------------------------------

CreateFileW = _sig(kernel32.CreateFileW, HANDLE, wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                   wintypes.DWORD, wintypes.DWORD, HANDLE)
ReadFile = _sig(kernel32.ReadFile, wintypes.BOOL, HANDLE, ctypes.c_void_p, wintypes.DWORD,
                ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
WriteFile = _sig(kernel32.WriteFile, wintypes.BOOL, HANDLE, ctypes.c_void_p, wintypes.DWORD,
                 ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p)
PeekNamedPipe = _sig(kernel32.PeekNamedPipe, wintypes.BOOL, HANDLE, ctypes.c_void_p, wintypes.DWORD,
                     ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD))
CloseHandle = _sig(kernel32.CloseHandle, wintypes.BOOL, HANDLE)
CreateJobObjectW = _sig(kernel32.CreateJobObjectW, HANDLE, ctypes.c_void_p, wintypes.LPCWSTR)
SetInformationJobObject = _sig(kernel32.SetInformationJobObject, wintypes.BOOL, HANDLE, ctypes.c_int, ctypes.c_void_p,
                               wintypes.DWORD)
AssignProcessToJobObject = _sig(kernel32.AssignProcessToJobObject, wintypes.BOOL, HANDLE, HANDLE)
CreateProcessW = _sig(kernel32.CreateProcessW, wintypes.BOOL, wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p,
                      ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
                      ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION))
ResumeThread = _sig(kernel32.ResumeThread, wintypes.DWORD, HANDLE)
WaitForSingleObject = _sig(kernel32.WaitForSingleObject, wintypes.DWORD, HANDLE, wintypes.DWORD)
GetTickCount = _sig(kernel32.GetTickCount, wintypes.DWORD)
GetCurrentThreadId = _sig(kernel32.GetCurrentThreadId, wintypes.DWORD)

# ---- user32 --------------------------------------------------------------------------------------------------

SetProcessDpiAwarenessContext = _sig(user32.SetProcessDpiAwarenessContext, wintypes.BOOL, ctypes.c_void_p)
SendInput = _sig(user32.SendInput, wintypes.UINT, wintypes.UINT, ctypes.POINTER(INPUT), ctypes.c_int)
GetSystemMetrics = _sig(user32.GetSystemMetrics, ctypes.c_int, ctypes.c_int)
GetLastInputInfo = _sig(user32.GetLastInputInfo, wintypes.BOOL, ctypes.POINTER(LASTINPUTINFO))
MapVirtualKeyW = _sig(user32.MapVirtualKeyW, wintypes.UINT, wintypes.UINT, wintypes.UINT)

WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, HWND, wintypes.LPARAM)
EnumWindows = _sig(user32.EnumWindows, wintypes.BOOL, WNDENUMPROC, wintypes.LPARAM)
GetWindowThreadProcessId = _sig(user32.GetWindowThreadProcessId, wintypes.DWORD, HWND, ctypes.POINTER(wintypes.DWORD))
IsWindowVisible = _sig(user32.IsWindowVisible, wintypes.BOOL, HWND)
IsIconic = _sig(user32.IsIconic, wintypes.BOOL, HWND)
GetWindowTextLengthW = _sig(user32.GetWindowTextLengthW, ctypes.c_int, HWND)
GetClassNameW = _sig(user32.GetClassNameW, ctypes.c_int, HWND, wintypes.LPWSTR, ctypes.c_int)
ShowWindow = _sig(user32.ShowWindow, wintypes.BOOL, HWND, ctypes.c_int)
GetForegroundWindow = _sig(user32.GetForegroundWindow, HWND)
SetForegroundWindow = _sig(user32.SetForegroundWindow, wintypes.BOOL, HWND)
AttachThreadInput = _sig(user32.AttachThreadInput, wintypes.BOOL, wintypes.DWORD, wintypes.DWORD, wintypes.BOOL)
GetAncestor = _sig(user32.GetAncestor, HWND, HWND, wintypes.UINT)
WindowFromPoint = _sig(user32.WindowFromPoint, HWND, wintypes.POINT)


def make_dpi_aware():
    """Per-monitor v2: the app reports physical screen pixels, so this process has to use them too."""
    SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))   # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
