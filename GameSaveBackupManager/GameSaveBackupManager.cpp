#define NOMINMAX
#include <windows.h>
#include <string>
#include <ShlObj.h> // Required for SHGetKnownFolderPath
#include <KnownFolders.h> // For FOLDERID_LocalAppData
#include <comdef.h>
#include <stdio.h>
#include <tchar.h>
#include <signal.h>
#include <ctime>
#include <cmath>
#include <time.h>
#include <locale>
#include <codecvt> // For string conversions
#include <conio.h>
#include <process.h>
#include <iostream>
#include <fstream>
#include <filesystem> // Requires C++17
#include <thread>
#include <sys/types.h>
#include <sys/stat.h> // For getting file modification time
#include <vector>
#include <chrono>
#include <limits>    // For numeric_limits
#include <stdexcept> // For exception handling
#include <atomic>    // For safely stopping the thread
#include <algorithm> // For std::sort, std::min, std::transform
#include <iomanip>   // For std::setw
#include <sstream>   // For string splitting
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Shell32.lib") // For SHGetKnownFolderPath

namespace fs = std::filesystem;
using namespace std;
using namespace std::chrono_literals;

// --- Global Settings ---
wstring g_GoogleDrivePath = L"";
int g_LocalAutoSaveLimit = 20;
int g_LocalManualSaveLimit = 0; // 0 means keep all
int g_CloudAutoSaveLimit = 10;
int g_CloudManualSaveLimit = 25;
// 0 means keep all
bool g_GDriveSetupComplete = false; // Tracks if initial GDrive setup prompt was shown
bool g_FirstGameAdded = false;
// Tracks if the first game has been added

// Struct to hold all information for a single game profile
struct GameProfile
{
	wstring name;
	wstring savePath;
	int autoSaveInterval; // Stored in seconds
	bool cloudSaveEnabled;
};

// --- Global State ---
vector<GameProfile> g_profiles;
GameProfile selectedGame;
// Currently selected game for monitoring/editing
atomic<bool> g_keepAutoSaving(false); // Flag to control the auto-save thread
thread g_autoSaveThread;
// --- Function Prototypes ---
void ClearScreen();
wstring GetExePath();
wstring GetExeFilename();
wstring s2ws(const string& str);
string ws2s(const wstring& wstr);
bool DirectoryExists(const char* path);
bool CheckExecutionDirectory(); // Checks if the program is in a dedicated folder
void CreateRequiredDirectories();
// Creates Config and Backups folders
string GetCurrentDateTime();
string GetFileModTime(const string& path);
void RegisterHotKeys();
void UnRegisterHotKeys();
void onSigBreakSignal(int s);
// Handles Ctrl+C / Console close
bool IsValidFilename(const wstring& name); // Checks for invalid characters in game names

// --- Config Functions ---
wstring GetConfigIniPath();
wstring GetProfilesIniPath();
void LoadGlobalConfig(); // Loads settings and setup flags from Config.ini
void SaveGlobalConfig();
// Saves settings and setup flags to Config.ini

// --- Profile Functions ---
void LoadProfiles();
// Loads all game profiles from GameProfiles.ini
void SaveProfile(const GameProfile& profile); // Saves a single profile to GameProfiles.ini
void DeleteProfileIniEntry(const wstring& profileName);
// Deletes only the INI section for a profile
void DeleteGame(GameProfile& profile); // Handles deleting profile and optionally backups
void CreateNewGame();
// Guides user through adding a new game
GameProfile* GetProfileByName(const wstring& name);
// Finds a profile in memory by name

// --- Menu Functions ---
void ShowHelpScreen();
void ShowSoftwareInfo();
// Reads version info from resources
void DisplayMainInterface(const GameProfile& profile); // Shows the monitoring screen
int SelectGameMenu();
// Main menu for selecting a game or action
void EditGameMenu(); // Menu for editing a selected game's details
void BackupAndStorageSettings();
// Menu for setting backup retention limits
void SetupCloudMenu(bool isFirstRun = false); // Menu for cloud path setup
void ShowSetupInstructions();
// Shows detailed Google Drive setup steps
void ShowOtherCloudInstructions(); // Shows steps for manually setting other cloud paths
void ShowRestoreMenu(); // Menu for selecting restore type (Local/Cloud)
void OpenSavePathFolder(const GameProfile& profile); // Opens game save path folder in Explorer

// --- Auto-Detect Functions ---
void DetectAndSetGoogleDrivePath();
// Tries to find Google Drive path automatically
wstring GetGoogleDrivePathFromConfig(); // Reads Google Drive's config file
bool ValidateGoogleDrivePath(const wstring& path);
// Checks if a path exists and is a directory

// --- Backup & Restore Functions ---
void BackupSaveFolder(const GameProfile& profile, bool autosave = false);
// Performs backup and purge
void PurgeBackups(const wstring& backupDir, const wstring& prefix, int autoLimit, int manualLimit, const wstring& locationName, std::vector<wstring>& logCollector);
// Deletes old backups
void RestoreLastBackup(const GameProfile& profile); // Restores latest MANUAL backup (Hotkey: Ctrl+R)
void RestoreFromCloud();
// Menu to select and restore a backup from the cloud folder
void RestoreFromLocal();
// Menu to select and restore a backup from the local folder
void OpenBackupFolder(const GameProfile& profile);
// Opens local backup folder in Explorer
void OpenCloudBackupFolder(const GameProfile& profile); // Opens cloud backup folder in Explorer
void CreateAutoSaveThread(const GameProfile& profile);
// Starts the background auto-save thread
void AutoSaveThreadFunction(GameProfile profile); // The function running in the auto-save thread (passed by value)

// --- Utility Functions ---

// Helper structure for VerQueryValue used in ShowSoftwareInfo
struct LANGANDCODEPAGE {
	WORD wLanguage;
	WORD wCodePage;
};

// Checks if a wide string ends with a specific suffix (case-sensitive)
bool endsWith(const std::wstring& str, const std::wstring& suffix) {
	if (str.length() < suffix.length()) {
		return false;
	}
	return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

// =========================================================================================
//                              MAIN FUNCTION
// =========================================================================================

int main()
{
	_setmode(_fileno(stdout), _O_U16TEXT);
	SetConsoleTitle(L"Game Save Backup Manager");

	// Perform strict directory check first. Exit if failed.
	if (!CheckExecutionDirectory())
	{
		system("pause");
		return 1;
	}

	// Create Config/Backups directories. Exit on critical error.
	try
	{
		CreateRequiredDirectories();
	}
	catch (const fs::filesystem_error& e)
	{
		ClearScreen();
		wcout << L"   ===================== CRITICAL ERROR =====================" << endl;
		wcout << L"    Could not create required 'Config' or 'Backups' folders." << endl;
		wcout << L"    Please ensure the program has write permissions in this" << endl;
		wcout << L"    directory and run it again." << endl << endl;
		wcout << L"    Error: " << s2ws(e.what()) << endl;
		wcout << L"   ==========================================================" << endl << endl;
		system("pause");
		return 1;
	}

	// Load settings and setup progress flags
	LoadGlobalConfig();
	LoadProfiles();

	// --- Handle first-run steps based on flags ---

	// Step 1: Initial Google Drive prompt (if not done before)
	if (!g_GDriveSetupComplete)
	{
		ClearScreen();
		wcout << L"   ============================================" << endl;
		wcout << L"       Welcome to Game Save Backup Manager!"
			<< endl;
		wcout << L"   ============================================" << endl << endl;
		wcout << L"This tool creates manual (CTRL+B) and auto-backups of your game saves." << endl << endl;
		wcout << L"--------------------------------------------------" << endl;
		wcout << L"   This program can sync your backups to Google Drive." << endl << endl;
		wcout << L"   * . Requires 'Google Drive for desktop' to be installed)" << endl;
		wcout << L"   * .Download from: https://www.google.com/drive/download/)" << endl;
		wcout << L"   * . Other cloud services can also be used. Instructions will be available on the next page.)" << endl << endl;
		wcout << L"Do you want to set up Google Drive / Cloud backup now? (y/n)" << endl;
		wcout << L"You can always do this later in 'Backup & Storage Settings'." << endl << L"> ";
		wstring choice;
		getline(wcin, choice);

		if (choice == L"y" || choice == L"Y")
		{
			SetupCloudMenu(true);
			// Run the setup menu in "first run" mode
		}
		// Mark GDrive setup prompt as complete regardless of choice
		g_GDriveSetupComplete = true;
		SaveGlobalConfig(); // Save the flag status
	}

	// Step 2: Prompt to add the first game (if not done before)
	if (!g_FirstGameAdded)
	{
		wcout << endl << L"Let's add your first game."
			<< endl;
		system("pause");
		CreateNewGame(); // Guide user through adding a game
		LoadProfiles();
		// Reload profiles immediately after adding
		// Mark first game added as complete *only if a game was actually added*
		if (!g_profiles.empty()) {
			g_FirstGameAdded = true;
			SaveGlobalConfig(); // Save the flag status
		}
	}


	// --- Main Program Loop ---
	while (true)
	{
		// If setup is complete but no games exist (e.g., user deleted all), prompt to add one.
		if (g_profiles.empty())
		{
			wcout << L"No games found. Please add one." << endl;
			CreateNewGame();
			LoadProfiles();
			if (g_profiles.empty()) // If they cancelled adding a game again, exit program
			{
				wcout << L"No games added. Exiting." << endl;
				break;
			}
			// Ensure flag is set if it somehow got missed (e.g., error during first add)
			if (!g_FirstGameAdded) {
				g_FirstGameAdded = true;
				SaveGlobalConfig();
			}
		}

		// Display the main menu and get user's choice
		int choice = SelectGameMenu();
		// Handle main menu actions
		if (choice == -1) // User chose to Exit
		{
			break;
		}
		else if (choice == -2) // User chose Backup & Storage Settings
		{
			BackupAndStorageSettings();
			continue;
			// Go back to the main menu
		}
		else if (choice == -3) // User chose Add New Game
		{
			CreateNewGame();
			LoadProfiles();
			// Reload after adding
			continue; // Go back to the main menu
		}
		else if (choice == -4) // User chose Help
		{
			ShowHelpScreen();
			continue;
			// Go back to the main menu
		}
		else if (choice == -5) // User chose Info
		{
			ShowSoftwareInfo();
			continue;
			// Go back to the main menu
		}

		// --- A specific game was selected ---
		selectedGame = g_profiles[choice];
		// Get the chosen game profile
		RegisterHotKeys(); // Activate global hotkeys for monitoring

		// Verify the game's save path exists before starting monitoring
		if (!fs::exists(selectedGame.savePath) || !fs::is_directory(selectedGame.savePath))
		{
			ClearScreen();
			wcout << L"   ===================== ERROR =====================" << endl;
			wcout << L"    Game Save Path NOT FOUND for " << selectedGame.name << L":" << endl;
			wcout << L"    " << selectedGame.savePath << endl << endl;
			wcout << L"    Please edit the game and fix the path." << endl;
			wcout << L"   ===============================================" << endl << endl;
			system("pause");
			UnRegisterHotKeys(); // Deactivate hotkeys before editing
			EditGameMenu();
			// Allow user to fix the path
			LoadProfiles(); // Reload in case the name/path changed
			continue;
			// Go back to the main game selection menu
		}

		// Ensure the specific backup directory for this game exists
		wstring backupPath = GetExePath() + L"\\Backups\\" + selectedGame.name;
		if (!DirectoryExists(ws2s(backupPath).c_str()))
			_wmkdir(backupPath.c_str()); // Create if missing

		// Start the background auto-save thread
		CreateAutoSaveThread(selectedGame);
		// Show the monitoring interface
		DisplayMainInterface(selectedGame);
		// Register signal handler for console close events
		signal(SIGBREAK, onSigBreakSignal);

		// --- Windows Message Loop (Handles Hotkeys while monitoring) ---
		MSG msg = { 0 };
		// This loop waits for messages (like hotkey presses) until interrupted
		while (GetMessage(&msg, NULL, 0, 0) != 0)
		{
			if (msg.message == WM_HOTKEY) // Process only hotkey messages
			{
				if (msg.wParam == 1) BackupSaveFolder(selectedGame, false);
				// CTRL+B (Manual Backup)
				if (msg.wParam == 2) OpenBackupFolder(selectedGame); // CTRL+O (Open Local Backups)
				if (msg.wParam == 3) RestoreLastBackup(selectedGame);
				// CTRL+R (Restore Last Manual)
				if (msg.wParam == 4) // CTRL+I (Show Help)
				{
					ShowHelpScreen();
					DisplayMainInterface(selectedGame);
					// Re-display monitoring screen after help
				}
				if (msg.wParam == 5) // CTRL+M (Back to Main Menu)
				{
					g_keepAutoSaving = false;
					// Signal auto-save thread to stop
					if (g_autoSaveThread.joinable())
						g_autoSaveThread.join(); // Wait for thread to finish
					UnRegisterHotKeys(); // Deactivate hotkeys
					PostMessage(NULL, WM_NULL, 0, 0);
					// Send a null message to break GetMessage loop
					break; // Exit the message loop
				}
				if (msg.wParam == 6) OpenCloudBackupFolder(selectedGame);
				// CTRL+G (Open Cloud Backups)
				if (msg.wParam == 7) // CTRL+L (List All Backups)
				{
					ShowRestoreMenu();
					DisplayMainInterface(selectedGame); // Re-display monitoring screen
				}
				if (msg.wParam == 8) // CTRL+P (Open Save Path)
				{
					OpenSavePathFolder(selectedGame);
					// No need to redraw screen, just opens Explorer
				}
			}
		}
		// After breaking the message loop (via Ctrl+M), the outer `while(true)` continues, showing the main menu again.
	}

	// Cleanup before exiting the program
	UnRegisterHotKeys(); // Ensure hotkeys are unregistered if exiting via 'X'
	return 0;
	// Normal exit
}


// =========================================================================================
//                              MENU & UI FUNCTIONS
// =========================================================================================

/**
 * @brief Clears the console screen.
 */
void ClearScreen()
{
	system("cls"); // Simple way to clear console on Windows
}

/**
 * @brief Displays the main monitoring interface while auto-save is active.
 * @param profile The game profile currently being monitored.
 */
void DisplayMainInterface(const GameProfile& profile)
{
	ClearScreen();
	wcout << L"   =============================================" << endl;
	wcout << L"       Game Save Backup Manager - Monitoring" << endl;
	wcout << L"   =============================================" << endl << endl;
	wcout << L"    GAME:       " << profile.name << endl;
	wcout << L"    SAVE PATH:  " << profile.savePath << endl;
	if (profile.cloudSaveEnabled)
	{
		wcout << L"    CLOUD SYNC: [ENABLED]" << endl << endl;
	}
	else
	{
		wcout << L"    CLOUD SYNC: [DISABLED]" << endl << endl;
	}

	wcout << L"   --- Hotkeys Active Now---" << endl;
	wcout << L"    CTRL + B:   Instant Manual Backup" << endl;
	wcout << L"    CTRL + R:   Restore Last MANUAL Backup Instantly (Quick Restore)" << endl;
	wcout << L"    CTRL + L:   List All Backups to Restore Specefic Save (Local/Cloud Restore)" << endl << endl;
	wcout << L"    CTRL + O:   Open Local Backup Folder" << endl;
	wcout << L"    CTRL + G:   Open Cloud Backup Folder" << endl;
	wcout << L"    CTRL + P:   Open Game Save Path Folder" << endl << endl;
	wcout << L"    CTRL + I:   Show Help" << endl;
	wcout << L"    CTRL + M:   Back to Main Menu" << endl << endl;
	wcout << L"   Monitoring for auto-save (" << (profile.autoSaveInterval / 60) << " min)..." << endl;
	// Display interval in minutes
	wcout << L"   ----------------------" << endl;
	// Backup messages will appear below this line
}

/**
 * @brief Displays the help screen with instructions and hotkey list.
 */
void ShowHelpScreen()
{
	ClearScreen();
	wcout << L"   ===========================================" << endl;
	wcout << L"             HELP AND INSTRUCTIONS" << endl;
	wcout << L"   ===========================================" << endl << endl;
	wcout << L"  WHAT DOES THIS DO?" << endl;
	wcout << L"    This tool runs in the background to automatically (and" << endl;
	wcout << L"    manually) back up your game save files. It can also" << endl;
	wcout << L"    sync these backups to your Google Drive or other cloud folders."
		<< endl << endl;

	wcout << L"  MONITORING HOTKEYS:" << endl;
	wcout << L"    CTRL + B:   Instantly creates a 'Manual' backup." << endl;
	wcout << L"                Use this after a big achievement!"
		<< endl << endl;
	wcout << L"    CTRL + R:   Instantly restores your most recent 'Manual'" << endl;
	wcout << L"                backup. (No confirmation!)" << endl << endl;
	wcout << L"    CTRL + L:   Opens a menu to list all backups and restore" << endl; // <-- UPDATED/NEW
	wcout << L"                from Local or Cloud." << endl << endl;              // <-- UPDATED/NEW
	wcout << L"    CTRL + O:   Opens the *local* Backups folder for" << endl;
	wcout << L"                the current game in Windows Explorer."
		<< endl << endl;
	wcout << L"    CTRL + G:   Opens the *cloud* Backups folder (if" << endl;
	wcout << L"                configured) in Windows Explorer."
		<< endl << endl;
	wcout << L"    CTRL + P:   Opens the *game's save path* folder in" << endl; // <-- NEW
	wcout << L"                Windows Explorer." << endl << endl;            // <-- NEW
	wcout << L"    CTRL + I:   Shows this Help screen again." << endl << endl;    // <-- Minor wording update
	wcout << L"    CTRL + M:   Stops monitoring and returns to the Home Menu."
		<< endl << endl;

	wcout << L"  CLOUD SYNC:" << endl;
	wcout << L"    Go to 'Backup & Storage Settings' from the Home Menu" << endl;
	wcout << L"    to set up cloud sync. The program auto-detects Google Drive." << endl;
	wcout << L"    Instructions for Google Drive and other services (Dropbox, etc.)" << endl;
	wcout << L"    are available in the Cloud Sync Setup menu." << endl << endl;
	wcout << L"  STORAGE LIMITS:" << endl;
	wcout << L"    In 'Backup & Storage Settings', you can set limits" << endl;
	wcout << L"    for how many Auto and Manual backups to keep, both" << endl;
	wcout << L"    locally and in the cloud. Set a limit to '0'" << endl;
	wcout << L"    to keep all backups of that type." << endl << endl;
	system("pause");
}

/**
 * @brief Displays the software information screen by reading from VERSIONINFO resource.
 */
void ShowSoftwareInfo()
{
	wchar_t exePath[MAX_PATH];
	GetModuleFileNameW(NULL, exePath, MAX_PATH);
	DWORD dwHandle = 0;
	DWORD dwVersionInfoSize = GetFileVersionInfoSizeW(exePath, &dwHandle);

	// Helper lambda for word-wrapping logic
	// We define it here so both the fallback and the main display can use it.
	auto PrintWrappedDescription = [](const wstring& label, const wstring& desc)
		{
			wcout << label;
			// Print the label (e.g., L"    Description:    ")

			// Indent logic to align wrapped lines under the description's start
			wstring indent = L"                   ";
			// Matches your label's spacing
			int maxWidth = 58; // Max width for content (80 total - 22 label - 0 safety)
			size_t startPos = 0;
			bool firstLine = true;

			if (desc.empty()) {
				wcout << endl; // Just print a newline if description is empty
				return;
			}

			while (startPos < desc.length()) {
				if (!firstLine) {
					wcout << indent; // Print indentation for subsequent lines
				}

				size_t remainingLength = desc.length() - startPos;
				if (remainingLength <= maxWidth) {
					// Last part of the string fits on one line
					wcout << desc.substr(startPos) << endl;
					break;
					// Exit loop
				}

				// Find a good place to break (last space before or at maxWidth)
				size_t breakPos = desc.rfind(L' ', startPos + maxWidth);
				if (breakPos == wstring::npos || breakPos <= startPos) {
					// No space found, or space is at the very beginning.
					// Hard break.
					breakPos = startPos + maxWidth;
				}

				wcout << desc.substr(startPos, breakPos - startPos) << endl;
				// Move startPos to the character after the break
				startPos = breakPos;
				// Also skip the space itself so the next line isn't indented
				if (startPos < desc.length() && desc[startPos] == L' ') {
					startPos++;
				}

				firstLine = false;
			}
		};
	// --- End of helper lambda ---


	if (dwVersionInfoSize == 0) {
		wcerr << L"Error: Could not get version info size."
			<< endl;
		// Fallback display if resource loading fails
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"             SOFTWARE INFORMATION" << endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"    Program Name:   Game Save Backup Manager" << endl;
		wcout << L"    Version:        (Error loading version info)" << endl;
		wcout << L"    Author:         Ezeiko" << endl;
		wcout << L"    Copyright:      Copyright (C) 2025 Ezeiko. All rights reserved." << endl;
		wcout << L"    Description:    A software to automatically or manually back up game save files locally and" << endl;
		wcout << L"                    sync them to Google Drive or other cloud services with hotkey support."
			<< endl;
		wcout << L"    License:        GNU General Public License v3.0" << endl << endl;

		system("pause");
		return;
	}

	std::vector<BYTE> versionInfoData(dwVersionInfoSize);
	if (!GetFileVersionInfoW(exePath, 0, dwVersionInfoSize, versionInfoData.data())) {
		wcerr << L"Error: Could not get version info data." << endl;
		// Fallback display
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"             SOFTWARE INFORMATION" << endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"    Program Name:   Game Save Backup Manager" << endl;
		wcout << L"    Version:        (Error loading version info)" << endl;
		wcout << L"    Author:         Ezeiko" << endl;
		wcout << L"    Copyright:      Copyright (C) 2025 Ezeiko. All rights reserved." << endl;
		wcout << L"    Description:    A software to automatically or manually back up game save files locally and" << endl;
		wcout << L"                    sync them to Google Drive or other cloud services with hotkey support."
			<< endl;
		wcout << L"    License:        GNU General Public License v3.0" << endl << endl;

		system("pause");
		return;
	}

	LANGANDCODEPAGE* lpTranslate = nullptr;
	UINT cbTranslate = 0;
	VerQueryValueW(versionInfoData.data(),
		L"\\VarFileInfo\\Translation",
		(LPVOID*)&lpTranslate,
		&cbTranslate);

	wstring subBlock;
	if (cbTranslate >= sizeof(LANGANDCODEPAGE) && lpTranslate) {
		wchar_t langCodeStr[9];
		swprintf_s(langCodeStr, 9, L"%04x%04x", lpTranslate[0].wLanguage, lpTranslate[0].wCodePage);
		subBlock = L"\\StringFileInfo\\" + wstring(langCodeStr) + L"\\";
	}
	else {
		subBlock = L"\\StringFileInfo\\040904b0\\";
	}

	auto GetValue = [&](const wstring& key) -> wstring {
		wstring query = subBlock + key;
		LPVOID lpBuffer = nullptr;
		UINT dwBytes = 0;
		if (VerQueryValueW(versionInfoData.data(), query.c_str(), &lpBuffer, &dwBytes) && dwBytes > 0) {
			return wstring((LPCWSTR)lpBuffer, dwBytes - 1);
		}
		return L"(Not Found)";
		};

	// --- Display the loaded information ---
	ClearScreen();
	wcout << L"   ===========================================" << endl;
	wcout << L"             SOFTWARE INFORMATION" << endl;
	wcout << L"   ===========================================" << endl << endl;
	wcout << L"    Program Name:   " << GetValue(L"ProductName") << endl;
	wcout << L"    Version:        " << GetValue(L"ProductVersion") << L" (File: " << GetValue(L"FileVersion") << L")" << endl;
	wcout << L"    Author:         " << GetValue(L"CompanyName") << endl;
	wcout << L"    Copyright:      " << GetValue(L"LegalCopyright") << endl;
	wcout << L"    Description:    A software to automatically or manually back up game save files locally and" << endl;
	wcout << L"                    sync them to Google Drive or other cloud services with hotkey support."
		<< endl;
	wcout << L"    License:        GNU General Public License v3.0" << endl;
	// --- ADDED ASCII ART AND THANK YOU ---
	// Use LR"()" for a wide raw string literal to handle special characters
	wcout << LR"(

        --- Thank You For Using This Software! ---

      ⣿⢟⣽⣿⣿⣿⣿⣫⡾⣵⣿⣿⣿⠃⠄⠄⠘⢿⣿⣾⣿⣿⣿⢿⣿
      ⢫⣿⣿⣿⣿⡿⣳⣿⣱⣿⣿⣿⡋⠄⠄⠄⠄⠄⠛⠛⠋⠁⠄⠄⣿
      ⣿⣿⣿⣿⡿⣹⡿⣃⣿⣿⣿⢳⠁⠄⠄⠄⢀⣀⠄⠄⠄⠄⠄⢀⣿
      ⡿⣿⣿⣿⢡⣫⣾⢸⢿⣿⡟⣿⣶⡶⢰⣿⣿⣿⢷⠄⠄⠄⠄⢼⣿
      ⣽⣿⣿⠃⣲⣿⣿⣸⣷⡻⡇⣿⣿⢇⣿⣿⣿⣏⣎⣸⣦⣠⡞⣾⢧
      ⣿⣿⡏⣼⣿⣿⡏⠙⣿⣿⣤⡿⣿⢸⣿⣿⢟⡞⣰⣿⣿⡟⣹⢯⣿
      ⣿⣿⣸⣿⣿⣿⣿⣦⡈⠻⣿⣿⣮⣿⣿⣯⣏⣼⣿⠿⠏⣰⡅⢸⣿
      ⣿⣇⣿⣿⡿⠛⠛⠛⠛⠄⣘⣿⣿⣿⣿⣿⣿⣶⣿⠿⠛⢾⡇⢸⣿
      ⣿⢻⣿⣿⣷⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⡋⠉⣠⣴⣾⣿⡇⣸⣿
      ⣿⢸⢻⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣄⠘⢿⣿⠏⠄⣿⣿
      ⣿⠸⣿⣿⣿⣿⣿⣿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣦⣼⠃⠄⢰⣿⣿
      ⣿⡄⠙⢿⣿⣿⡿⠁⠄⠄⠄⠄⠉⣿⣿⣿⣿⣿⣿⡏⠄⢀⣾⣿⢯
      ⣿⡇⠄⠄⠙⢿⣀⠄⠄⠄⠄⠄⣰⣿⣿⣿⣿⣿⠟⠄⠄⣼⡿⢫⣿

)" << endl;
	system("pause");
}

/**
 * @brief Displays the main "Home Menu" for selecting a game or action.
 * @return Index of selected game (0+), or negative codes for actions (-1=Exit, -2=Settings, etc.)
 */
int SelectGameMenu()
{
	while (true)
	{
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"                   HOME MENU" << endl;
		wcout << L"   ===========================================" << endl << endl;
		// Show warning if cloud path isn't configured
		if (g_GoogleDrivePath.empty())
		{
			wcout << L"   [WARNING: Cloud path not set. Cloud sync is disabled.]" << endl;
			wcout << L"   [Go to 'Backup & Storage Settings' to set it up.]" << endl << endl;
		}

		wcout << L"   Select a Game:" << endl;
		int i = 1;
		for (const auto& profile : g_profiles)
		{
			wcout << L"    " << i++ << L". " << profile.name << endl;
		}
		wcout << L"   -------------------------------------------" << endl << endl;
		wcout << L"    C. Add New Game" << endl;
		wcout << L"    S. Backup & Storage Settings" << endl;
		wcout << L"    H. Help and Instructions" << endl;
		wcout << L"    I. Software Information" << endl;
		wcout << L"    X. Exit" << endl << endl;
		wcout << L"   Choose an option (e.g., 1, C, S, H, I, X): ";

		string choice_str;
		getline(cin, choice_str);
		// Read user input as string

		// Handle non-numeric choices first
		if (choice_str == "x" || choice_str == "X") return -1;
		if (choice_str == "s" || choice_str == "S") return -2;
		if (choice_str == "c" || choice_str == "C") return -3;
		if (choice_str == "h" || choice_str == "H") return -4;
		if (choice_str == "i" || choice_str == "I") return -5;
		// Try to convert input to a number for game selection
		try
		{
			int choice = stoi(choice_str);
			// Validate if the number corresponds to a loaded game profile
			if (choice > 0 && choice <= g_profiles.size())
			{
				int profileIndex = choice - 1;
				// Convert 1-based index to 0-based

				// --- Game Sub-Menu ---
				while (true)
				{
					ClearScreen();
					// Ensure profileIndex is still valid (in case name was changed/deleted in Edit)
					if (profileIndex >= g_profiles.size()) break;
					// Exit sub-menu if index is out of bounds

					wcout << L"   Game: " << g_profiles[profileIndex].name << endl;
					wcout << L"   -------------------------------------------" << endl;
					wcout << L"    1. Start Monitoring" << endl;
					wcout << L"    2. Edit Game" << endl;
					wcout << L"    3. Restore from Local..." << endl;
					wcout << L"    4. Restore from Cloud..." << endl;
					wcout << L"    5. Delete Game" << endl;
					wcout << L"    6. Back to Home Menu" << endl << endl;
					wcout << L"   Choose an option: ";

					string sub_choice;
					getline(cin, sub_choice);

					if (sub_choice == "1") return profileIndex;
					// Return selected game index to main loop
					else if (sub_choice == "2") // Edit Game
					{
						selectedGame = g_profiles[profileIndex];
						// Store current game details
						EditGameMenu(); // Open edit menu
						LoadProfiles(); // Reload profiles immediately after edit
						// Re-find the profile by name in case it was renamed or deleted
						bool found = false;
						for (int k = 0; k < g_profiles.size(); ++k) {
							if (g_profiles[k].name == selectedGame.name) {
								profileIndex = k;
								// Update index if found
								found = true;
								break;
							}
						}
						if (!found) break;
						// If profile not found (e.g., deleted), exit sub-menu
						// Otherwise, loop back to re-display sub-menu with potentially updated index
					}
					else if (sub_choice == "3") // Restore Local
					{
						selectedGame = g_profiles[profileIndex];
						RestoreFromLocal();
						// Stay in sub-menu after restore attempt
					}
					else if (sub_choice == "4") // Restore Cloud
					{
						selectedGame = g_profiles[profileIndex];
						RestoreFromCloud();
						// Stay in sub-menu after restore attempt
					}
					else if (sub_choice == "5") // Delete Game
					{
						selectedGame = g_profiles[profileIndex];
						DeleteGame(selectedGame);
						// Perform delete actions
						LoadProfiles(); // Reload immediately
						break; // Exit sub-menu, go back to main menu
					}
					else if (sub_choice == "6") // Back
					{
						break;
						// Exit sub-menu, go back to main menu
					}
					// If invalid input, loop back to re-display sub-menu
				}
				// After breaking from sub-menu, loop back to re-display main menu
			}
			// If stoi succeeded but number was invalid, loop back to main menu prompt
		}
		catch (...) {
			// If stoi failed (non-numeric input not caught above), loop back to main menu prompt
		}
	} // End main menu loop
}

/**
 * @brief Displays the menu for editing the selected game's details.
 */
void EditGameMenu()
{
	while (true)
	{
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"              EDIT: " << selectedGame.name << endl;
		// Display current name being edited
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"    1. Edit Game Name" << endl;
		wcout << L"    2. Edit Game Save Path" << endl;
		wcout << L"    3. Edit Auto-Save Interval (minutes)" << endl;
		wcout << L"    4. Enable/Disable Cloud Backup" << endl;
		wcout << L"    5. Back to Game Menu" << endl << endl;
		// Go back to the previous menu (sub-menu)

		wcout << L"   Current Name: " << selectedGame.name << endl;
		wcout << L"   Current Path: " << selectedGame.savePath << endl;
		wcout << L"   Current Interval: " << (selectedGame.autoSaveInterval / 60) << " minutes" << endl;
		wcout << L"   Cloud Backup: " << (selectedGame.cloudSaveEnabled ? L"ENABLED" : L"DISABLED") << endl;
		wcout << L"   -------------------------------------------" << endl;
		wcout << L"   Choose an option: ";

		string choice_str;
		getline(cin, choice_str);

		if (choice_str == "1") // Edit Game Name
		{
			wcout << L"Enter new name (e.g., Elden Ring or leave blank to cancel the process): ";
			wstring oldName = selectedGame.name; // Store old name for INI deletion
			wstring newName;

			while (true) // Loop for name validation
			{
				getline(wcin, newName);
				if (newName.empty()) { // Handle blank input as cancel
					wcout << L"   Edit cancelled." << endl;
					system("pause");
					break;
					// Exit validation loop, return to edit menu
				}

				if (IsValidFilename(newName)) // Check for invalid characters
				{
					DeleteProfileIniEntry(oldName);
					// Delete the old entry from INI
					selectedGame.name = newName;    // Update the name in the current struct
					SaveProfile(selectedGame);
					// Save the profile with the new name
					wcout << L"Name saved." << endl;
					system("pause");
					break;
					// Exit validation loop, return to edit menu
				}
				else // Invalid name entered
				{
					wcout << L"Name contains invalid characters." << endl;
					wcout << L"Avoid: < > : \" / \\ | ? * " << endl;
					wcout << L"Enter new name (leave blank and press Enter to cancel): "; // Re-prompt
				}
			}
			// After break (save or cancel), re-display the edit menu
		}
		else if (choice_str == "2") // Edit Save Path
		{
			wcout << L"Enter new path (e.g., C:\\...\\SaveGames): ";
			wstring newPath;
			getline(wcin, newPath);
			if (!newPath.empty()) // Only update if something was entered
			{
				selectedGame.savePath = newPath;
				SaveProfile(selectedGame); // Save changes to INI
			}
		}
		else if (choice_str == "3") // Edit Interval
		{
			wcout << L"Enter new interval in MINUTES (e.g., 5, 10, 15): ";
			string interval_str;
			getline(cin, interval_str);
			try {
				int intervalMinutes = stoi(interval_str);
				if (intervalMinutes > 0) // Validate positive number
				{
					selectedGame.autoSaveInterval = intervalMinutes * 60; // Convert to seconds
					SaveProfile(selectedGame); // Save changes 
					// to INI
					wcout << L"Interval saved." << endl;
				}
				else
				{
					wcout << L"Interval must be a positive number." << endl;
				}
			}
			catch (...) { // Handle non-numeric input
				wcout << L"Invalid number." << endl;
			}
			system("pause");
		}
		else if (choice_str == "4") // Toggle Cloud Backup
		{
			// Prevent enabling if cloud path isn't set globally
			if (g_GoogleDrivePath.empty() && !selectedGame.cloudSaveEnabled) // Only show warning if trying to ENABLE without path
			{
				wcout << endl << L"   [WARNING] Global Cloud Sync path not set."
					<< endl;
				wcout << L"   Cloud backup cannot be enabled." << endl;
				wcout << L"   Please go to 'Backup & Storage Settings' to set it up." << endl;
				system("pause");
			}
			else // Allow toggling if path is set OR if disabling
			{
				selectedGame.cloudSaveEnabled = !selectedGame.cloudSaveEnabled; // Toggle the boolean
				SaveProfile(selectedGame);
				// Save changes to INI
				wcout << endl << L"   Cloud backup for " << selectedGame.name << L" is now "
					<< (selectedGame.cloudSaveEnabled ? L"ENABLED" : L"DISABLED") << L"."
					<< endl;
				system("pause");
			}
		}
		else if (choice_str == "5") // Back to Game Menu
		{
			return;
			// Exit the edit menu function
		}
		// If invalid input, loop back to re-display edit menu
	} // End edit menu loop
}

/**
 * @brief Displays the menu for setting global backup retention limits.
 */
void BackupAndStorageSettings()
{
	// Lambda helper function to handle getting and validating user input for limits
	auto SetLimitSetting = [](const wstring& title, const wstring& reasoning, int& settingToChange)
		{
			ClearScreen();
			wcout << L"   --- " << title << L" ---" << endl << endl;
			wcout << reasoning << endl;
			wcout << L"   Current Value: " << (settingToChange == 0 ? L"Keep All" : to_wstring(settingToChange)) << endl;
			wcout << L"   Enter new limit (0 to Keep All): "; // Clarified 0 meaning

			string input;
			getline(cin, input);
			try {
				int newLimit = stoi(input);
				if (newLimit < 0) // Ensure limit is not negative
				{
					wcout << L"Limit cannot be negative."
						<< endl;
				}
				else
				{
					settingToChange = newLimit; // Update the global setting variable
					wcout << L"Setting saved." << endl;
				}
			}
			catch (...) { // Handle non-numeric input
				wcout << L"Invalid number." << endl;
			}
			SaveGlobalConfig(); // Save all global settings after modification
			system("pause");
		};

	// Descriptive text for auto-save limit reasoning
	wstring autoSaveReasoning =
		L"   Auto-saves provide short-term protection against crashes.\n"
		L"   The program keeps the 'X' most recent auto-saves.\n\n"
		L"   RECOMMENDATIONS:\n"
		L"     - Limited Storage:  5 - 15\n"
		L"     - Ample Storage:   20 - 50\n";
	// Descriptive text for manual-save limit reasoning
	wstring manualSaveReasoning =
		L"   Manual saves (Ctrl+B) capture important milestones.\n"
		L"   Set limit to 0 to keep all, or 'X' to keep the most recent.\n\n"
		L"   RECOMMENDATIONS:\n"
		L"     - Limited Storage:  25 - 50 (Oldest are deleted)\n"
		L"     - Ample Storage:     0 (Keeps ALL - Recommended)\n";
	// Settings menu loop
	while (true)
	{
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"           BACKUP & STORAGE SETTINGS" << endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"   --- Local Storage Settings ---" << endl;
		wcout << L"    1. Set Local Auto-Save Limit   (Current: " << g_LocalAutoSaveLimit << L")" << endl;
		wcout << L"    2. Set Local Manual-Save Limit (Current: " << (g_LocalManualSaveLimit == 0 ? L"Keep All" : to_wstring(g_LocalManualSaveLimit)) << L")" << endl << endl;
		wcout << L"   --- Cloud Storage Settings ---" << endl;
		wcout << L"    3. Go to Cloud Sync Setup..." << endl;
		wcout << L"    4. Set Cloud Auto-Save Limit   (Current: " << g_CloudAutoSaveLimit << L")" << endl;
		wcout << L"    5. Set Cloud Manual-Save Limit (Current: " << (g_CloudManualSaveLimit == 0 ? L"Keep All" : to_wstring(g_CloudManualSaveLimit)) << L")" << endl << endl;
		wcout << L"   -------------------------------------------" << endl;
		wcout << L"    6. Back to Home Menu" << endl << endl;
		wcout << L"   Choose an option: ";

		string choice;
		getline(cin, choice);
		if (choice == "1") SetLimitSetting(L"Local Auto-Save Limit", autoSaveReasoning, g_LocalAutoSaveLimit);
		else if (choice == "2") SetLimitSetting(L"Local Manual-Save Limit", manualSaveReasoning, g_LocalManualSaveLimit);
		else if (choice == "3") SetupCloudMenu(false); // Open cloud setup (not in first run mode)
		else if (choice == "4") SetLimitSetting(L"Cloud Auto-Save Limit", autoSaveReasoning, g_CloudAutoSaveLimit);
		else if (choice == "5") SetLimitSetting(L"Cloud Manual-Save Limit", manualSaveReasoning, g_CloudManualSaveLimit);
		else if (choice == "6") return;
		// Exit settings menu
		// Invalid input loops back
	}
}

/**
 * @brief Displays the cloud setup menu with options for Google Drive and others.
 * @param isFirstRun True if this is being called during initial program setup.
 */
void SetupCloudMenu(bool isFirstRun)
{
	while (true)
	{
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"              CLOUD SYNC SETUP" << endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"   --- Using Other Cloud Services ---" << endl;
		wcout << L"   NOTE: This program primarily auto-detects Google Drive." << endl;
		wcout << L"         To use a different cloud sync folder (e.g., Dropbox, OneDrive):" << endl;
		wcout << L"         1. Select Option 1 below." << endl;
		wcout << L"         2. Press 'n' if the program suggests an incorrect path."
			<< endl;
		wcout << L"         3. Manually paste your desired path when prompted."
			<< endl << endl;
		wcout << L"   -------------------------------------------" << endl;
		wcout << L"   Current Cloud Path: " << (g_GoogleDrivePath.empty() ? L"[Not Set]" : g_GoogleDrivePath) << endl;
		wcout << L"   -------------------------------------------" << endl << endl;
		wcout << L"    1. Set / Change Cloud Sync Path" << endl;
		wcout << L"    2. View Google Drive Setup Instructions" << endl;
		wcout << L"    3. View Instructions for Other Cloud Services (Works for Google Drive too)" << endl;
		// Option 4 text changes based on context
		wcout << L"    4. " << (isFirstRun ? L"Save and Continue" : L"Back to Settings Menu") << endl;
		// Option 5 only shown during first run
		if (isFirstRun)
		{
			wcout << L"    5. Cancel Setup" << endl;
		}
		wcout << endl << L"   Choose an option: ";

		string choice;
		getline(cin, choice);

		if (choice == "1") DetectAndSetGoogleDrivePath();
		// Attempt auto-detect/manual path set
		else if (choice == "2") ShowSetupInstructions();
		// Show Google Drive specific help
		else if (choice == "3") ShowOtherCloudInstructions();
		// Show help for other services
		else if (choice == "4") // Save/Back action
		{
			// If it's the first run and no path was set, warn before continuing
			if (isFirstRun && g_GoogleDrivePath.empty())
			{
				wcout << endl << L"   [WARNING] You have not set a Cloud Sync path."
					<< endl;
				wcout << L"   Cloud sync will be disabled. Continue anyway? (y/n)" << endl << L"> ";
				string confirm;
				getline(cin, confirm);
				if (confirm == "y" || confirm == "Y")
				{
					return;
					// Exit setup menu, proceed with first run
				}
				// else stay in menu if user selects 'n'
			}
			else
			{
				return;
				// Exit setup menu (either first run with path set, or normal settings access)
			}
		}
		else if (isFirstRun && choice == "5") // Cancel Setup (only during first run)
		{
			wcout << endl << L"   Setup cancelled. Cloud sync will be disabled."
				<< endl;
			system("pause");
			return; // Exit setup menu, proceed with first run
		}
		// Invalid input loops back
	}
}

/**
 * @brief Displays the detailed setup instructions for Google Drive.
 */
void ShowSetupInstructions()
{
	ClearScreen();
	wcout << L"   --- How to Set Up Google Drive Sync ---" << endl << endl;
	wcout << L"   This program syncs backups using the official" << endl;
	wcout << L"   'Google Drive for desktop' application." << endl << endl;
	wcout << L"   Step 1: Install Google Drive for desktop" << endl;
	wcout << L"    - If you don't have it, download and install it from:" << endl;
	wcout << L"      https://www.google.com/drive/download/" << endl;
	wcout << L"      (Copy this link into your web browser)" << endl << endl;
	wcout << L"   Step 2: Sign In & Configure Mirroring" << endl;
	wcout << L"    - Open Google Drive and sign in with your Google account." << endl;
	wcout << L"    - !! IMPORTANT !! For reliability, set sync mode to 'Mirror files':" << endl;
	wcout << L"      Find this in: Google Drive > Settings (Gear Icon) > Preferences >" << endl;
	wcout << L"                      Google Drive > Mirror files."
		<< endl;
	wcout << L"      (This ensures backups are always available offline.)" << endl << endl;
	wcout << L"   Step 3: Set the Path in This Program" << endl;
	wcout << L"    - Go back to the Cloud Sync Setup menu and choose Option 1 ('Set Path')."
		<< endl;
	wcout << L"    - The program will try to automatically detect your 'My Drive' folder."
		<< endl;
	wcout << L"    - Confirm if it suggests the correct path." << endl << endl;
	wcout << L"   Step 4: (If Auto-Detect Fails) Manually Enter Path" << endl;
	wcout << L"    - If auto-detect doesn't find the correct path, press 'n' (No)." << endl;
	wcout << L"    - The program will then ask you to paste the path." << endl;
	wcout << L"    - Find your main Google Drive folder/drive in Windows Explorer." << endl;
	wcout << L"      Common locations:" << endl;
	wcout << L"        - A separate drive letter (e.g., G:\\My Drive)" << endl;
	wcout << L"        - Inside your user folder (e.g., C:\\Users\\YourName\\My Drive)" << endl;
	wcout << L"    - Copy the *full path* from Explorer's address bar and paste it" << endl;
	wcout << L"      into this program when prompted." << endl << endl;
	wcout << L"   Completion:" << endl;
	wcout << L"    - Once the path is set, the program will automatically create a" << endl;
	wcout << L"      'Game Save Backup Manager' folder inside your Google Drive path" << endl;
	wcout << L"      to store backups." << endl << endl;
	system("pause");
}

/**
 * @brief Displays instructions for setting up Dropbox, OneDrive, Mega, etc. manually.
 */
void ShowOtherCloudInstructions()
{
	ClearScreen();
	wcout << L"   --- Using Other Cloud Services (Dropbox, OneDrive, Mega, etc.) ---" << endl << endl;
	wcout << L"   This program works with any cloud service that creates a" << endl;
	wcout << L"   sync folder on your PC. You just need to manually set the path."
		<< endl << endl;

	wcout << L"   How to Find the Path:" << endl;
	wcout << L"    1. Install your cloud service's desktop app." << endl;
	wcout << L"       Examples: Dropbox, Microsoft OneDrive, MegaSync, pCloud Drive, etc." << endl;
	wcout << L"       (Note: iCloud for Windows may sync differently; test carefully.)" << endl << endl;
	wcout << L"    2. Find the main sync folder it creates on your computer." << endl;
	wcout << L"       Common Default Locations:" << endl;
	wcout << L"         - Dropbox:  C:\\Users\\YourName\\Dropbox" << endl;
	wcout << L"         - OneDrive: C:\\Users\\YourName\\OneDrive" << endl;
	wcout << L"         - MegaSync: C:\\Users\\YourName\\Documents\\MEGAsync Uploads" << endl;
	wcout << L"       (Your specific path might be different!)" << endl << endl;
	wcout << L"    3. In our program's Cloud Setup menu, choose Option 1 ('Set Path')." << endl;
	wcout << L"    4. If it suggests a Google Drive path, press 'n' (No)." << endl;
	wcout << L"    5. When it asks you to paste the path manually, paste the *full path*" << endl;
	wcout << L"       to your cloud service's main sync folder." << endl << endl;
	wcout << L"   The program will then create its 'Game Save Backup Manager' folder" << endl;
	wcout << L"   inside the path you provided and sync backups there." << endl << endl;
	system("pause");
}

/**
 * @brief Displays the restore menu (Local/Cloud) when CTRL+L is pressed.
 */
void ShowRestoreMenu()
{
	while (true)
	{
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"                 RESTORE FROM..." << endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"    1. Restore from Local Backups" << endl;
		wcout << L"    2. Restore from Cloud Backups" << endl;
		if (!selectedGame.cloudSaveEnabled)
		{
			wcout << L"       [Cloud Sync is DISABLED for this game. Option unavailable.]" << endl;
		}
		wcout << endl << L"    X. Cancel (Back to Monitoring)" << endl << endl;
		wcout << L"   Choose an option: ";

		string choice;
		getline(cin, choice);

		if (choice == "1")
		{
			RestoreFromLocal(); // Call existing function
		}
		else if (choice == "2")
		{
			if (!selectedGame.cloudSaveEnabled)
			{
				wcout << endl << L"   Action cancelled: Cloud Sync is disabled for this game." << endl;
				system("pause");
			}
			else
			{
				RestoreFromCloud(); // Call existing function
			}
		}
		else if (choice == "x" || choice == "X")
		{
			return; // Exit this menu, return to monitoring
		}
		// Any other input will just loop and re-display the menu
	}
}

// =========================================================================================
//                       PROFILE & CONFIG (INI) FUNCTIONS
// =========================================================================================

/**
 * @brief Gets the full path to the Config.ini file (in Config subfolder).
 */
wstring GetConfigIniPath()
{
	return GetExePath() + L"\\Config\\Config.ini";
}

/**
 * @brief Gets the full path to the GameProfiles.ini file (in Config subfolder).
 */
wstring GetProfilesIniPath()
{
	return GetExePath() + L"\\Config\\GameProfiles.ini";
}

/**
 * @brief Loads global settings (limits, cloud path) and setup flags from Config.ini.
 * Creates Config.ini with default flags if it doesn't exist.
 */
void LoadGlobalConfig()
{
	wstring configFile = GetConfigIniPath();
	// Create Config.ini with default setup flags if it doesn't exist
	if (!fs::exists(configFile)) {
		ofstream create_file(configFile);
		if (create_file.is_open()) {
			create_file << "[Setup]" << endl;
			create_file << "GDriveSetupComplete=0" << endl;
			// Default to 0 (not complete)
			create_file << "FirstGameAdded=0" << endl;    // Default to 0 (not complete)
			create_file.close();
		}
		else {
			// This error should ideally be caught by CheckExecutionDirectory/CreateRequiredDirectories
			wcerr << L"Critical Error: Could not create Config.ini" << endl;
			throw runtime_error("Failed to create Config.ini"); // Throw to stop execution
		}
	}

	// Load settings from [GlobalSettings] section
	wchar_t buffer[MAX_PATH];
	GetPrivateProfileStringW(L"GlobalSettings", L"GoogleDrivePath", L"", buffer, MAX_PATH, configFile.c_str());
	g_GoogleDrivePath = buffer; // Load cloud path (empty if not set)

	g_LocalAutoSaveLimit = GetPrivateProfileIntW(L"GlobalSettings", L"LocalAutoSaveLimit", 20, configFile.c_str());
	// Default 20
	g_LocalManualSaveLimit = GetPrivateProfileIntW(L"GlobalSettings", L"LocalManualSaveLimit", 0, configFile.c_str()); // Default 0 (keep all)
	g_CloudAutoSaveLimit = GetPrivateProfileIntW(L"GlobalSettings", L"CloudAutoSaveLimit", 10, configFile.c_str());
	// Default 10
	g_CloudManualSaveLimit = GetPrivateProfileIntW(L"GlobalSettings", L"CloudManualSaveLimit", 25, configFile.c_str()); // Default 25

	// Load setup progress flags from [Setup] section
	g_GDriveSetupComplete = GetPrivateProfileIntW(L"Setup", L"GDriveSetupComplete", 0, configFile.c_str()) == 1;
	g_FirstGameAdded = GetPrivateProfileIntW(L"Setup", L"FirstGameAdded", 0, configFile.c_str()) == 1;
}

/**
 * @brief Saves global settings (limits, cloud path) and setup flags to Config.ini.
 */
void SaveGlobalConfig()
{
	wstring configFile = GetConfigIniPath();
	// Save settings to [GlobalSettings] section
	WritePrivateProfileStringW(L"GlobalSettings", L"GoogleDrivePath", g_GoogleDrivePath.c_str(), configFile.c_str());
	WritePrivateProfileStringW(L"GlobalSettings", L"LocalAutoSaveLimit", to_wstring(g_LocalAutoSaveLimit).c_str(), configFile.c_str());
	WritePrivateProfileStringW(L"GlobalSettings", L"LocalManualSaveLimit", to_wstring(g_LocalManualSaveLimit).c_str(), configFile.c_str());
	WritePrivateProfileStringW(L"GlobalSettings", L"CloudAutoSaveLimit", to_wstring(g_CloudAutoSaveLimit).c_str(), configFile.c_str());
	WritePrivateProfileStringW(L"GlobalSettings", L"CloudManualSaveLimit", to_wstring(g_CloudManualSaveLimit).c_str(), configFile.c_str());
	// Save setup progress flags to [Setup] section
	WritePrivateProfileStringW(L"Setup", L"GDriveSetupComplete", (g_GDriveSetupComplete ? L"1" : L"0"), configFile.c_str());
	WritePrivateProfileStringW(L"Setup", L"FirstGameAdded", (g_FirstGameAdded ? L"1" : L"0"), configFile.c_str());
}

/**
 * @brief Loads all game profiles from GameProfiles.ini into the global g_profiles vector.
 * Creates an empty GameProfiles.ini if it doesn't exist.
 */
void LoadProfiles()
{
	g_profiles.clear(); // Clear existing profiles before loading
	wstring profilesFile = GetProfilesIniPath();
	// Create GameProfiles.ini if it doesn't exist
	if (!fs::exists(profilesFile))
	{
		ofstream create_file(profilesFile);
		create_file.close(); // Create empty file
		return;
		// No profiles to load
	}

	// Read all section names (game names) from the INI file
	wchar_t sectionNames[8192];
	// Buffer to hold all section names
	DWORD bytesRead = GetPrivateProfileSectionNamesW(sectionNames, 8192, profilesFile.c_str());
	// If file exists but is empty or has no sections, return
	if (bytesRead == 0 || (bytesRead == 2 && sectionNames[0] == L'\0')) {
		return;
	}

	// Iterate through the null-terminated strings in sectionNames buffer
	wchar_t* pSection = sectionNames;
	while (*pSection)
	{
		wstring sectionName = pSection;
		// Current game name
		GameProfile profile;
		wchar_t buffer[MAX_PATH]; // Reusable buffer for reading strings

		// Read profile details using the section name
		GetPrivateProfileStringW(sectionName.c_str(), L"Name", L"", buffer, MAX_PATH, profilesFile.c_str());
		profile.name = buffer;

		GetPrivateProfileStringW(sectionName.c_str(), L"SavePath", L"", buffer, MAX_PATH, profilesFile.c_str());
		profile.savePath = buffer;

		profile.autoSaveInterval = GetPrivateProfileIntW(sectionName.c_str(), L"AutoSaveInterval", 600, profilesFile.c_str());
		// Default 10 min (600s)
		profile.cloudSaveEnabled = GetPrivateProfileIntW(sectionName.c_str(), L"CloudSaveEnabled", 0, profilesFile.c_str()) == 1;
		// Default 0 (false)

		// Add profile to vector only if Name and SavePath were successfully read
		if (!profile.name.empty() && !profile.savePath.empty())
		{
			g_profiles.push_back(profile);
		}

		// Move to the next section name (strings are double-null terminated)
		pSection += wcslen(pSection) + 1;
	}
}

/**
 * @brief Saves a single game profile's details to GameProfiles.ini under a section matching its name.
 * @param profile The GameProfile struct containing the data to save.
 */
void SaveProfile(const GameProfile& profile)
{
	wstring profilesFile = GetProfilesIniPath();
	// Write each setting under the [profile.name] section
	WritePrivateProfileStringW(profile.name.c_str(), L"Name", profile.name.c_str(), profilesFile.c_str());
	WritePrivateProfileStringW(profile.name.c_str(), L"SavePath", profile.savePath.c_str(), profilesFile.c_str());
	WritePrivateProfileStringW(profile.name.c_str(), L"AutoSaveInterval", to_wstring(profile.autoSaveInterval).c_str(), profilesFile.c_str());
	// Save interval in seconds
	WritePrivateProfileStringW(profile.name.c_str(), L"CloudSaveEnabled", (profile.cloudSaveEnabled ? L"1" : L"0"), profilesFile.c_str());
	// Save boolean as 1 or 0
}

/**
 * @brief Deletes a specific game's section from GameProfiles.ini.
 * @param profileName The name of the game profile section to delete.
 */
void DeleteProfileIniEntry(const wstring& profileName)
{
	// Passing NULL as key and value deletes the entire section
	WritePrivateProfileStringW(profileName.c_str(), NULL, NULL, GetProfilesIniPath().c_str());
}

/**
 * @brief Handles the complete process of deleting a game:
 * 1. Confirms profile deletion.
 * 2. Deletes the profile from GameProfiles.ini.
 * 3. Confirms and optionally deletes local backups.
 * 4. Confirms and optionally deletes cloud backups.
 * @param profile The GameProfile struct of the game to delete.
 */
void DeleteGame(GameProfile& profile)
{
	ClearScreen();
	// Confirm deleting the profile itself
	wcout << L"   ARE YOU SURE you want to delete the game profile: " << profile.name << L"? (y/n)" << endl;
	wcout << L"   This will remove it from the program's list." << endl << L"> ";
	string confirm;
	getline(cin, confirm);
	if (confirm != "y" && confirm != "Y")
	{
		wcout << L"Delete cancelled." << endl;
		system("pause");
		return;
		// Abort deletion
	}

	// Delete the entry from the INI file
	DeleteProfileIniEntry(profile.name);
	wcout << L"Game profile deleted." << endl;
	// Check for and optionally delete local backups
	wstring localBackupPath = GetExePath() + L"\\Backups\\" + profile.name;
	if (fs::exists(localBackupPath))
	{
		wcout << endl << L"   Do you also want to delete all *local* backups for this game?"
			<< endl;
		wcout << L"   (WARNING: This is permanent and cannot be undone!) (y/n)" << endl << L"> ";
		getline(cin, confirm);
		if (confirm == "y" || confirm == "Y")
		{
			try
			{
				fs::remove_all(localBackupPath); // Delete the entire folder recursively
				wcout << L"Local backups deleted."
					<< endl;
			}
			catch (const fs::filesystem_error& e) // Handle potential deletion errors
			{
				wcout << L"Error deleting local backups: " << s2ws(e.what()) << endl;
			}
		}
	}

	// Check for and optionally delete cloud backups (if path is set)
	wstring cloudBackupPath = g_GoogleDrivePath + L"\\Game Save Backup Manager\\" + profile.name;
	if (!g_GoogleDrivePath.empty() && fs::exists(cloudBackupPath))
	{
		wcout << endl << L"   Do you also want to delete all *cloud* backups for this game?"
			<< endl;
		wcout << L"   (WARNING: This is also permanent!) (y/n)" << endl << L"> ";
		getline(cin, confirm);
		if (confirm == "y" || confirm == "Y")
		{
			try
			{
				fs::remove_all(cloudBackupPath); // Delete the entire folder recursively
				wcout << L"Cloud backups deleted." << endl;
			}
			catch (const fs::filesystem_error& e) // Handle potential deletion errors
			{
				wcout << L"Error deleting cloud backups: " << s2ws(e.what()) << endl;
			}
		}
	}

	system("pause");
	// Pause after deletion process is complete
}

/**
 * @brief Guides the user through adding a new game profile, including validation.
 * Allows cancellation by entering a blank name or path.
 */
void CreateNewGame()
{
	ClearScreen();
	GameProfile newGame;
	wcout << L"   ===========================================" << endl;
	wcout << L"                 ADD NEW GAME" << endl;
	wcout << L"   ===========================================" << endl << endl;
	// Loop for getting and validating the game name
	while (true)
	{
		wcout << L"Enter a name for this game (e.g., Elden Ring or leave blank to cancel the process):" << endl << L"> ";
		getline(wcin, newGame.name);
		if (newGame.name.empty()) { // Check for empty input to cancel
			wcout << L"Cancelled adding game." << endl;
			system("pause");
			return;
			// Exit the function if cancelled
		}
		if (IsValidFilename(newGame.name)) // Check for invalid characters
		{
			break;
			// Valid name entered, exit loop
		}
		else
		{
			wcout << L"Name contains invalid characters." << endl;
			wcout << L"Avoid: < > : \" / \\ | ? * and control characters." << endl << endl;
		}
	}

	// Get the save path
	wcout << endl << L"Enter the FULL path to the game's save folder (cannot be blank):" << endl;
	wcout << L"e.g., C:\\Users\\YourName\\AppData\\Roaming\\EldenRing" << endl << L"> ";
	getline(wcin, newGame.savePath);
	if (newGame.savePath.empty()) { // Check for empty path to cancel
		wcout << L"Save path cannot be empty. Cancelling add game." << endl;
		system("pause");
		return; // Exit the function if cancelled
	}

	// Get the auto-save interval in minutes
	int intervalMinutes = 0;
	while (true)
	{
		wcout << endl << L"Enter auto-save interval in MINUTES (e.g., 10, minimum 1):" << endl << L"> ";
		string interval_str;
		getline(cin, interval_str);
		try {
			intervalMinutes = stoi(interval_str);
			if (intervalMinutes > 0) // Validate positive interval
			{
				newGame.autoSaveInterval = intervalMinutes * 60; // Convert to seconds for internal use
				break; // Valid interval entered, exit loop
			}
			else
			{
				wcout << L"Interval must be a positive number (e.g., 5, 10)." << endl;
			}
		}
		catch (...) { // Handle non-numeric input
			wcout << L"Invalid input. Please enter a number (e.g., 10)." << endl;
		}
	}

	// Ask about enabling cloud backup (only if global path is set)
	if (g_GoogleDrivePath.empty())
	{
		newGame.cloudSaveEnabled = false; // Cannot enable if path isn't set
		wcout << endl << L"[Cloud sync path not set globally. Cloud backup disabled for this game.]" << endl;
		wcout << L"[Set the path in 'Backup & Storage Settings' to enable cloud features.]" << endl;
	}
	else
	{
		wcout << endl << L"Enable cloud backup for this game? (y/n)" << endl << L"> ";
		string choice;
		getline(cin, choice);
		newGame.cloudSaveEnabled = (choice == "y" || choice == "Y");
	}

	// Save the newly created profile
	SaveProfile(newGame);
	wcout << endl << L"Game saved!" << endl;
	system("pause");
}

/**
 * @brief Finds a profile in the global g_profiles vector by its exact name.
 * @param name The name of the game profile to find.
 * @return A pointer to the found GameProfile, or nullptr if not found.
 */
GameProfile* GetProfileByName(const wstring& name)
{
	for (auto& profile : g_profiles) // Iterate through the vector
	{
		if (profile.name == name) // Case-sensitive comparison
			return &profile; // Return pointer to the matching profile
	}
	return nullptr; // Return null if no match 
	// found
}


// =========================================================================================
//                       BACKUP, RESTORE & PURGE FUNCTIONS
// =========================================================================================

/**
 * @brief Creates a backup, logs the action, syncs if enabled, purges old backups,
 * and logs results with purges grouped after the summary.
 * @param profile The game profile to back up.
 * @param autosave True if this is an automatic backup, False if manual (Ctrl+B).
 */
void BackupSaveFolder(const GameProfile& profile, bool autosave)
{
	wstring prefix = (autosave ? L"A" : L"M");
	auto now_time_point = chrono::system_clock::now();
	long long epochTime = chrono::system_clock::to_time_t(now_time_point);
	string currentTime = GetCurrentDateTime(); // Consistent timestamp for this operation

	// Convert time_point to tm struct for formatting folder name
	time_t now_t = chrono::system_clock::to_time_t(now_time_point);
	tm ltm;
	localtime_s(&ltm, &now_t);
	wchar_t timeBuffer[100];
	wcsftime(timeBuffer, 100, L"%Y-%m-%d_%H-%M-%S", &ltm);
	wstring safeDateTime = timeBuffer;

	// Construct paths
	wstring backupPathBase = GetExePath() + L"\\Backups\\" + profile.name;
	wstring backupFolderName = to_wstring(epochTime) + L"-[" + safeDateTime + L"]-" + prefix;
	wstring targetBackupPath = backupPathBase + L"\\" + backupFolderName;

	bool localSuccess = false;
	bool cloudSuccess = false;
	bool cloudAttempted = false; // Track if cloud was enabled/attempted

	std::vector<wstring> purgeMessages; // Vector to store purge log messages

	// --- 1. Perform Local Backup ---
	try {
		fs::copy(profile.savePath, targetBackupPath, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
		localSuccess = true;
		// Don't log success yet
	}
	catch (const fs::filesystem_error& e) {
		// Log failure immediately and exit function
		wcout << L"[" << s2ws(currentTime) << L"] [" << prefix << L"] Local backup FAILED for " << backupFolderName << L": " << s2ws(e.what()) << endl;
		try { fs::remove_all(targetBackupPath); } // Attempt cleanup
		catch (...) {}
		wcout << L"--------------------------------------------------" << endl; // Separator after failure
		return;
	}

	// --- 2. Purge Old Local Backups (Collect Messages) ---
	// This runs only if local backup succeeded
	PurgeBackups(backupPathBase, prefix, g_LocalAutoSaveLimit, g_LocalManualSaveLimit, L"Local", purgeMessages);

	// --- 3. Perform Cloud Backup (if enabled and path is set) ---
	if (profile.cloudSaveEnabled && !g_GoogleDrivePath.empty())
	{
		cloudAttempted = true;
		wstring cloudGamePath = g_GoogleDrivePath + L"\\Game Save Backup Manager\\" + profile.name;
		wstring cloudTargetPath = cloudGamePath + L"\\" + backupFolderName;

		try {
			fs::create_directories(cloudGamePath);
			fs::copy(targetBackupPath, cloudTargetPath, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
			cloudSuccess = true;
			// Don't log success yet

			// --- 4. Purge Old Cloud Backups (Collect Messages) ---
			PurgeBackups(cloudGamePath, prefix, g_CloudAutoSaveLimit, g_CloudManualSaveLimit, L"Cloud", purgeMessages);
		}
		catch (const fs::filesystem_error& e) {
			// Log cloud sync failure immediately (as it's part of the operation's status)
			wcout << L"[" << s2ws(currentTime) << L"] [CLOUD] Sync FAILED for " << backupFolderName << L": " << s2ws(e.what()) << endl;
			cloudSuccess = false; // Ensure this is false
		}
	}

	// --- 5. FINAL Consolidated Logging ---
	// Print the final summary status line FIRST
	if (localSuccess && cloudAttempted && cloudSuccess) {
		wcout << L"[" << s2ws(currentTime) << L"] [" << prefix << L"] Backup " << backupFolderName << L" completed (Local + Cloud Sync)." << endl;
	}
	else if (localSuccess && cloudAttempted && !cloudSuccess) {
		// Cloud failure message was already printed in the catch block
		wcout << L"[" << s2ws(currentTime) << L"] [" << prefix << L"] Backup " << backupFolderName << L" completed (Local only due to Cloud sync failure)." << endl;
	}
	else if (localSuccess) { // Covers Local only (cloud disabled or path not set)
		wcout << L"[" << s2ws(currentTime) << L"] [" << prefix << L"] Backup " << backupFolderName << L" completed (Local)." << endl;
	}
	// else: Local failed case handled earlier with immediate return.

	// Now print all collected purge messages AFTER the summary
	for (const auto& msg : purgeMessages) {
		wcout << msg << endl;
	}

	// --- ADD SEPARATOR LINE ---
	// Add separator only if the operation didn't completely fail locally (localSuccess should be true here)
	wcout << L"--------------------------------------------------" << endl;

} // End of BackupSaveFolder function

/**
 * @brief Deletes old backups (auto or manual) if they exceed limits, collecting log messages.
 * @param backupDir Directory containing backups.
 * @param prefix Type of backup just created ("A" or "M") - used internally for consistency check.
 * @param autoLimit Max auto-saves to keep (0=all).
 * @param manualLimit Max manual-saves to keep (0=all).
 * @param locationName "Local" or "Cloud".
 * @param logCollector Vector to store generated log messages.
 */
void PurgeBackups(const wstring& backupDir, const wstring& prefix, int autoLimit, int manualLimit, const wstring& locationName, std::vector<wstring>& logCollector)
{
	if (!fs::exists(backupDir)) return; // Don't proceed if the directory doesn't exist

	vector<fs::path> autoSaves;
	vector<fs::path> manualSaves;
	// Iterate through the backup directory and categorize folders by suffix
	for (const auto& entry : fs::directory_iterator(backupDir))
	{
		if (entry.is_directory()) // Only consider directories
		{
			wstring dirName = entry.path().filename().wstring();
			if (endsWith(dirName, L"-A")) autoSaves.push_back(entry.path());
			else if (endsWith(dirName, L"-M")) manualSaves.push_back(entry.path());
		}
	}

	// Sort backups chronologically (oldest first)
	sort(autoSaves.begin(), autoSaves.end());
	sort(manualSaves.begin(), manualSaves.end());

	// String stream to build messages before adding to vector
	wstringstream wss;

	// Purge oldest auto-saves if limit is exceeded
	if (autoLimit > 0 && autoSaves.size() > autoLimit)
	{
		int toDelete = static_cast<int>(autoSaves.size()) - autoLimit;
		wss.str(L""); // Clear stream
		wss << L"      [PURGE:" << locationName << L"] Auto-save limit (" << autoLimit << L") exceeded. Deleting " << toDelete << L" oldest...";
		logCollector.push_back(wss.str()); // Add summary message to vector

		for (int i = 0; i < toDelete; ++i)
		{
			try {
				wss.str(L""); // Clear stream
				wss << L"         - Deleting: " << autoSaves[i].filename().wstring();
				logCollector.push_back(wss.str()); // Add deletion detail message to vector
				fs::remove_all(autoSaves[i]); // Delete the folder recursively
			}
			catch (const fs::filesystem_error& e) {
				// Print failure immediately for visibility
				wcout << L"      [PURGE:" << locationName << L"] FAILED to delete Auto " << autoSaves[i].filename().wstring() << L": " << s2ws(e.what()) << endl;
			}
		}
	}

	// Purge oldest manual-saves if limit is exceeded
	if (manualLimit > 0 && manualSaves.size() > manualLimit)
	{
		int toDelete = static_cast<int>(manualSaves.size()) - manualLimit;
		wss.str(L""); // Clear stream
		wss << L"      [PURGE:" << locationName << L"] Manual-save limit (" << manualLimit << L") exceeded. Deleting " << toDelete << L" oldest...";
		logCollector.push_back(wss.str()); // Add summary message to vector

		for (int i = 0; i < toDelete; ++i)
		{
			try {
				wss.str(L""); // Clear stream
				wss << L"         - Deleting: " << manualSaves[i].filename().wstring();
				logCollector.push_back(wss.str()); // Add deletion detail message to vector
				fs::remove_all(manualSaves[i]); // Delete the folder recursively
			}
			catch (const fs::filesystem_error& e) {
				// Print failure immediately for visibility
				wcout << L"      [PURGE:" << locationName << L"] FAILED to delete Manual " << manualSaves[i].filename().wstring() << L": " << s2ws(e.what()) << endl;
			}
		}
	}
}

/**
 * @brief Instantly restores the most recent MANUAL backup found in the local backups folder,
 * overwriting the current game save files without confirmation.
 (Triggered by Ctrl+R).
 * @param profile The game profile for which to restore the backup.
 */
void RestoreLastBackup(const GameProfile& profile)
{
	wstring backupPathBase = GetExePath() + L"\\Backups\\" + profile.name;
	if (!fs::exists(backupPathBase))
	{
		wcout << L"No local backups found for this game." << endl;
		wcout << L"--------------------------------------------------" << endl;
		return;
	}

	fs::path latestManualBackup;
	fs::file_time_type latestTime = fs::file_time_type::min();
	// Initialize to earliest possible time

	// Find the most recent directory ending in "-M"
	for (const auto& entry : fs::directory_iterator(backupPathBase))
	{
		if (entry.is_directory() && endsWith(entry.path().filename().wstring(), L"-M"))
		{
			try {
				auto modTime = fs::last_write_time(entry);
				// Check if this is the first manual save found or if it's newer than the current latest
				if (latestManualBackup.empty() || modTime > latestTime)
				{
					latestTime = modTime;
					latestManualBackup = entry.path();
				}
			}
			catch (const fs::filesystem_error&) {
				// Ignore errors reading time for a specific backup folder, skip it
			}
		}
	}

	// If no manual backup was found
	if (latestManualBackup.empty())
	{
		wcout << L"No MANUAL (-M) backups found. CTRL+R only restores the latest manual save." << endl;
		wcout << L"--------------------------------------------------" << endl;
		return;
	}

	// Proceed with restore (no confirmation)
	try {
		// Ensure the target save path exists and is a directory
		if (!fs::exists(profile.savePath)) {
			fs::create_directories(profile.savePath);
			// Create if missing
		}
		else if (!fs::is_directory(profile.savePath)) {
			wcout << L"RESTORE FAILED: Target save path exists but is not a directory: " << profile.savePath << endl;
			wcout << L"--------------------------------------------------" << endl;
			return; // Cannot restore if target isn't a directory
		}

		// Clear the existing save directory contents
		for (const auto& entry : fs::directory_iterator(profile.savePath))
		{
			fs::remove_all(entry.path());
		}

		// Copy the contents of the chosen backup folder to the save directory
		fs::copy(latestManualBackup, profile.savePath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
		wcout << L"Restored from latest manual backup: " << latestManualBackup.filename().wstring() << endl;
		wcout << L"--------------------------------------------------" << endl;
	}
	catch (const fs::filesystem_error& e) { // Handle potential deletion/copy errors
		wcout << L"RESTORE FAILED: " << s2ws(e.what()) << endl;
		wcout << L"Another program might be using the save files, or permissions may be insufficient." << endl;
		wcout << L"--------------------------------------------------" << endl;
	}
}

/**
 * @brief Displays a menu listing local backups for the selected game and allows the user
 * to choose one to restore, overwriting the current save files after confirmation.
 */
void RestoreFromLocal()
{
	ClearScreen();
	wstring localGamePath = GetExePath() + L"\\Backups\\" + selectedGame.name;
	// Path to local backups for the current game
	// Check if the backup directory exists
	if (!fs::exists(localGamePath) || !fs::is_directory(localGamePath))
	{
		wcout << L"Local restore failed: No local backups found for " << selectedGame.name << L" at:" << endl;
		wcout << localGamePath << endl;
		system("pause");
		return;
	}

	vector<fs::path> backups; // Vector to hold paths of valid backup folders
	// Populate the vector with backup directories
	for (const auto& entry : fs::directory_iterator(localGamePath))
	{
		if (entry.is_directory()) // Only consider directories
		{
			backups.push_back(entry.path());
		}
	}
	// Sort backups by path name (timestamp) in descending order (newest first)
	sort(backups.rbegin(), backups.rend());
	if (backups.empty()) // Check if any backup folders were found
	{
		wcout << L"No backup folders found locally for this game."
			<< endl;
		system("pause");
		return;
	}

	// Display the list of backups
	wcout << L"   --- Local Backups for " << selectedGame.name << L" ---" << endl;
	wcout << L"   (Newest first)" << endl << endl;
	for (size_t i = 0; i < backups.size(); ++i) // Use size_t for index
	{
		wcout << L"    " << (i + 1) << L". " << backups[i].filename().wstring() << endl;
		// Display 1-based index
	}
	wcout << L"   -------------------------------------------" << endl;
	wcout << L"   Enter a number to restore (or 'x' to cancel): ";

	string choice_str;
	getline(cin, choice_str);
	// Get user input
	if (choice_str == "x" || choice_str == "X") // Check for cancel
	{
		wcout << L"Cancelled." << endl;
		system("pause");
		return;
	}

	try {
		int choice_idx = stoi(choice_str) - 1; // Convert input to 0-based index
		// Validate the chosen index
		if (choice_idx >= 0 && choice_idx < backups.size())
		{
			fs::path backupToRestore = backups[choice_idx];
			// Get the path of the selected backup

			// Confirm overwrite
			wcout << endl << L"   ===================== WARNING =====================" << endl;
			wcout << L"    This will OVERWRITE your current save files with" << endl;
			wcout << L"    the local backup: " << backupToRestore.filename().wstring() << endl << endl;
			wcout << L"    ARE YOU SURE? (y/n)" << endl << L"> ";

			string confirm;
			getline(cin, confirm);
			if (confirm == "y" || confirm == "Y") // Proceed if confirmed
			{
				try {
					// Ensure the target save path exists and is a directory
					if (!fs::exists(selectedGame.savePath)) {
						fs::create_directories(selectedGame.savePath);
					}
					else if (!fs::is_directory(selectedGame.savePath)) {
						wcout << L"RESTORE FAILED: Save path exists but is not a directory." << endl;
						system("pause");
						return;
					}

					// Clear the existing save directory contents
					for (const auto& entry : fs::directory_iterator(selectedGame.savePath))
					{
						fs::remove_all(entry.path());
					}
					// Copy the backup contents to the save directory
					fs::copy(backupToRestore, selectedGame.savePath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
					wcout << L"Restore from local backup complete."
						<< endl;
				}
				catch (const fs::filesystem_error& e) { // Handle deletion/copy errors
					wcout << L"RESTORE FAILED: " << s2ws(e.what()) << endl;
				}
			}
			else // User cancelled overwrite
			{
				wcout << L"Restore cancelled." << endl;
			}
		}
		else { // Input number was out of range
			wcout << L"Invalid selection." << endl;
		}
	}
	catch (...) { // Handle non-numeric input for selection
		wcout << L"Invalid selection." << endl;
	}
	system("pause");
	// Pause after restore attempt or cancellation
}

/**
 * @brief Displays a menu listing cloud backups for the selected game and allows the user
 * to choose one to restore, overwriting the current save files after confirmation.
 */
void RestoreFromCloud()
{
	ClearScreen();
	// Check if cloud path is configured
	if (g_GoogleDrivePath.empty())
	{
		wcout << L"Cloud restore failed: Cloud Sync path is not set."
			<< endl;
		system("pause");
		return;
	}

	// Construct path to cloud backups for the current game
	wstring cloudGamePath = g_GoogleDrivePath + L"\\Game Save Backup Manager\\" + selectedGame.name;
	// Check if the cloud backup directory exists
	if (!fs::exists(cloudGamePath) || !fs::is_directory(cloudGamePath))
	{
		wcout << L"Cloud restore failed: No cloud backups found for " << selectedGame.name << L" at:" << endl;
		wcout << cloudGamePath << endl;
		system("pause");
		return;
	}

	vector<fs::path> backups; // Vector to hold paths of valid backup folders
	// Populate the vector with backup directories from the cloud path
	for (const auto& entry : fs::directory_iterator(cloudGamePath))
	{
		if (entry.is_directory()) // Only consider directories
		{
			backups.push_back(entry.path());
		}
	}
	// Sort backups by path name (timestamp) in descending order (newest first)
	sort(backups.rbegin(), backups.rend());
	if (backups.empty()) // Check if any backup folders were found
	{
		wcout << L"No backup folders found in the cloud for this game."
			<< endl;
		system("pause");
		return;
	}

	// Display the list of backups
	wcout << L"   --- Cloud Backups for " << selectedGame.name << L" ---" << endl;
	wcout << L"   (Newest first)" << endl << endl;
	for (size_t i = 0; i < backups.size(); ++i) // Use size_t for index
	{
		wcout << L"    " << (i + 1) << L". " << backups[i].filename().wstring() << endl;
		// Display 1-based index
	}
	wcout << L"   -------------------------------------------" << endl;
	wcout << L"   Enter a number to restore (or 'x' to cancel): ";

	string choice_str;
	getline(cin, choice_str);
	// Get user input
	if (choice_str == "x" || choice_str == "X") // Check for cancel
	{
		wcout << L"Cancelled." << endl;
		system("pause");
		return;
	}

	try {
		int choice_idx = stoi(choice_str) - 1; // Convert input to 0-based index
		// Validate the chosen index
		if (choice_idx >= 0 && choice_idx < backups.size())
		{
			fs::path backupToRestore = backups[choice_idx];
			// Get the path of the selected backup

			// Confirm overwrite
			wcout << endl << L"   ===================== WARNING =====================" << endl;
			wcout << L"    This will OVERWRITE your current save files with" << endl;
			wcout << L"    the cloud backup: " << backupToRestore.filename().wstring() << endl << endl;
			wcout << L"    ARE YOU SURE? (y/n)" << endl << L"> ";

			string confirm;
			getline(cin, confirm);
			if (confirm == "y" || confirm == "Y") // Proceed if confirmed
			{
				try {
					// Ensure the target save path exists and is a directory
					if (!fs::exists(selectedGame.savePath)) {
						fs::create_directories(selectedGame.savePath);
					}
					else if (!fs::is_directory(selectedGame.savePath)) {
						wcout << L"RESTORE FAILED: Save path exists but is not a directory." << endl;
						system("pause");
						return;
					}

					// Clear the existing save directory contents
					for (const auto& entry : fs::directory_iterator(selectedGame.savePath))
					{
						fs::remove_all(entry.path());
					}
					// Copy the backup contents to the save directory
					fs::copy(backupToRestore, selectedGame.savePath, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
					wcout << L"Restore from cloud complete."
						<< endl;
				}
				catch (const fs::filesystem_error& e) { // Handle deletion/copy errors
					wcout << L"RESTORE FAILED: " << s2ws(e.what()) << endl;
					// Add specific hint for cloud restores, as sync status matters
					wcout << L"Is the cloud client running and fully synced? Is the folder set to be available offline?"
						<< endl;
				}
			}
			else // User cancelled overwrite
			{
				wcout << L"Restore cancelled." << endl;
			}
		}
		else { // Input number was out of range
			wcout << L"Invalid selection." << endl;
		}
	}
	catch (...) { // Handle non-numeric input for selection
		wcout << L"Invalid selection." << endl;
	}
	system("pause");
	// Pause after restore attempt or cancellation
}

/**
 * @brief Opens the local backup folder for the specified game in Windows Explorer.
 */
void OpenBackupFolder(const GameProfile& profile)
{
	wstring path = GetExePath() + L"\\Backups\\" + profile.name;
	ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
	// Use ShellExecuteW for wide paths
}

/**
 * @brief Opens the configured cloud backup folder for the specified game in Windows Explorer.
 */
void OpenCloudBackupFolder(const GameProfile& profile)
{
	// Check if cloud path is configured
	if (g_GoogleDrivePath.empty())
	{
		wcout << L"Cloud Sync path not set." << endl;
		// Message displayed in console
		return;
	}

	wstring path = g_GoogleDrivePath + L"\\Game Save Backup Manager\\" + profile.name;
	// Construct the path
	// Check if the folder actually exists (might not if no cloud backups made yet)
	if (!fs::exists(path))
	{
		wcout << L"Cloud folder doesn't exist yet (no cloud backups made for this game)."
			<< endl;
		return;
	}
	ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL); // Open the folder
}

/**
 * @brief Opens the game's configured save path folder in Windows Explorer.
 */
void OpenSavePathFolder(const GameProfile& profile)
{
	// Check if the save path exists before trying to open it
	if (!fs::exists(profile.savePath))
	{
		wcout << L"Save path folder not found: " << profile.savePath << endl;
		// Message displayed in console
		return;
	}
	ShellExecuteW(NULL, L"open", profile.savePath.c_str(), NULL, NULL, SW_SHOWNORMAL); // Open the folder
}

// =========================================================================================
//                       AUTO-DETECT & VALIDATION FUNCTIONS
// =========================================================================================

/**
 * @brief Validates if a given path exists and points to a directory.
 Displays error if not.
 * @param path The wide string path to validate.
 * @return True if path is valid, False otherwise.
 */
bool ValidateGoogleDrivePath(const wstring& path)
{
	if (!fs::exists(path) || !fs::is_directory(path))
	{
		wcout << endl << L"   [ERROR] Path not found or is not a folder: " << path << endl;
		wcout << L"   Please check the path and try again." << endl;
		system("pause");
		return false;
	}
	return true;
	// Path is valid
}

/**
 * @brief Attempts to read the Google Drive for desktop config file to find the mount point.
 * @return The detected Google Drive path as a wide string, or an empty string if not found/error.
 */
wstring GetGoogleDrivePathFromConfig()
{
	PWSTR appDataPath = NULL; // Pointer to receive AppData path string
	// Get the path to the user's Local AppData folder
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &appDataPath) != S_OK)
	{
		CoTaskMemFree(appDataPath);
		// Free memory even on failure if allocated
		return L""; // Failed to get Local AppData path
	}

	// Construct the path to Google Drive's config file
	wstring configPath = appDataPath;
	configPath += L"\\Google\\DriveFS\\config\\config.json";
	CoTaskMemFree(appDataPath); // Free the memory allocated by SHGetKnownFolderPath

	// Check if the config file actually exists
	if (!fs::exists(configPath))
	{
		return L"";
		// Config file not found
	}

	// Open the config file for reading (as wide characters)
	wifstream configFile(configPath);
	if (!configFile.is_open())
	{
		return L"";
		// Failed to open file
	}

	wstring line;
	// Read the file line by line
	while (getline(configFile, line))
	{
		// Look for the line containing the default mount point key
		size_t pos = line.find(L"\"default_mount_point\":");
		if (pos != wstring::npos) // If the key is found
		{
			// Find the start and end quotes of the path value
			size_t start = line.find(L"\"", pos + 23); // Find first " after the key
			size_t end = line.find(L"\"", start + 1);  // Find the closing "
			if (start != wstring::npos && end != wstring::npos) // Ensure both quotes were found
			{
				// Extract the path substring
				wstring path = line.substr(start + 1, end - start - 1);
				// JSON escapes backslashes, so replace "\\" with "\"
				size_t p = path.find(L"\\\\");
				while (p != wstring::npos)
				{
					path.replace(p, 2, L"\\");
					p = path.find(L"\\\\", p + 1); // Find next occurrence
				}
				configFile.close(); // Close file before returning
				return path;
				// Return the extracted and unescaped path
			}
		}
	}
	configFile.close(); // Close file if loop finishes without finding path
	return L"";
	// Mount point not found in file
}

/**
 * @brief Implements the 3-tier logic for finding and setting the cloud sync path:
 * 1. Tries reading Google Drive config.
 * 2. Tries guessing common Google Drive mount points (D:\My Drive, etc.).
 * 3. Falls back to manual user input.
 * Saves the path to Config.ini if found and confirmed.
 */
void DetectAndSetGoogleDrivePath()
{
	ClearScreen();
	wcout << L"   Searching for Google Drive..." << endl;
	// --- Tier 1: Verified Suggestion (Read Google Drive Config) ---
	wstring verifiedPath = GetGoogleDrivePathFromConfig();
	if (!verifiedPath.empty() && fs::exists(verifiedPath)) // Check if path was found and exists
	{
		wcout << endl << L"   We found a *verified* Google Drive folder at this location:" << endl;
		wcout << L"   " << verifiedPath << endl << endl;
		wcout << L"   Is this correct? (y/n)" << endl << L"> ";
		string choice;
		getline(cin, choice);
		if (choice == "y" || choice == "Y")
		{
			if (ValidateGoogleDrivePath(verifiedPath)) // Double-check validity
			{
				g_GoogleDrivePath = verifiedPath; // Update global variable
				SaveGlobalConfig();
				// Save to INI
				wcout << L"   Path saved!" << endl;
				system("pause");
				return;
				// Path set successfully
			}
			// If validation failed (unlikely here), proceed to next tier
		}
		// If user entered 'n', proceed to next tier
	}

	// --- Tier 2: Best-Guess Suggestion (Scan Common Drive Letters) ---
	wcout << L"   Verified path not found or rejected. Scanning common drives..." << endl;
	for (wchar_t drive = L'D'; drive <= L'Z'; ++drive) // Check D: through Z:
	{
		wstring guessPath = wstring(1, drive) + L":\\My Drive";
		// Construct guess path
		if (fs::exists(guessPath) && fs::is_directory(guessPath)) // Check if it exists and is a folder
		{
			wcout << endl << L"   We found a *potential* Google Drive folder at this location:" << endl;
			wcout << L"   " << guessPath << endl << endl;
			wcout << L"   Is this correct? (y/n)" << endl << L"> ";
			string choice;
			getline(cin, choice);
			if (choice == "y" || choice == "Y")
			{
				if (ValidateGoogleDrivePath(guessPath)) // Validate the guessed path
				{
					g_GoogleDrivePath = guessPath; // Update global variable
					SaveGlobalConfig();
					// Save to INI
					wcout << L"   Path saved!" << endl;
					system("pause");
					return;
					// Path set successfully
				}
				// If validation failed, continue scanning other drives
			}
			// If user entered 'n', continue scanning other drives
		}
	}

	// --- Tier 3: Manual Fallback (User Input) ---
	wcout << endl << L"   Could not auto-detect Google Drive."
		<< endl;
	wcout << endl << L"   [This feature requires 'Google Drive for desktop' to be installed.]" << endl;
	wcout << L"   [See 'View Setup Instructions' for details.]" << endl << endl;
	wcout << L"   You can manually enter the path to your desired cloud sync folder below."
		<< endl << endl;
	wcout << L"   Example (Google Drive on G:):" << endl;
	wcout << L"   G:\\My Drive" << endl << endl;
	wcout << L"   Example (Dropbox in user folder):" << endl;
	wcout << L"   C:\\Users\\YourName\\Dropbox" << endl << endl;
	wcout << L"Enter the full path (or leave blank to cancel):" << endl << L"> ";

	wstring manualPath;
	getline(wcin, manualPath);
	// Get user input

	if (manualPath.empty()) // User cancelled
	{
		wcout << L"   Cancelled." << endl;
	}
	else if (ValidateGoogleDrivePath(manualPath)) // Validate user-provided path
	{
		g_GoogleDrivePath = manualPath; // Update global variable
		SaveGlobalConfig();
		// Save to INI
		wcout << L"   Path saved!" << endl;
	}
	// If validation failed, error message was shown by ValidateGoogleDrivePath
	system("pause");
}


// =========================================================================================
//                       AUTO-SAVE THREAD & UTILITIES
// =========================================================================================

/**
 * @brief Creates and starts the auto-save background thread.
 * @param profile The game profile to monitor (passed by value to the thread).
 */
void CreateAutoSaveThread(const GameProfile& profile)
{
	g_keepAutoSaving = true;
	// Set the flag to allow the thread loop to run
	// Create and detach the thread, passing the profile data by value
	g_autoSaveThread = thread(AutoSaveThreadFunction, profile);
	// Detach if you don't plan to join it later, but joining on exit is safer.
	// g_autoSaveThread.detach();
	// Consider implications if using detach
}

/**
 * @brief The function executed by the auto-save thread.
 Triggers backups periodically based on the interval.
 * @param profile A copy of the game profile data for this thread.
 */
void AutoSaveThreadFunction(GameProfile profile) // Takes profile by value
{
	// Loop continues as long as g_keepAutoSaving is true
	while (g_keepAutoSaving)
	{
		// Sleep for the specified interval, checking the stop flag every second
		for (int i = 0; i < profile.autoSaveInterval; ++i)
		{
			if (!g_keepAutoSaving) return; // Exit thread immediately if flag is false
			this_thread::sleep_for(1s); // Sleep for 1 second
		}

		if (!g_keepAutoSaving) return; // Check flag again after sleeping interval

		// --- Perform backup regardless of modification ---
		try
		{
			BackupSaveFolder(profile, true); // Perform auto-save backup
		}
		catch (const exception& e) // Catch potential errors during backup
		{
			wcout << L"Auto-save thread backup error: " << s2ws(e.what()) << endl;
			// Consider adding a longer sleep here to avoid spamming errors if backup fails repeatedly
		}
	} // End while loop
}

/**
 * @brief Gets the directory path where the executable is running.
 * @return The directory path as a wide string.
 */
wstring GetExePath()
{
	wchar_t buffer[MAX_PATH];
	GetModuleFileNameW(NULL, buffer, MAX_PATH);
	// Get full path of the executable
	wstring exePath = buffer;
	// Find the last backslash and return the substring before it
	return exePath.substr(0, exePath.find_last_of(L"\\"));
}

/**
 * @brief Gets just the filename of the running executable.
 * @return The filename as a wide string.
 */
wstring GetExeFilename()
{
	wchar_t buffer[MAX_PATH];
	GetModuleFileNameW(NULL, buffer, MAX_PATH);
	wstring exePath = buffer;
	size_t lastSlash = exePath.find_last_of(L"\\");
	if (lastSlash != wstring::npos) {
		// Return substring after the last backslash
		return exePath.substr(lastSlash + 1);
	}
	return exePath;
	// Should only happen if path has no backslashes (unlikely)
}


/**
 * @brief Converts a standard UTF-8 string to a wide string (wstring).
 * @param str The input UTF-8 string.
 * @return The converted wide string.
 */
wstring s2ws(const string& str)
{
	if (str.empty()) return L"";
	// Handle empty input
	// Calculate required buffer size
	int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
	wstring wstrTo(size_needed, 0);
	// Allocate wstring buffer
	// Perform conversion
	MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
	return wstrTo;
}

/**
 * @brief Converts a wide string (wstring) to a standard UTF-8 string.
 * @param wstr The input wide string.
 * @return The converted UTF-8 string.
 */
string ws2s(const wstring& wstr)
{
	if (wstr.empty()) return "";
	// Handle empty input
	// Calculate required buffer size
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	string strTo(size_needed, 0); // Allocate string buffer
	// Perform conversion
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}

/**
 * @brief Checks if a directory exists using stat (legacy method).
 * @param path Path to check (narrow string).
 * @return True if path exists and is a directory, False otherwise.
 */
bool DirectoryExists(const char* path)
{
	struct stat info;
	if (stat(path, &info) != 0) // stat returns 0 on success
		return false;
	// Path doesn't exist or error occurred
	else if (info.st_mode & S_IFDIR) // Check if the directory flag is set
		return true;
	// It's a directory
	else
		return false; // It exists but is not a directory
}

/**
 * @brief Checks if the execution directory is "clean" (only contains expected items).
 * Displays a warning and returns false if unexpected items are found.
 * @return True if directory is clean or if scan fails gracefully, False if anomalies found.
 */
bool CheckExecutionDirectory() {
	wstring exePath = GetExePath();
	wstring exeFilename = GetExeFilename();
	// List of allowed filenames/folder names in the program's directory
	vector<wstring> allowedItems;
	allowedItems.push_back(exeFilename); // The executable itself
	allowedItems.push_back(L"Config");      // Config folder
	allowedItems.push_back(L"Backups");
	// Backups folder
	// Note: Add runtime DLLs here if using dynamic linking and shipping them

	vector<wstring> anomalies;
	// List to store unexpected item names
	try {
		// Iterate through items in the executable's directory
		for (const auto& entry : fs::directory_iterator(exePath)) {
			wstring itemName = entry.path().filename().wstring();
			bool allowed = false;
			// Check if the current item is in the allowed list (case-insensitive)
			for (const auto& allowedItem : allowedItems) {
				if (_wcsicmp(itemName.c_str(), allowedItem.c_str()) == 0) {
					allowed = true;
					break;
				}
			}
			if (!allowed) { // If item is not in the allowed list
				anomalies.push_back(itemName);
				// Add it to the anomalies list
			}
		}
	}
	catch (const fs::filesystem_error& e) { // Handle errors scanning directory (e.g., permissions)
		ClearScreen();
		wcout << L"   ===================== WARNING =====================" << endl;
		wcout << L"    Could not scan the program's directory." << endl;
		wcout << L"    Error: " << s2ws(e.what()) << endl;
		wcout << L"    Continuing, but the directory might not be clean." << endl;
		wcout << L"   ===================================================" << endl << endl;
		system("pause");
		return true;
		// Allow continuation despite the error
	}

	// If anomalies were found, display the warning message
	if (!anomalies.empty()) {
		ClearScreen();
		wcout << L"   ===========================================" << endl;
		wcout << L"           DEDICATED FOLDER REQUIRED!"
			<< endl;
		wcout << L"   ===========================================" << endl << endl;
		wcout << L"    * This program requires its own folder to run correctly." << endl;
		wcout << L"--------------------------------------------------" << endl << endl;
		wcout << L"   --- Unexpected Items Found ---" << endl;
		// List the first 5 anomalies found
		for (size_t i = 0; i < std::min(anomalies.size(), static_cast<size_t>(5)); ++i) {
			wcout << L"    * " << anomalies[i] << endl;
		}
		if (anomalies.size() > 5) { // Indicate if more anomalies exist
			wcout << L"    * ... and " << (anomalies.size() - 5) << L" more."
				<< endl;
		}
		wcout << endl;
		wcout << L"--------------------------------------------------" << endl;
		wcout << L"   --- What To Do Now ---" << endl;
		wcout << L"    * Option 1: Move " << exeFilename << L" to a new, empty folder."
			<< endl;
		wcout << L"    * Option 2: Remove the unnecessary items listed above from" << endl;
		wcout << L"                the current folder."
			<< endl << endl;
		wcout << L"   The program will now exit." << endl;
		wcout << L"   -------------------------------------------" << endl << endl;
		return false; // Indicate failure, main() will exit
	}

	return true;
	// Directory is clean
}

/**
 * @brief Creates the required Config and Backups subdirectories if they don't exist.
 * Throws fs::filesystem_error on failure (e.g., lack of permissions).
 */
void CreateRequiredDirectories()
{
	string configPath = ws2s(GetExePath()) + "\\Config";
	string backupsPath = ws2s(GetExePath()) + "\\Backups";

	// Use C++17 filesystem to create directories; throws on error
	if (!fs::exists(configPath))
		fs::create_directories(configPath);
	if (!fs::exists(backupsPath))
		fs::create_directories(backupsPath);
}

/**
 * @brief Gets the current date and time formatted as "YYYY-MM-DD HH:MM:SS".
 * @return Formatted date/time string.
 */
string GetCurrentDateTime()
{
	time_t now = time(0); // Get current time_t
	tm ltm;
	localtime_s(&ltm, &now);
	// Convert to local time struct (safe version)
	// Use stringstream for formatted output
	stringstream ss;
	ss << (1900 + ltm.tm_year) << "-" // Year is years since 1900
		<< setfill('0') << setw(2) << (1 + ltm.tm_mon) << "-" // Month is 0-11
		<< setfill('0') << setw(2) << ltm.tm_mday << " " // Day of month
		<< setfill('0') << setw(2) << ltm.tm_hour << ":" // Hour (0-23)
		<< setfill('0') << setw(2) << ltm.tm_min << ":" // Minute (0-59)
		<< setfill('0') << setw(2) << ltm.tm_sec;
	// Second (0-59)
	return ss.str();
}

/**
 * @brief Gets the last modification time of the most recently modified file
 * within a directory (recursive).
 Used to detect save changes.
 * @param path The directory path (narrow string) to scan.
 * @return String representation of the time_t of the most recent file,
 * "no_files" if directory is empty, or current time string on error.
 */
string GetFileModTime(const string& path)
{
	if (!fs::exists(path)) return ""; // Return empty if path doesn't exist

	fs::file_time_type mostRecentTime;
	// Stores the latest time found
	bool foundFile = false; // Flag to track if any file was found

	try
	{
		// Recursively iterate through all files and subdirectories
		for (const auto& entry : fs::recursive_directory_iterator(path))
		{
			if (entry.is_regular_file()) // Only consider regular files
			{
				auto modTime = fs::last_write_time(entry);
				// Get modification time
				// Update if it's the first file found or newer than the current most recent
				if (!foundFile || modTime > mostRecentTime)
				{
					mostRecentTime = modTime;
					foundFile = true;
				}
			}
		}
	}
	catch (const fs::filesystem_error&) // Handle errors during iteration (e.g., permissions, file locked)
	{
		// On error, return current time to potentially trigger a backup conservatively
		time_t now_time = chrono::system_clock::to_time_t(chrono::system_clock::now());
		return to_string(now_time);
	}

	if (!foundFile) return "no_files"; // Return specific string if directory contained no files

	// Convert the found file_time_type to a time_t string for comparison
	auto sctp = chrono::time_point_cast<chrono::system_clock::duration>(mostRecentTime - fs::file_time_type::clock::now() + chrono::system_clock::now());
	time_t mod_time = chrono::system_clock::to_time_t(sctp);
	return to_string(mod_time);
}

/**
 * @brief Registers the global hotkeys used during monitoring.
 */
void RegisterHotKeys()
{
	RegisterHotKey(NULL, 1, MOD_CONTROL, 0x42); // CTRL+B (Manual Backup)
	RegisterHotKey(NULL, 2, MOD_CONTROL, 0x4F); // CTRL+O (Open Local Folder)
	RegisterHotKey(NULL, 3, MOD_CONTROL, 0x52);
	// CTRL+R (Restore Last Manual)
	RegisterHotKey(NULL, 4, MOD_CONTROL, 0x49); // CTRL+I (Show Help)
	RegisterHotKey(NULL, 5, MOD_CONTROL, 0x4D);
	// CTRL+M (Back to Menu)
	RegisterHotKey(NULL, 6, MOD_CONTROL, 0x47); // CTRL+G (Open Cloud Folder)
	RegisterHotKey(NULL, 7, MOD_CONTROL, 0x4C); // CTRL+L (List Restores)
	RegisterHotKey(NULL, 8, MOD_CONTROL, 0x50); // CTRL+P (Open Save Path)
}

/**
 * @brief Unregisters all global hotkeys.
 */
void UnRegisterHotKeys()
{
	UnregisterHotKey(NULL, 1);
	UnregisterHotKey(NULL, 2);
	UnregisterHotKey(NULL, 3);
	UnregisterHotKey(NULL, 4);
	UnregisterHotKey(NULL, 5);
	UnregisterHotKey(NULL, 6);
	UnregisterHotKey(NULL, 7);
	UnregisterHotKey(NULL, 8);
}

/**
 * @brief Signal handler for console close events (Ctrl+C, closing window).
 * Stops the auto-save thread and unregisters hotkeys before exiting.
 * @param s Signal number (unused but required by signature).
 */
void onSigBreakSignal(int s)
{
	g_keepAutoSaving = false; // Signal thread to stop
	if (g_autoSaveThread.joinable())
		g_autoSaveThread.join(); // Wait for thread to exit cleanly
	UnRegisterHotKeys();
	// Clean up hotkeys
	exit(1); // Exit program
}

/**
 * @brief Checks if a given wide string is a valid filename/directory name on Windows,
 * excluding reserved characters and names.
 * @param name The wide string name to check.
 * @return True if valid, False otherwise.
 */
bool IsValidFilename(const wstring& name)
{
	if (name.empty()) // Cannot be empty
	{
		return false;
	}
	// Check for invalid characters
	for (wchar_t c : name)
	{
		// Characters forbidden in Windows filenames/paths
		if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' || c == L'\\' || c == L'|' || c == L'?' || c == L'*')
		{
			return false;
		}
		// ASCII Control characters (0-31) are also invalid
		if (c < 32) return false;
	}
	// Check for reserved filenames (case-insensitive)
	wstring upperName = name;
	transform(upperName.begin(), upperName.end(), upperName.begin(), ::towupper); // Convert to uppercase for comparison
	// Standard reserved names
	if (upperName == L"CON" || upperName == L"PRN" || upperName == L"AUX" || upperName == L"NUL" ||
		// COM1-COM9
		(upperName.length() ==
			4 && upperName.substr(0, 3) == L"COM" && upperName[3] >= L'1' && upperName[3] <= L'9') ||
		// LPT1-LPT9
		(upperName.length() == 4 && upperName.substr(0, 3) == L"LPT" && upperName[3] >= L'1' && upperName[3] <= L'9')) {
		return false;
	}
	// Check for trailing spaces or dots, which are problematic
	if (!name.empty() && (name.back() == L' ' ||
		name.back() == L'.')) {
		return false;
	}

	// If none of the checks failed, the name is valid
	return true;
}