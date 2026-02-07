#pragma once
#ifdef _WIN32
#	include <windows.h>

namespace MultiInstance {
	inline HANDLE g_mutex = nullptr;
	inline bool g_mutexOwned = false;

	inline void Enable() {
		if (!g_mutex) {
			g_mutex = CreateMutexW(nullptr, TRUE, L"ROBLOX_singletonEvent");
			if (g_mutex) { g_mutexOwned = (GetLastError() != ERROR_ALREADY_EXISTS); }
		}
	}

	inline void Disable() {
		if (g_mutex) {
			if (g_mutexOwned) { ReleaseMutex(g_mutex); }
			CloseHandle(g_mutex);
			g_mutex = nullptr;
			g_mutexOwned = false;
		}
	}
} // namespace MultiInstance
#endif
