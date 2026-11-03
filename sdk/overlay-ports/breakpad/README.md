# Breakpad Windows shutdown policy

This overlay retains vcpkg's 2024-02-16#1 port and its pinned upstream archive and
patches. Port metadata/build files derive from that MIT-licensed vcpkg port;
see `VCPKG-LICENSE.txt`. The getcontext compile probe is copied unchanged from
vcpkg; it is a template, not application C++ source.

The only library compilation change enables the upstream
`BREAKPAD_NO_TERMINATE_THREAD` switch for `libbreakpad_client`. Its default Windows
destructor uses `TerminateThread`, which can leave the loader lock abandoned
when short-lived tools exit while the handler thread is starting. Use cooperative
semaphore shutdown and thread joining instead. No upstream C++ files are edited.

The installed CMake config exports `CYBER_BREAKPAD_GRACEFUL_SHUTDOWN`, and a
marker file lets dependency preparation distinguish this build from the unsafe
default cache entry. The crash adapter requires the marked build. Exception
handlers are installed only in application EXEs, never from `DllMain`.
