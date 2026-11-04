"""Optional Windows file-drop adapter; setup.py owns all identification and writes."""
import sys

def attach(window,receive):
    if sys.platform!='win32':return lambda:None
    import ctypes
    from ctypes import wintypes as w
    user=ctypes.windll.user32;shell=ctypes.windll.shell32
    callback_type=ctypes.WINFUNCTYPE(ctypes.c_ssize_t,w.HWND,w.UINT,w.WPARAM,w.LPARAM)
    user.GetParent.argtypes=[w.HWND];user.GetParent.restype=w.HWND
    user.SetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int,ctypes.c_void_p];user.SetWindowLongPtrW.restype=ctypes.c_void_p
    user.CallWindowProcW.argtypes=[ctypes.c_void_p,w.HWND,w.UINT,w.WPARAM,w.LPARAM];user.CallWindowProcW.restype=ctypes.c_ssize_t
    shell.DragAcceptFiles.argtypes=[w.HWND,w.BOOL]
    shell.DragQueryFileW.argtypes=[w.HANDLE,w.UINT,w.LPWSTR,w.UINT];shell.DragQueryFileW.restype=w.UINT
    shell.DragFinish.argtypes=[w.HANDLE]
    window.update_idletasks();child=window.winfo_id();hwnd=user.GetParent(child) or child
    previous=None
    def message(handle,kind,wp,lp):
        if kind==0x233:
            paths=[]
            try:
                count=shell.DragQueryFileW(wp,0xffffffff,None,0)
                for i in range(min(count,10000)):
                    length=shell.DragQueryFileW(wp,i,None,0);buf=ctypes.create_unicode_buffer(length+1)
                    shell.DragQueryFileW(wp,i,buf,length+1);paths.append(buf.value)
                window.after_idle(lambda:receive(paths))
            finally:shell.DragFinish(wp)
            return 0
        return user.CallWindowProcW(previous,handle,kind,wp,lp)
    callback=callback_type(message)
    ctypes.set_last_error(0);previous=user.SetWindowLongPtrW(hwnd,-4,ctypes.cast(callback,ctypes.c_void_p))
    if not previous:raise OSError('Cannot attach Windows file-drop handler')
    shell.DragAcceptFiles(hwnd,True)
    def close():
        shell.DragAcceptFiles(hwnd,False);user.SetWindowLongPtrW(hwnd,-4,previous)
        # Keep the callback alive until after restoring the original procedure.
        return callback
    close.hwnd=hwnd
    return close
