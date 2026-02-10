#pragma once

#include <cstring>
#include <cwchar>
#include <shlobj.h>
#include <string>
#include <tlhelp32.h>
#include <windows.h>

#include "../core/logging.hpp"

namespace RobloxControl {
	inline void LogWarnSilent(const std::string &msg) { LOG(std::string("[WARN] ") + msg); }
	inline void LogErrorSilent(const std::string &msg) { LOG(std::string("[ERROR] ") + msg); }

	inline bool IsRobloxProcessName(const char *exeName) {
		static const char *kRobloxProcesses[]
			= {"RobloxPlayerBeta.exe",
			   "RobloxPlayerLauncher.exe",
			   "RobloxCrashHandler.exe",
			   "RobloxCrashHandler64.exe",
			   "RobloxPlayerInstaller.exe"};
		for (const char *proc : kRobloxProcesses) {
			if (_stricmp(exeName, proc) == 0) { return true; }
		}
		return false;
	}

	inline bool IsRobloxRunning() {
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE) { return false; }
		PROCESSENTRY32 pe {};
		pe.dwSize = sizeof(pe);
		bool running = false;
		if (Process32First(snap, &pe)) {
			do {
				if (IsRobloxProcessName(pe.szExeFile)) {
					running = true;
					break;
				}
			} while (Process32Next(snap, &pe));
		}
		CloseHandle(snap);
		return running;
	}

	inline void WaitForRobloxExit(int timeoutMs = 5000, int pollMs = 100) {
		int waited = 0;
		while (IsRobloxRunning() && waited < timeoutMs) {
			Sleep(pollMs);
			waited += pollMs;
		}
		if (IsRobloxRunning()) { LogWarnSilent("Roblox still running after kill wait timeout."); }
	}

	inline void KillRobloxProcesses() {
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		PROCESSENTRY32 pe {};
		pe.dwSize = sizeof(pe);
		if (hSnap == INVALID_HANDLE_VALUE) { return; }

		if (Process32First(hSnap, &pe)) {
			do {
				if (IsRobloxProcessName(pe.szExeFile)) {
					HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
					if (hProc) {
						TerminateProcess(hProc, 0);
						WaitForSingleObject(hProc, 2000);
						CloseHandle(hProc);
						LOG_INFO(std::string("Terminated Roblox process: ") + std::to_string(pe.th32ProcessID));
					} else {
						LogErrorSilent(
							std::string("Failed to open Roblox process for termination: ")
							+ std::to_string(pe.th32ProcessID) + " (Error: " + std::to_string(GetLastError()) + ")"
						);
					}
				}
			} while (Process32Next(hSnap, &pe));
		} else {
			LogErrorSilent(
				std::string("Process32First failed when trying to kill Roblox. (Error: ")
				+ std::to_string(GetLastError()) + ")"
			);
		}
		CloseHandle(hSnap);
		WaitForRobloxExit();
		LOG_INFO("Kill Roblox process completed.");
	}

	inline std::string WStringToString(const std::wstring &wstr) {
		if (wstr.empty()) { return std::string(); }
		int size_needed
			= WideCharToMultiByte(CP_UTF8, 0, &wstr[0], static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
		std::string strTo(size_needed, 0);
		WideCharToMultiByte(
			CP_UTF8,
			0,
			&wstr[0],
			static_cast<int>(wstr.size()),
			&strTo[0],
			size_needed,
			nullptr,
			nullptr
		);
		return strTo;
	}

	inline void NormalizeDeleteAttributes(const std::wstring &path) {
		DWORD attrs = GetFileAttributesW(path.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) { return; }
		if (attrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) {
			if (!SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL)) {
				LogWarnSilent(
					"Failed to normalize file attributes: " + WStringToString(path)
					+ " (Error: " + std::to_string(GetLastError()) + ")"
				);
			}
		}
	}

	inline bool DeleteFileWithRetry(const std::wstring &path, int attempts = 50, int delayMs = 100) {
		for (int i = 0; i < attempts; ++i) {
			NormalizeDeleteAttributes(path);
			if (DeleteFileW(path.c_str())) { return true; }
			DWORD err = GetLastError();
			if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) {
				LogErrorSilent(
					"Failed to delete file: " + WStringToString(path) + " (Error: " + std::to_string(err) + ")"
				);
				return false;
			}
			Sleep(delayMs);
		}
		LogErrorSilent("Timed out waiting to delete file: " + WStringToString(path));
		return false;
	}

	inline bool RemoveDirectoryWithRetry(const std::wstring &path, int attempts = 50, int delayMs = 100) {
		for (int i = 0; i < attempts; ++i) {
			NormalizeDeleteAttributes(path);
			if (RemoveDirectoryW(path.c_str())) { return true; }
			DWORD err = GetLastError();
			if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) {
				LogErrorSilent(
					"Failed to remove directory: " + WStringToString(path) + " (Error: " + std::to_string(err) + ")"
				);
				return false;
			}
			Sleep(delayMs);
		}
		LogErrorSilent("Timed out waiting to remove directory: " + WStringToString(path));
		return false;
	}

	inline void ClearDirectoryContents(const std::wstring &directoryPath) {
		std::wstring searchPath = directoryPath + L"\\*";
		WIN32_FIND_DATAW findFileData;
		HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);

		if (hFind == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();

			if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
				LOG_INFO(
					"ClearDirectoryContents: Directory to clear not found or is empty: "
					+ WStringToString(directoryPath)
				);
			} else {
				LogErrorSilent(
					"ClearDirectoryContents: Failed to find first file in directory: " + WStringToString(directoryPath)
					+ " (Error: " + std::to_string(error) + ")"
				);
			}
			return;
		}

		do {
			const std::wstring itemName = findFileData.cFileName;
			if (itemName == L"." || itemName == L"..") { continue; }

			std::wstring itemFullPath = directoryPath + L"\\" + itemName;

			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				ClearDirectoryContents(itemFullPath);

				if (RemoveDirectoryWithRetry(itemFullPath)) {
					LOG_INFO("ClearDirectoryContents: Removed sub-directory: " + WStringToString(itemFullPath));
				} else {
					LogErrorSilent(
						"ClearDirectoryContents: Failed to remove sub-directory: " + WStringToString(itemFullPath)
					);
				}
			} else {
				if (DeleteFileWithRetry(itemFullPath)) {
					LOG_INFO("ClearDirectoryContents: Deleted file: " + WStringToString(itemFullPath));
				} else {
					LogErrorSilent("ClearDirectoryContents: Failed to delete file: " + WStringToString(itemFullPath));
				}
			}
		} while (FindNextFileW(hFind, &findFileData) != 0);

		FindClose(hFind);

		DWORD lastError = GetLastError();
		if (lastError != ERROR_NO_MORE_FILES) {
			LogErrorSilent(
				"ClearDirectoryContents: Error during file iteration in directory: " + WStringToString(directoryPath)
				+ " (Error: " + std::to_string(lastError) + ")"
			);
		}
	}

	inline bool EqualsInsensitive(const std::wstring &a, const std::wstring &b) {
		return _wcsicmp(a.c_str(), b.c_str()) == 0;
	}

	inline void ClearRobloxCache() {
		LOG_INFO("Starting Roblox cache clearing process...");

		WCHAR localAppDataPath_c[MAX_PATH];
		if (!SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, localAppDataPath_c))) {
			LogErrorSilent("Failed to get Local AppData path. Aborting cache clear.");
			return;
		}
		std::wstring localAppDataPath_ws = localAppDataPath_c;

		auto directoryExists = [](const std::wstring &path) -> bool {
			DWORD attrib = GetFileAttributesW(path.c_str());
			return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
		};

		std::wstring robloxBasePath = localAppDataPath_ws + L"\\Roblox";
		if (!directoryExists(robloxBasePath)) {
			LOG_INFO("Roblox base directory not found, skipping: " + WStringToString(robloxBasePath));
			return;
		}

		const std::wstring keepDir = L"Versions";
		const std::wstring keepFile = L"GlobalBasicSettings_13.xml";

		std::wstring searchPath = robloxBasePath + L"\\*";
		WIN32_FIND_DATAW findFileData;
		HANDLE hFind = FindFirstFileW(searchPath.c_str(), &findFileData);
		if (hFind == INVALID_HANDLE_VALUE) {
			DWORD error = GetLastError();
			LogErrorSilent(
				"Failed to enumerate Roblox base directory: " + WStringToString(robloxBasePath)
				+ " (Error: " + std::to_string(error) + ")"
			);
			return;
		}

		do {
			const std::wstring itemName = findFileData.cFileName;
			if (itemName == L"." || itemName == L"..") { continue; }

			const bool isDir = (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			if (isDir && EqualsInsensitive(itemName, keepDir)) { continue; }
			if (!isDir && EqualsInsensitive(itemName, keepFile)) { continue; }

			std::wstring itemFullPath = robloxBasePath + L"\\" + itemName;
			if (isDir) {
				ClearDirectoryContents(itemFullPath);
				if (RemoveDirectoryWithRetry(itemFullPath)) {
					LOG_INFO("Removed directory: " + WStringToString(itemFullPath));
				} else {
					LogErrorSilent("Failed to remove directory: " + WStringToString(itemFullPath));
				}
			} else {
				if (DeleteFileWithRetry(itemFullPath)) {
					LOG_INFO("Deleted file: " + WStringToString(itemFullPath));
				} else {
					LogErrorSilent("Failed to delete file: " + WStringToString(itemFullPath));
				}
			}
		} while (FindNextFileW(hFind, &findFileData) != 0);

		FindClose(hFind);
		DWORD lastError = GetLastError();
		if (lastError != ERROR_NO_MORE_FILES) {
			LogErrorSilent(
				"Error during Roblox base directory iteration: " + WStringToString(robloxBasePath)
				+ " (Error: " + std::to_string(lastError) + ")"
			);
		}

		LOG_INFO("Roblox cache clearing process finished.");
	}
} // namespace RobloxControl
