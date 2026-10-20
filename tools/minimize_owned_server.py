"""Minimize only the explicitly selected dedicated server's own windows.

No process command line or window title is read, so RCON secrets cannot leak.
The server remains running; ordinary Steam client windows are not changed.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time

def minimize_server(pid, timeout=5.0):
    assert isinstance(pid,int) and pid>0
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    user=ctypes.WinDLL('user32',use_last_error=True)
    kernel.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]
    kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes=[wintypes.HANDLE,wintypes.DWORD,wintypes.LPWSTR,ctypes.POINTER(wintypes.DWORD)]
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    process=kernel.OpenProcess(0x1000,False,pid)
    if not process:raise RuntimeError('Selected server process is unavailable')
    try:
        path=ctypes.create_unicode_buffer(32768);length=wintypes.DWORD(len(path))
        if not kernel.QueryFullProcessImageNameW(process,0,path,ctypes.byref(length)):
            raise RuntimeError('Cannot verify selected server executable')
        expected=Path(r'D:\css\CS2\steamcmd\steamapps\common\Counter-Strike Source Dedicated Server\srcds_win64.exe')
        if Path(path.value)!=expected:raise RuntimeError('Refuse windows belonging to an unexpected executable')
    finally:kernel.CloseHandle(process)
    kernel.AttachConsole.argtypes=[wintypes.DWORD]
    kernel.AttachConsole.restype=wintypes.BOOL
    kernel.FreeConsole.restype=wintypes.BOOL
    kernel.GetConsoleWindow.restype=wintypes.HWND
    user.ShowWindowAsync.argtypes=[wintypes.HWND,ctypes.c_int]
    user.IsIconic.argtypes=[wintypes.HWND]
    user.IsWindowVisible.argtypes=[wintypes.HWND]
    user.GetWindowThreadProcessId.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.DWORD)]
    callback=ctypes.WINFUNCTYPE(wintypes.BOOL,wintypes.HWND,wintypes.LPARAM)
    deadline=time.monotonic()+timeout
    windows=set()
    attached=False
    try:
        while time.monotonic()<deadline:
            def collect(hwnd,_):
                owner=wintypes.DWORD()
                user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner))
                if owner.value==pid:windows.add(hwnd)
                return True
            user.EnumWindows(callback(collect),0)
            if not attached:
                kernel.FreeConsole()
                attached=bool(kernel.AttachConsole(pid))
            if attached:
                console=kernel.GetConsoleWindow()
                if console:windows.add(console)
            if windows:break
            time.sleep(.1)
        for window in windows:user.ShowWindowAsync(window,6)
        time.sleep(.12)
        rows=[{'hwnd':int(window),'minimized':bool(user.IsIconic(window)),
               'visible':bool(user.IsWindowVisible(window))} for window in windows]
        effective=bool(rows) and all(row['minimized'] or not row['visible'] for row in rows)
        return {'server_pid':pid,'windows':rows,'minimized_or_hidden':effective,
                'process_stopped':False,'client_window_changed':False}
    finally:
        if attached:kernel.FreeConsole()

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--pid',type=int,required=True)
    args=parser.parse_args();result=minimize_server(args.pid)
    print(json.dumps(result))
    raise SystemExit(0 if result['minimized_or_hidden'] else 1)
