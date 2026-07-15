#include <Windows.h>
#include <iostream>
#include <string>
#include <TlHelp32.h>
#include <cwchar>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <sddl.h>
#include <thread>
#include <chrono>
#include <vector>
#include <psapi.h>
#include <memoryapi.h>

#define FG_CYAN (FOREGROUND_BLUE | FOREGROUND_GREEN)
#define FG_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)
#define FG_YELLOW (FOREGROUND_RED | FOREGROUND_GREEN)
#define FG_WHITE (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

HANDLE hProcess = nullptr;
std::wstring targetProcessName;

const std::string INJECTOR_VERSION = "1.4";
const std::vector<std::wstring> SUPPORTED_GAMES = { L"cs2.exe",
	L"csgo.exe",
	L"RustClient.exe",
	L"gmod.exe"
};

namespace Helper {
	void SetConsoleColor(int color) {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		SetConsoleTextAttribute(hConsole, color);
	}

	void PrintBanner() {
		SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
		std::cout << "  ==================================" << std::endl;
		std::cout << "   ChanchitoInjector v" << INJECTOR_VERSION << std::endl;
		std::cout << "  ==================================" << std::endl;
		SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
		std::cout << "  :: ManualMap DLL injector" << std::endl;
		std::cout << "  :: Targets:" << std::endl;
		for (const std::wstring& game : SUPPORTED_GAMES) {
			std::wcout << L"     " << game << std::endl;
		}
		SetConsoleColor(FG_WHITE);
	}

	bool IsElevated() {
		BOOL fIsElevated = FALSE;
		HANDLE hToken = NULL;
		TOKEN_ELEVATION tokenElevation;
		DWORD dwSize;

		if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
			SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "OpenProcessToken failed: " << GetLastError() << std::endl;
			SetConsoleColor(FG_WHITE);
			return FALSE;
		}

		if (GetTokenInformation(hToken, TokenElevation, &tokenElevation, sizeof(tokenElevation), &dwSize)) {
			fIsElevated = tokenElevation.TokenIsElevated;
		}
		else {
			SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "GetTokenInformation failed: " << GetLastError() << std::endl;
			SetConsoleColor(FG_WHITE);
		}

		if (hToken) {
			CloseHandle(hToken);
		}
		return fIsElevated;
	}

	std::string GetFileNameFromPath(const std::string& path) {
		return std::filesystem::path(path).filename().string();
	}

	bool IsDigits(const std::string& str) {
		return std::all_of(str.begin(), str.end(), ::isdigit);
	}

	bool IsGameInSupportedList(const std::wstring& processName) {
		return std::any_of(SUPPORTED_GAMES.begin(), SUPPORTED_GAMES.end(),
			[&](const std::wstring& game) { return _wcsicmp(processName.c_str(), game.c_str()) == 0; });
	}

}

namespace ProcessUtils {
	HANDLE GetProcessByName(const std::wstring& processName) {
		DWORD desiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
		if (Helper::IsElevated()) {
			desiredAccess = PROCESS_ALL_ACCESS;
		}

		PROCESSENTRY32W entry;
		entry.dwSize = sizeof(PROCESSENTRY32W);

		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
		if (snapshot == INVALID_HANDLE_VALUE) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "CreateToolhelp32Snapshot failed: " << GetLastError() << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return NULL;
		}

		if (Process32FirstW(snapshot, &entry)) {
			do {
				if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) {
					DWORD processId = entry.th32ProcessID;
					HANDLE hProc = OpenProcess(desiredAccess, FALSE, processId);
					if (hProc != NULL) {
						CloseHandle(snapshot);
						targetProcessName = processName;
						return hProc;
					}
					else {
						Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
						std::wcerr << L"OpenProcess failed for process '" << processName << L"' (PID: " << processId << L"): " << GetLastError() << std::endl;
						Helper::SetConsoleColor(FG_WHITE);
					}
				}
			} while (Process32NextW(snapshot, &entry));
		}

		CloseHandle(snapshot);
		return NULL;
	}

	HANDLE GetProcessById(DWORD processId) {
		DWORD desiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
		if (Helper::IsElevated()) {
			desiredAccess = PROCESS_ALL_ACCESS;
		}
		HANDLE hProc = OpenProcess(desiredAccess, FALSE, processId);
		if (hProc == NULL) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "OpenProcess failed for PID " << processId << ": " << GetLastError() << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
		}
		else {
			HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
			if (snapshot != INVALID_HANDLE_VALUE) {
				PROCESSENTRY32W entry;
				entry.dwSize = sizeof(PROCESSENTRY32W);
				if (Process32FirstW(snapshot, &entry)) {
					do {
						if (entry.th32ProcessID == processId) {
							targetProcessName = entry.szExeFile;
							break;
						}
					} while (Process32NextW(snapshot, &entry));
				}
				CloseHandle(snapshot);
			}
			else
			{
				Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
				std::cerr << "Warning: Could not retrieve process name for PID " << processId << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				targetProcessName = std::to_wstring(processId);
			}
		}
		return hProc;
	}
}

namespace ModuleUtils {
	bool WaitForModules(HANDLE hProcess, const std::vector<std::wstring>& moduleNames, DWORD timeoutMs) {
		auto startTime = std::chrono::steady_clock::now();
		std::vector<HMODULE> hModules(1024);
		DWORD cbNeeded;

		while (true) {
			if (!EnumProcessModules(hProcess, hModules.data(), static_cast<DWORD>(hModules.size() * sizeof(HMODULE)), &cbNeeded)) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "EnumProcessModules failed: " << GetLastError() << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}

			if (cbNeeded > hModules.size() * sizeof(HMODULE)) {
				hModules.resize(cbNeeded / sizeof(HMODULE));
				continue;
			}

			int modulesFound = 0;
			for (const auto& moduleName : moduleNames) {
				for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); ++i) {
					wchar_t szModuleName[MAX_PATH];
					if (GetModuleFileNameExW(hProcess, hModules[i], szModuleName, MAX_PATH)) {
						if (_wcsicmp(szModuleName, moduleName.c_str()) == 0 || std::wstring(szModuleName).find(moduleName) != std::wstring::npos) {
							modulesFound++;
							break;
						}
					}
				}
			}

			if (modulesFound == moduleNames.size()) {
				return true;
			}

			auto currentTime = std::chrono::steady_clock::now();
			auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
			if (elapsedTime > timeoutMs) {
				Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
				std::cerr << "Timeout waiting for modules." << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

namespace MemoryUtils {
	LPVOID FindFreeMemoryRegion(HANDLE hProcess, SIZE_T size) {
		MEMORY_BASIC_INFORMATION mbi;
		LPVOID address = NULL;

		while (VirtualQueryEx(hProcess, address, &mbi, sizeof(mbi))) {
			if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
				return mbi.BaseAddress;
			}
			address = (LPVOID)((DWORD_PTR)mbi.BaseAddress + mbi.RegionSize);
		}

		return NULL;
	}
}

LPVOID ntOpenFile = GetProcAddress(LoadLibraryW(L"ntdll"), "NtOpenFile"); // https://github.com/v3ctra/load-lib-injector

namespace Injection {
	void bypass(HANDLE hProcess) // https://github.com/v3ctra/load-lib-injector
	{
		// Restore original NtOpenFile from external process
		//credits: Daniel Krupiñski(pozdro dla ciebie byczku <3)
		if (ntOpenFile) {
			char originalBytes[5];
			memcpy(originalBytes, ntOpenFile, 5);
			WriteProcessMemory(hProcess, ntOpenFile, originalBytes, 5, NULL);
		}
	}

	void backup(HANDLE hProcess) // https://github.com/v3ctra/load-lib-injector
	{
		if (ntOpenFile) {
			//So, when I patching first 5 bytes I need to backup them to 0? (I think)
			char originalBytes[5];
			memcpy(originalBytes, ntOpenFile, 5);
			WriteProcessMemory(hProcess, ntOpenFile, originalBytes, 0, NULL);
		}
	}

	bool InjectDll(const std::string& path, HANDLE hProcess) {
		std::filesystem::path dllPath = std::filesystem::absolute(path);
		std::string absoluteDllPath = dllPath.string();
		std::string dllFileName = Helper::GetFileNameFromPath(absoluteDllPath);

		Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
		std::wcout << L"Attempting to inject DLL: " << std::wstring(dllFileName.begin(), dllFileName.end()) << L" into process: " << targetProcessName << std::endl;
		Helper::SetConsoleColor(FG_WHITE);

		std::ifstream file(absoluteDllPath);
		if (!file.good()) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: DLL file not found: " << absoluteDllPath << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return false;
		}
		file.close();
		std::cout << "[*] DLL file found." << std::endl;

		if (dllFileName == "skeet.dll") {
			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << "Performing skeet-specific injection..." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			bypass(hProcess);

			VirtualAllocEx(hProcess, (LPVOID)0x43310000, 0x2FC000u, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); // for skeet
			VirtualAllocEx(hProcess, 0, 0x1000u, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); // for skeet

			LPVOID lpPathAddress = VirtualAllocEx(hProcess, nullptr, absoluteDllPath.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (lpPathAddress == nullptr) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: VirtualAllocEx failed (skeet path alloc): " << GetLastError() << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::cout << "Memory allocated for path at address: " << lpPathAddress << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			if (!WriteProcessMemory(hProcess, lpPathAddress, absoluteDllPath.c_str(), absoluteDllPath.size() + 1, nullptr)) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: WriteProcessMemory failed (skeet path write): " << GetLastError() << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, lpPathAddress, 0, MEM_RELEASE);
				return false;
			}

			std::cout << "[*] DLL path written successfully." << std::endl;

			HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
			if (!hKernel32) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: GetModuleHandleA failed for kernel32.dll" << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, lpPathAddress, 0, MEM_RELEASE);
				return false;
			}

			FARPROC lpLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
			if (!lpLoadLibraryA) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: GetProcAddress failed for LoadLibraryA" << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, lpPathAddress, 0, MEM_RELEASE);
				return false;
			}
			std::cout << "[*] LoadLibraryA address found." << std::endl;

			HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)lpLoadLibraryA, lpPathAddress, 0, nullptr);
			if (!hThread) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: CreateRemoteThread failed (skeet injection): " << GetLastError() << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, lpPathAddress, 0, MEM_RELEASE);
				return false;
			}
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::cout << "Remote thread created with handle: " << hThread << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			WaitForSingleObject(hThread, INFINITE);
			DWORD exitCode;
			GetExitCodeThread(hThread, &exitCode);

			std::cout << "DLL ";
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::wcout << std::wstring(dllFileName.begin(), dllFileName.end());
			Helper::SetConsoleColor(FG_WHITE);
			std::cout << " injected successfully into ";
			Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
			std::wcout << targetProcessName;
			Helper::SetConsoleColor(FG_WHITE);
			std::cout << ", Return code: ";
			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << exitCode << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			CloseHandle(hThread);
			backup(hProcess);
			std::cout << "[*] Injection completed (skeet)." << std::endl;
			return true;
		}
		else {
			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << "Allocating memory in target process..." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			LPVOID allocatedMem = VirtualAllocEx(hProcess, NULL, absoluteDllPath.size() + 1, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (!allocatedMem) {
				DWORD error = GetLastError();
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: VirtualAllocEx failed: " << error << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::cout << "Memory allocated at address: " << allocatedMem << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << "Writing DLL path to target process..." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			SIZE_T bytesWritten;
			if (!WriteProcessMemory(hProcess, allocatedMem, absoluteDllPath.c_str(), absoluteDllPath.size() + 1, &bytesWritten)) {
				DWORD error = GetLastError();
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: WriteProcessMemory failed: " << error << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
				return false;
			}
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::cout << "Successfully wrote " << bytesWritten << " bytes to target process." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << "Creating remote thread in target process..." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			HANDLE hThread = CreateRemoteThread(hProcess, 0, 0, (LPTHREAD_START_ROUTINE)LoadLibraryA, allocatedMem, 0, 0);
			if (!hThread) {
				DWORD error = GetLastError();
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Error: CreateRemoteThread failed: " << error << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
				return false;
			}
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::cout << "Remote thread created with handle: " << hThread << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			WaitForSingleObject(hThread, INFINITE);
			DWORD exitCode;
			GetExitCodeThread(hThread, &exitCode);

			std::cout << "DLL ";
			Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
			std::wcout << std::wstring(dllFileName.begin(), dllFileName.end());
			Helper::SetConsoleColor(FG_WHITE);
			std::cout << " injected successfully into ";
			Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
			std::wcout << targetProcessName;
			Helper::SetConsoleColor(FG_WHITE);
			std::cout << ", Return code: ";
			Helper::SetConsoleColor(FG_CYAN | FOREGROUND_INTENSITY);
			std::cout << exitCode << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			VirtualFreeEx(hProcess, allocatedMem, 0, MEM_RELEASE);
			CloseHandle(hThread);
			std::cout << "[*] Injection completed." << std::endl;
			return true;
		}
	}

	bool InjectAfterModulesLoaded(HANDLE hProcess, const std::string& dllPath, const std::vector<std::wstring>& moduleNames, DWORD timeoutMs) {
		if (!ModuleUtils::WaitForModules(hProcess, moduleNames, timeoutMs)) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Failed to wait for necessary modules." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return false;
		}

		Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
		std::cout << "All required modules found. Waiting 20 seconds..." << std::endl;
		Helper::SetConsoleColor(FG_WHITE);
		std::this_thread::sleep_for(std::chrono::seconds(20));

		return InjectDll(dllPath, hProcess);
	}
}

namespace HookBypass {
	void LoadLib() {
		if (!GetModuleHandleW(L"kernel32")) LoadLibraryW(L"kernel32");
		if (!GetModuleHandleW(L"ntdll")) LoadLibraryW(L"ntdll");
		if (!GetModuleHandleW(L"KernelBase")) LoadLibraryW(L"KernelBase");
	}

	BOOL UnhookMethod(const char* methodName, const wchar_t* dllName, PBYTE save_origin_bytes) {
		HMODULE hModule = GetModuleHandleW(dllName);
		if (!hModule) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::wcerr << L"Error: GetModuleHandleW failed for " << dllName << L" (" << GetLastError() << L")" << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		LPVOID oriMethodAddr = GetProcAddress(hModule, methodName);
		if (!oriMethodAddr) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: GetProcAddress failed for " << methodName << " in " << dllName << " (" << GetLastError() << ")" << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		PBYTE originalGameBytes[6];
		if (!ReadProcessMemory(hProcess, oriMethodAddr, originalGameBytes, sizeof(char) * 6, NULL)) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: ReadProcessMemory failed for " << methodName << " in " << dllName << ": " << GetLastError() << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}

		if (save_origin_bytes != nullptr) {
			memcpy(save_origin_bytes, originalGameBytes, sizeof(char) * 6);
		}


		PBYTE originalDllBytes[6];
		memcpy(originalDllBytes, oriMethodAddr, sizeof(char) * 6);
		if (!WriteProcessMemory(hProcess, oriMethodAddr, originalDllBytes, sizeof(char) * 6, NULL)) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: WriteProcessMemory failed for " << methodName << " in " << dllName << ": " << GetLastError() << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		return TRUE;
	}

	BOOL RestoreOriginalHook(const char* methodName, const wchar_t* dllName, PBYTE save_origin_bytes) {
		HMODULE hModule = GetModuleHandleW(dllName);
		if (!hModule) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::wcerr << L"Error: GetModuleHandleW failed for " << dllName << L" (" << GetLastError() << L")" << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		LPVOID oriMethodAddr = GetProcAddress(hModule, methodName);
		if (!oriMethodAddr) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: GetProcAddress failed for " << methodName << " in " << dllName << " (" << GetLastError() << ")" << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		if (!WriteProcessMemory(hProcess, oriMethodAddr, save_origin_bytes, sizeof(char) * 6, NULL)) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Error: WriteProcessMemory failed for " << methodName << " in " << dllName << ": " << GetLastError() << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return FALSE;
		}
		return TRUE;
	}

	enum MethodNum {
		LOADLIBEXW = 1,
		VIRALLOC = 2,
		FREELIB = 3,
		LOADLIBEXA = 4,
		LOADLIBW = 5,
		LOADLIBA = 6,
		VIRALLOCEX = 7,
		LDRLOADDLL = 10,
		NTOPENFILE = 11,
		VIRPROT = 12,
		CREATPROW = 13,
		CREATPROA = 14,
		VIRPROTEX = 15,
		FREELIB_ = 16,
		LOADLIBEXA_ = 17,
		LOADLIBEXW_ = 18,
		RESUMETHREAD = 19,
	};
	BYTE originalGameBytess[30][6];
	BOOL BypassCSGO_hook(bool disableAll = false) {
		BOOL res = TRUE;
		if (!disableAll)
		{
			res &= UnhookMethod("LoadLibraryExW", L"kernel32", originalGameBytess[LOADLIBEXW]);
			res &= UnhookMethod("VirtualAlloc", L"kernel32", originalGameBytess[VIRALLOC]);
			res &= UnhookMethod("FreeLibrary", L"kernel32", originalGameBytess[FREELIB]);
			res &= UnhookMethod("LoadLibraryExA", L"kernel32", originalGameBytess[LOADLIBEXA]);
			res &= UnhookMethod("LoadLibraryW", L"kernel32", originalGameBytess[LOADLIBW]);
			res &= UnhookMethod("LoadLibraryA", L"kernel32", originalGameBytess[LOADLIBA]);
			res &= UnhookMethod("VirtualAllocEx", L"kernel32", originalGameBytess[VIRALLOCEX]);
			res &= UnhookMethod("LdrLoadDll", L"ntdll", originalGameBytess[LDRLOADDLL]);
			res &= UnhookMethod("NtOpenFile", L"ntdll", originalGameBytess[NTOPENFILE]);
			res &= UnhookMethod("VirtualProtect", L"kernel32", originalGameBytess[VIRPROT]);
			res &= UnhookMethod("CreateProcessW", L"kernel32", originalGameBytess[CREATPROW]);
			res &= UnhookMethod("CreateProcessA", L"kernel32", originalGameBytess[CREATPROA]);
			res &= UnhookMethod("VirtualProtectEx", L"kernel32", originalGameBytess[VIRPROTEX]);
			res &= UnhookMethod("FreeLibrary", L"KernelBase", originalGameBytess[FREELIB_]);
			res &= UnhookMethod("LoadLibraryExA", L"KernelBase", originalGameBytess[LOADLIBEXA_]);
			res &= UnhookMethod("LoadLibraryExW", L"KernelBase", originalGameBytess[LOADLIBEXW_]);
			res &= UnhookMethod("ResumeThread", L"KernelBase", originalGameBytess[RESUMETHREAD]);
		}
		else {
			res &= UnhookMethod("LoadLibraryExW", L"kernel32", nullptr);
			res &= UnhookMethod("VirtualAlloc", L"kernel32", nullptr);
			res &= UnhookMethod("FreeLibrary", L"kernel32", nullptr);
			res &= UnhookMethod("LoadLibraryExA", L"kernel32", nullptr);
			res &= UnhookMethod("LoadLibraryW", L"kernel32", nullptr);
			res &= UnhookMethod("LoadLibraryA", L"kernel32", nullptr);
			res &= UnhookMethod("VirtualAllocEx", L"kernel32", nullptr);
			res &= UnhookMethod("LdrLoadDll", L"ntdll", nullptr);
			res &= UnhookMethod("NtOpenFile", L"ntdll", nullptr);
			res &= UnhookMethod("VirtualProtect", L"kernel32", nullptr);
			res &= UnhookMethod("CreateProcessW", L"kernel32", nullptr);
			res &= UnhookMethod("CreateProcessA", L"kernel32", nullptr);
			res &= UnhookMethod("VirtualProtectEx", L"kernel32", nullptr);
			res &= UnhookMethod("FreeLibrary", L"KernelBase", nullptr);
			res &= UnhookMethod("LoadLibraryExA", L"KernelBase", nullptr);
			res &= UnhookMethod("LoadLibraryExW", L"KernelBase", nullptr);
			res &= UnhookMethod("ResumeThread", L"KernelBase", nullptr);

		}

		return res;
	}

	BOOL RestoreCSGO_hook() {
		BOOL res = TRUE;
		res &= RestoreOriginalHook("LoadLibraryExW", L"kernel32", originalGameBytess[LOADLIBEXW]);
		res &= RestoreOriginalHook("VirtualAlloc", L"kernel32", originalGameBytess[VIRALLOC]);
		res &= RestoreOriginalHook("FreeLibrary", L"kernel32", originalGameBytess[FREELIB]);
		res &= RestoreOriginalHook("LoadLibraryExA", L"kernel32", originalGameBytess[LOADLIBEXA]);
		res &= RestoreOriginalHook("LoadLibraryW", L"kernel32", originalGameBytess[LOADLIBW]);
		res &= RestoreOriginalHook("LoadLibraryA", L"kernel32", originalGameBytess[LOADLIBA]);
		res &= RestoreOriginalHook("VirtualAllocEx", L"kernel32", originalGameBytess[VIRALLOCEX]);
		res &= RestoreOriginalHook("LdrLoadDll", L"ntdll", originalGameBytess[LDRLOADDLL]);
		res &= RestoreOriginalHook("NtOpenFile", L"ntdll", originalGameBytess[NTOPENFILE]);
		res &= RestoreOriginalHook("VirtualProtect", L"kernel32", originalGameBytess[VIRPROT]);
		res &= RestoreOriginalHook("CreateProcessW", L"kernel32", originalGameBytess[CREATPROW]);
		res &= RestoreOriginalHook("CreateProcessA", L"kernel32", originalGameBytess[CREATPROA]);
		res &= RestoreOriginalHook("VirtualProtectEx", L"kernel32", originalGameBytess[VIRPROTEX]);
		res &= RestoreOriginalHook("FreeLibrary", L"KernelBase", originalGameBytess[FREELIB_]);
		res &= RestoreOriginalHook("LoadLibraryExA", L"KernelBase", originalGameBytess[LOADLIBEXA_]);
		res &= RestoreOriginalHook("LoadLibraryExW", L"KernelBase", originalGameBytess[LOADLIBEXW_]);
		res &= RestoreOriginalHook("ResumeThread", L"KernelBase", originalGameBytess[RESUMETHREAD]);
		return res;
	}
}

namespace SteamInjection {
	bool InjectSteamDll(const std::string& dllPath) {
		std::string cheatName = Helper::GetFileNameFromPath(dllPath);
		cheatName = cheatName.substr(0, cheatName.find_last_of("."));
		std::string steamDllName = "steam_" + cheatName + ".dll";
		std::filesystem::path injectorPath = std::filesystem::absolute(dllPath).parent_path();
		std::filesystem::path steamDllPath = injectorPath / steamDllName;
		std::string steamDllPathStr = steamDllPath.string();

		if (std::filesystem::exists(steamDllPath)) {
			Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
			std::cout << "Found Steam DLL: " << steamDllName << std::endl;
			Helper::SetConsoleColor(FG_WHITE);

			HANDLE hSteamProcess = ProcessUtils::GetProcessByName(L"steam.exe");
			if (hSteamProcess) {
				targetProcessName = L"steam.exe";
				if (!Injection::InjectDll(steamDllPathStr, hSteamProcess)) {
					Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
					std::cerr << "Failed to inject into steam.exe" << std::endl;
					Helper::SetConsoleColor(FG_WHITE);
					CloseHandle(hSteamProcess);
					return false;
				}
				else {
					Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
					std::cout << "Successfully injected " << steamDllName << " into steam.exe" << std::endl;
					Helper::SetConsoleColor(FG_WHITE);
					CloseHandle(hSteamProcess);
					return true;
				}
			}
			else {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Could not find steam.exe. Skipping Steam DLL injection." << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}
		}
		else {
			return false;
		}
	}
}

namespace GameSpecific {
	std::vector<std::wstring> GetModulesToWaitFor(const std::wstring& processName) {
		if (processName == L"cs2.exe") {
			return {
				L"client.dll",
					L"engine2.dll",
					L"server.dll"
			};
		}
		else if (processName == L"csgo.exe") {
			return {
			  L"client.dll",
			  L"engine.dll",
			  L"server.dll"
			};
		}
		else if (processName == L"RustClient.exe")
		{
			return {
			   L"GameAssembly.dll",
			   L"UnityPlayer.dll"
			};
		}
		else if (processName == L"gmod.exe")
		{
			return {
			   L"client.dll",
			   L"engine.dll"
			};
		}
		return {};
	}

	bool ApplyHookBypass(const std::wstring& processName, bool disableHooks) {
		if (processName == L"cs2.exe" || processName == L"csgo.exe") {
			if (!HookBypass::BypassCSGO_hook(disableHooks)) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Failed to bypass VAC hooks!" << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				if (!disableHooks) {
					if (!HookBypass::RestoreCSGO_hook()) {
						Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
						std::cerr << "Failed to restore VAC hooks.  This is VERY dangerous." << std::endl;
						Helper::SetConsoleColor(FG_WHITE);
					}
				}
				return false;
			}
			if (!disableHooks)
			{
				std::cout << "[*] VAC hooks bypassed." << std::endl;
			}
			else
			{
				std::cout << "[*] VAC bypass not applied as the target process is steam." << std::endl;
			}

		}
		else
		{
			return true;
		}
		return true;
	}

	bool RestoreHookBypass(const std::wstring& processName) {
		if (processName == L"cs2.exe" || processName == L"csgo.exe") {
			if (!HookBypass::RestoreCSGO_hook()) {
				Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
				std::cerr << "Warning: Failed to restore VAC hooks! This may result in a VAC ban." << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return false;
			}
			else {
				std::cout << "[*] VAC hooks restored." << std::endl;
				return true;
			}
		}
		return true;
	}
}

int main(int argc, char* argv[]) {
	SetConsoleTitleA("ChInjector");

	if (argc == 1) {
		Helper::PrintBanner();
	}

	if (Helper::IsElevated()) {
		Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
		std::cout << "[*] Injector is running with administrator privileges." << std::endl;
	}

	Helper::SetConsoleColor(FG_WHITE);
	std::cout << std::endl;

	std::string dllPath;
	std::string processNameOrId;
	std::string exeName = std::filesystem::path(argv[0]).filename().string();

	if (argc == 2) {
		dllPath = argv[1];
	}
	else if (argc == 3) {
		processNameOrId = argv[1];
		dllPath = argv[2];
	}
	else {
		Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
		std::cerr << "Usage: " << exeName << " <dll_path>\nOR: " << exeName << " <process_name_or_PID> <dll_path>" << std::endl << std::endl;
		Helper::SetConsoleColor(FG_WHITE);

		system("pause");
		return 1;
	}

	bool injectedIntoSteam = false;

	bool isSupportedGame = false;
	if (!processNameOrId.empty()) {
		std::wstring wProcessName(processNameOrId.begin(), processNameOrId.end());
		isSupportedGame = Helper::IsGameInSupportedList(wProcessName);
	}
	else {
		isSupportedGame = true;
	}

	if (isSupportedGame) {
		injectedIntoSteam = SteamInjection::InjectSteamDll(dllPath);
	}

	if (processNameOrId.empty()) {
		Helper::SetConsoleColor(FG_YELLOW | FOREGROUND_INTENSITY);
		std::cout << "Please launch the target process..." << std::endl;
		Helper::SetConsoleColor(FG_WHITE);

		bool gameFound = false;
		for (int i = 0; i < 60; ++i) {
			for (const auto& game : SUPPORTED_GAMES) {
				hProcess = ProcessUtils::GetProcessByName(game);
				if (hProcess) {
					targetProcessName = game;
					gameFound = true;
					break;
				}
			}
			if (gameFound)
				break;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		}

		if (!gameFound) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Timeout: Target process not launched within the waiting period." << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return 1;
		}
	}
	else {
		if (Helper::IsDigits(processNameOrId)) {
			try {
				DWORD processId = std::stoi(processNameOrId);
				hProcess = ProcessUtils::GetProcessById(processId);
			}
			catch (const std::invalid_argument&) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Invalid process ID: " << processNameOrId << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return 1;
			}
			catch (const std::out_of_range&) {
				Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
				std::cerr << "Process ID out of range: " << processNameOrId << std::endl;
				Helper::SetConsoleColor(FG_WHITE);
				return 1;
			}
		}
		else {
			std::wstring wProcessName(processNameOrId.begin(), processNameOrId.end());
			hProcess = ProcessUtils::GetProcessByName(wProcessName);
		}

		if (!hProcess) {
			Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
			std::cerr << "Can not find process: " << processNameOrId << std::endl;
			Helper::SetConsoleColor(FG_WHITE);
			return 1;
		}
	}

	Helper::SetConsoleColor(FG_MAGENTA | FOREGROUND_INTENSITY);
	std::wcout << L"Process found: " << targetProcessName << std::endl;
	Helper::SetConsoleColor(FG_WHITE);

	std::string dllFileName = Helper::GetFileNameFromPath(dllPath);

	bool disableBypass = injectedIntoSteam;
	if (isSupportedGame && dllFileName != "skeet.dll")
	{
		if (!GameSpecific::ApplyHookBypass(targetProcessName, disableBypass))
		{
			CloseHandle(hProcess);
			return 1;
		}

	}

	if (!Injection::InjectDll(dllPath, hProcess)) {
		Helper::SetConsoleColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
		std::cerr << "Failed to inject DLL directly." << std::endl;
		Helper::SetConsoleColor(FG_WHITE);
		GameSpecific::RestoreHookBypass(targetProcessName);
		CloseHandle(hProcess);
		return 1;
	}

	if ((isSupportedGame && !disableBypass) && dllFileName != "skeet.dll")
	{
		GameSpecific::RestoreHookBypass(targetProcessName);
	}

	CloseHandle(hProcess);
	return 0;
}

