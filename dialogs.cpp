#include "dialogs.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

std::string openFileDlg(const char *filter) {
	char filename[MAX_PATH] = "";
	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	if (GetOpenFileNameA(&ofn)) return std::string(filename);
	return "";
}

std::string saveFileDlg(const char *filter) {
	char filename[MAX_PATH] = "";
	OPENFILENAMEA ofn = {};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = GetActiveWindow();
	ofn.lpstrFilter = filter;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
	if (GetSaveFileNameA(&ofn)) return std::string(filename);
	return "";
}

#else
std::string openFileDlg(const char *) { return ""; }
std::string saveFileDlg(const char *) { return ""; }
#endif
