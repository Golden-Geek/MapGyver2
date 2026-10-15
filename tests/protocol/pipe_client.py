# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent Windows SDK client fixture; no app codec or server implementation."""
import ctypes as C
from ctypes import wintypes as W
import json
import struct
import time

K = C.WinDLL('kernel32', use_last_error=True)
A = C.WinDLL('advapi32', use_last_error=True)
INVALID = C.c_void_p(-1).value
PENDING, INCOMPLETE, NOT_FOUND = 997, 996, 1168
retained_io = []  # Only used on an unexpected OS cancellation timeout; never free pending buffers.

class Overlapped(C.Structure):
    _fields_ = [('Internal', C.c_size_t), ('InternalHigh', C.c_size_t),
                ('Offset', W.DWORD), ('OffsetHigh', W.DWORD), ('hEvent', W.HANDLE)]

def api(dll, name, restype, *args):
    value = getattr(dll, name); value.restype = restype; value.argtypes = args; return value

create_file = api(K, 'CreateFileW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE)
close_handle = api(K, 'CloseHandle', W.BOOL, W.HANDLE)
create_event = api(K, 'CreateEventW', W.HANDLE, C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR)
wait = api(K, 'WaitForSingleObject', W.DWORD, W.HANDLE, W.DWORD)
read_file = api(K, 'ReadFile', W.BOOL, W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(Overlapped))
write_file = api(K, 'WriteFile', W.BOOL, W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.POINTER(Overlapped))
get_result = api(K, 'GetOverlappedResult', W.BOOL, W.HANDLE, C.POINTER(Overlapped), C.POINTER(W.DWORD), W.BOOL)
cancel = api(K, 'CancelIoEx', W.BOOL, W.HANDLE, C.POINTER(Overlapped))
get_process = api(K, 'GetCurrentProcess', W.HANDLE)

class Pipe:
    def __init__(self, endpoint, timeout=5):
        deadline = time.monotonic()+timeout
        self.handle, self.retain = INVALID, False
        while time.monotonic() < deadline:
            self.handle = create_file(endpoint, 0xC0020000, 0, None, 3, 0x40000000, None)
            if self.handle != INVALID: return
            error = C.get_last_error()
            if error not in (2, 231): raise C.WinError(error)
            time.sleep(.01)
        raise TimeoutError('Native pipe connection timed out')

    def close(self):
        if self.handle != INVALID and not self.retain:
            close_handle(self.handle); self.handle = INVALID

    def io(self, data=None, size=0, timeout=7):
        event = create_event(None, True, False, None)
        if not event: raise C.WinError(C.get_last_error())
        operation = Overlapped(); operation.hEvent = event
        buffer = C.create_string_buffer(data, len(data)) if data is not None else C.create_string_buffer(size)
        count = W.DWORD(); pending = False
        try:
            ok = (write_file if data is not None else read_file)(self.handle, buffer, len(data) if data is not None else size, C.byref(count), C.byref(operation))
            if not ok:
                error = C.get_last_error()
                if error != PENDING: raise C.WinError(error)
                pending = True
                result = wait(event, int(timeout*1000))
                if result == 258:
                    if not cancel(self.handle, C.byref(operation)) and C.get_last_error() != NOT_FOUND:
                        raise C.WinError(C.get_last_error())
                    if wait(event, 5000) != 0:
                        retained_io.append((self.handle, event, operation, buffer)); self.retain = True
                        raise AssertionError('OS cancellation did not complete; pending client IO retained')
                    get_result(self.handle, C.byref(operation), C.byref(count), False); pending = False
                    raise TimeoutError('Native pipe IO timed out')
                if result != 0: raise C.WinError(C.get_last_error())
                ok = get_result(self.handle, C.byref(operation), C.byref(count), False); pending = False
                if not ok: raise C.WinError(C.get_last_error())
            if count.value == 0: raise EOFError('Native pipe closed')
            return count.value if data is not None else buffer.raw[:count.value]
        finally:
            if pending and not self.retain:
                cancel(self.handle, C.byref(operation))
                if wait(event, 5000) != 0:
                    retained_io.append((self.handle, event, operation, buffer)); self.retain = True
                else:
                    get_result(self.handle, C.byref(operation), C.byref(count), False)
            if not self.retain: close_handle(event)

    def write(self, data):
        offset = 0
        while offset < len(data): offset += self.io(data=data[offset:])

    def send(self, value, fragments=False):
        data = json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode('utf-8')
        frame = struct.pack('<I', len(data))+data
        if fragments:
            for chunk in (frame[:1], frame[1:3], frame[3:7], frame[7:]): self.write(chunk)
        else: self.write(frame)

    def exact(self, size):
        result = bytearray()
        while len(result) < size: result.extend(self.io(size=min(65536, size-len(result))))
        return bytes(result)

    def receive(self):
        size, = struct.unpack('<I', self.exact(4))
        assert 0 < size <= 4194304, size
        return json.loads(self.exact(size).decode('utf-8'))

    def reply(self, request_id):
        for _ in range(64):
            value = self.receive()
            if value['type'] == 'reply':
                assert value['requestId'] == request_id, value
                return value
            assert value['type'] == 'notification', value
        raise AssertionError('Reply starved by notifications')

def acl_current_user_only(pipe):
    open_token = api(A, 'OpenProcessToken', W.BOOL, W.HANDLE, W.DWORD, C.POINTER(W.HANDLE))
    token_info = api(A, 'GetTokenInformation', W.BOOL, W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD))
    get_security = api(A, 'GetSecurityInfo', W.DWORD, W.HANDLE, C.c_int, W.DWORD, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p), C.c_void_p, C.POINTER(C.c_void_p))
    get_ace = api(A, 'GetAce', W.BOOL, C.c_void_p, W.DWORD, C.POINTER(C.c_void_p))
    equal_sid = api(A, 'EqualSid', W.BOOL, C.c_void_p, C.c_void_p)
    local_free = api(K, 'LocalFree', C.c_void_p, C.c_void_p)
    class Acl(C.Structure):
        _fields_ = [('revision', W.BYTE), ('sbz1', W.BYTE), ('size', W.WORD), ('count', W.WORD), ('sbz2', W.WORD)]
    token, security, acl = W.HANDLE(), C.c_void_p(), C.c_void_p()
    if not open_token(get_process(), 8, C.byref(token)): raise C.WinError(C.get_last_error())
    try:
        size = W.DWORD(); token_info(token, 1, None, 0, C.byref(size))
        storage = C.create_string_buffer(size.value)
        if not token_info(token, 1, storage, size, C.byref(size)): raise C.WinError(C.get_last_error())
        sid = C.cast(storage, C.POINTER(C.c_void_p))[0]
        code = get_security(pipe.handle, 6, 4, None, None, C.byref(acl), None, C.byref(security))
        if code: raise C.WinError(code)
        assert acl.value and C.cast(acl, C.POINTER(Acl)).contents.count == 1, 'Pipe DACL is not one explicit user ACE'
        ace = C.c_void_p()
        if not get_ace(acl, 0, C.byref(ace)): raise C.WinError(C.get_last_error())
        assert C.c_ubyte.from_address(ace.value).value == 0, 'Pipe ACE is not an allow ACE'
        assert equal_sid(sid, ace.value+8), 'Pipe ACE does not match the current user'
        return {'aceCount': 1, 'currentUserOnly': True}
    finally:
        if security: local_free(security)
        close_handle(token)

def handle_count(pid):
    open_process = api(K, 'OpenProcess', W.HANDLE, W.DWORD, W.BOOL, W.DWORD)
    count_handles = api(K, 'GetProcessHandleCount', W.BOOL, W.HANDLE, C.POINTER(W.DWORD))
    process = open_process(0x1000, False, pid)
    if not process: raise C.WinError(C.get_last_error())
    try:
        count = W.DWORD()
        if not count_handles(process, C.byref(count)): raise C.WinError(C.get_last_error())
        return count.value
    finally: close_handle(process)

def first_instance_probe(endpoint):
    """Separate owned namespace: distinguish first-instance and full-pool errors."""
    create_pipe = api(K, 'CreateNamedPipeW', W.HANDLE, W.LPCWSTR, W.DWORD, W.DWORD,
                      W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_void_p)
    first = create_pipe(endpoint, 3 | 0x40000000 | 0x80000, 8, 2, 4096, 4096, 5000, None)
    if first == INVALID: raise C.WinError(C.get_last_error())
    second = INVALID
    try:
        duplicate = create_pipe(endpoint, 3 | 0x40000000 | 0x80000, 8, 2, 4096, 4096, 5000, None)
        error_available = C.get_last_error()
        if duplicate != INVALID: close_handle(duplicate)
        assert duplicate == INVALID and error_available == 5, (duplicate, error_available)
        second = create_pipe(endpoint, 3 | 0x40000000, 8, 2, 4096, 4096, 5000, None)
        if second == INVALID: raise C.WinError(C.get_last_error())
        duplicate = create_pipe(endpoint, 3 | 0x40000000 | 0x80000, 8, 2, 4096, 4096, 5000, None)
        error_full = C.get_last_error()
        if duplicate != INVALID: close_handle(duplicate)
        assert duplicate == INVALID and error_full in (5, 231), (duplicate, error_full)
        return {'availableSlotCollision': error_available, 'fullPoolCollision': error_full}
    finally:
        if second != INVALID: close_handle(second)
        close_handle(first)
