import ctypes,time
from ctypes import wintypes
user=ctypes.WinDLL('user32',use_last_error=True)
user.GetForegroundWindow.restype=wintypes.HWND
user.SetForegroundWindow.argtypes=[wintypes.HWND]
user.ShowWindow.argtypes=[wintypes.HWND,ctypes.c_int]
user.BringWindowToTop.argtypes=[wintypes.HWND]
user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.c_void_p]
user.AttachThreadInput.argtypes=[wintypes.DWORD,wintypes.DWORD,wintypes.BOOL]
kernel=ctypes.WinDLL('kernel32',use_last_error=True)
class KEYBDINPUT(ctypes.Structure):
    _fields_=[('wVk',wintypes.WORD),('wScan',wintypes.WORD),('dwFlags',wintypes.DWORD),('time',wintypes.DWORD),('dwExtraInfo',ctypes.c_size_t)]
class MOUSEINPUT(ctypes.Structure):
    _fields_=[('dx',wintypes.LONG),('dy',wintypes.LONG),('mouseData',wintypes.DWORD),('dwFlags',wintypes.DWORD),('time',wintypes.DWORD),('dwExtraInfo',ctypes.c_size_t)]
class UNION(ctypes.Union):
    _fields_=[('ki',KEYBDINPUT),('mi',MOUSEINPUT)]
class INPUT(ctypes.Structure):
    _fields_=[('type',wintypes.DWORD),('u',UNION)]
user.SendInput.argtypes=[wintypes.UINT,ctypes.POINTER(INPUT),ctypes.c_int]
def press(key):
    windows=[]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    def collect(hwnd,_):
        title=ctypes.create_unicode_buffer(256);user.GetWindowTextW(hwnd,title,256)
        if title.value=='Counter-Strike Source - Direct3D 9 - 64 Bit' and user.IsWindowVisible(hwnd):windows.append(hwnd)
        return True
    user.EnumWindows(callback(collect),0)
    if len(windows)!=1:raise RuntimeError('Cannot uniquely identify the running CSS window')
    hwnd=windows[0];user.ShowWindow(hwnd,9);user.SetForegroundWindow(hwnd);time.sleep(.2)
    if user.GetForegroundWindow()!=hwnd:
        current=kernel.GetCurrentThreadId()
        foreground=user.GetWindowThreadProcessId(user.GetForegroundWindow(),None)
        attached=foreground!=current and user.AttachThreadInput(current,foreground,True)
        try:
            user.BringWindowToTop(hwnd);user.SetForegroundWindow(hwnd)
        finally:
            if attached:user.AttachThreadInput(current,foreground,False)
        time.sleep(.2)
    if user.GetForegroundWindow()!=hwnd:raise RuntimeError('CSS is not foreground; refusing to send input elsewhere')
    scan=user.MapVirtualKeyW(13 if key.upper()=='ENTER' else ord(key.upper()),0)
    for flag in [8,10]:
        item=INPUT(type=1);item.u.ki=KEYBDINPUT(0,scan,flag,0,0)
        if user.SendInput(1,ctypes.byref(item),ctypes.sizeof(INPUT))!=1:raise ctypes.WinError(ctypes.get_last_error())
        time.sleep(.12)
    print('Physical keyboard input sent to CSS:',key,flush=True)
