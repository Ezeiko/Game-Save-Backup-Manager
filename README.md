# Game Save Backup Manager v1.0.0

A Windows console application to automatically and manually back up your game save files locally and sync them to cloud storage, with convenient hotkey support during monitoring.

---

## Features ✨

* **Game Profiles:** Manage multiple games, each with its own:
    * Name
    * Save Folder Path
    * Auto-Save Interval (in minutes)
    * Cloud Sync Toggle (Enable/Disable per game)
* **Automatic Backups:** Runs in the background when monitoring a game, creating backups based purely on your chosen time interval.
* **Manual Backups:** Instantly create a timestamped manual backup using a hotkey (`CTRL + B`) anytime while monitoring.
* **Cloud Sync:**
    * Copies backups to a designated cloud sync folder (if enabled).
    * Auto-detects Google Drive for Desktop installation path.
    * Supports manually setting the path for other services (Dropbox, OneDrive, etc.).
* **Backup Retention:**
    * Set separate limits for the number of **Auto-Saves** and **Manual Saves** to keep.
    * Set separate limits for **Local** storage and **Cloud** storage.
    * Setting a limit to `0` keeps all backups of that type.
    * Automatically deletes the oldest backups when a limit is exceeded.
* **Restore Options:**
    * **Quick Restore (`CTRL + R`):** Instantly restores the most recent *manual* backup without confirmation.
    * **List Backups (`CTRL + L`):** Opens a menu to browse and restore any backup (Auto or Manual) from either Local or Cloud storage.
* **Hotkey Support (During Monitoring):**
    * `CTRL + B`: Create Manual Backup
    * `CTRL + R`: Quick Restore (Last Manual)
    * `CTRL + L`: List All Backups (Restore Menu)
    * `CTRL + O`: Open Local Backup Folder
    * `CTRL + G`: Open Cloud Backup Folder
    * `CTRL + P`: Open Game's Save Path Folder
    * `CTRL + I`: Show Help Screen
    * `CTRL + M`: Return to Main Menu
* **User Interface:** Simple console menu system for managing games and settings.
* **Logging:** Provides console output for backup operations, purges (with location tags and indentation), restores, and errors. Includes visual separators between operations.
* **Safety:** Checks if running from a dedicated folder to prevent accidental file clutter. Automatically creates necessary `Config` and `Backups` folders.

---

## Requirements 🖥️

* **Operating System:** Windows (uses Windows API functions).
* **Cloud Sync Client (Optional):** To use cloud sync, you need the official desktop client for your cloud service installed and running (e.g., Google Drive for Desktop, Dropbox desktop app, OneDrive). Google Drive must be set to 'Mirror files' mode for best reliability.

---

## Installation & Setup ⚙️

1.  * Download the .rar file from the release and extract.
2.  **Dedicated Folder:** **IMPORTANT:** Create a new, empty folder anywhere on your PC (e.g., `C:\GameSaveManager`). Place the `GameSaveBackupManager.exe` file inside this dedicated folder. The program needs its own folder to store configuration and backups correctly. It will warn you if it finds unexpected files in its directory on startup.
3.  **Run the Program:** Double-click `GameSaveBackupManager.exe`.
4.  **First Run - Cloud Setup (Optional):**
    * The program will ask if you want to set up Google Drive sync.
    * If you choose 'Yes', it will attempt to auto-detect your Google Drive path. Confirm if it's correct.
    * If not detected, or if you use another service, you can manually enter the full path to your desired cloud sync folder (e.g., `C:\Users\YourName\Dropbox`). See the in-program instructions (`Backup & Storage Settings` > `Cloud Sync Setup...` > `View Instructions...`) for details.
    * If you choose 'No' or cancel, cloud sync will be disabled globally, but you can set it up later via the main menu: `S. Backup & Storage Settings` > `3. Go to Cloud Sync Setup...`.
5.  **First Run - Add Game:** The program will then prompt you to add your first game. Follow the on-screen instructions to provide:
    * A name for the game profile.
    * The full path to the game's save folder.
    * The desired auto-save interval in minutes.
    * Whether to enable cloud backup for this specific game (only possible if the global cloud path is set).

---

## Usage Guide 🎮

### Main Menu (`Home Menu`)

* Lists all your added game profiles by number.
* Provides options:
    * `[Number]`: Select a game to view its sub-menu.
    * `C`: Add a New Game profile.
    * `S`: Configure Backup & Storage Settings (limits, cloud path).
    * `H`: Show the Help and Instructions screen.
    * `I`: Show Software Information.
    * `X`: Exit the program.

### Game Sub-Menu (After selecting a game)

* `1. Start Monitoring`: Begins the background backup process for the selected game and activates hotkeys.
* `2. Edit Game`: Change the name, save path, auto-save interval, or cloud sync setting for this game.
* `3. Restore from Local...`: Opens a menu to select and restore a backup from the local `Backups` folder.
* `4. Restore from Cloud...`: Opens a menu to select and restore a backup from the cloud folder (if configured).
* `5. Delete Game`: Removes the game profile and optionally deletes its associated local and cloud backups.
* `6. Back to Home Menu`: Returns to the main game list.

### Monitoring Screen (After choosing `1. Start Monitoring`)

* Displays the currently monitored game and its save path.
* Shows the list of active hotkeys (see Features section above).
* Logs backup, purge, and restore activities as they happen.
* Press `CTRL + M` to stop monitoring and return to the Home Menu.

### Backup & Storage Settings Menu

* Set global limits for how many Auto-Saves and Manual Saves are kept locally.
* Access the Cloud Sync Setup menu.
* Set global limits for how many Auto-Saves and Manual Saves are kept in the cloud.

---

## Backup Folder Structure 📁

* Backups are stored locally in a `Backups` subfolder within the program's directory.
* Cloud backups are stored in a `Game Save Backup Manager` folder inside your configured cloud path.
* Each game gets its own subfolder (named after the game profile).
* Individual backups are folders named using the format:
    `[Timestamp]-[YYYY-MM-DD_HH-MM-SS]-[Type]`
    * `[Timestamp]`: Unix epoch time (for chronological sorting).
    * `[YYYY-MM-DD_HH-MM-SS]`: Human-readable date and time of backup.
    * `[Type]`: `A` for Auto-Save, `M` for Manual Save.

---

## License 📜

This software is distributed under the **GNU General Public License v3.0**. See the `LICENSE` file (or program info screen) for more details.

---
