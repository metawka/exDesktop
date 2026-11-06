import os
import sys
import threading

BASE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [BASE, os.path.join(BASE, 'lib')] + [p for p in [os.environ.get('EXD_TGWS_LIB')] if p]


def watch(pid):
    if os.name == 'nt':
        import ctypes
        handle = ctypes.windll.kernel32.OpenProcess(0x00100000, False, pid)
        if handle:
            ctypes.windll.kernel32.WaitForSingleObject(handle, 0xFFFFFFFF)
        os._exit(0)
    import time
    while True:
        try:
            os.kill(pid, 0)
        except OSError:
            os._exit(0)
        time.sleep(2)


def run():
    pid = int(sys.argv[1])
    sys.argv = ['tg-ws-proxy'] + sys.argv[2:]
    threading.Thread(target=watch, args=(pid,), daemon=True).start()
    from proxy.tg_ws_proxy import main
    main()


run()
