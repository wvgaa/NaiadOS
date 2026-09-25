#pragma once
#include <ncurses.h>
#include <string>
#include <vector>
#include <cstdio>
#include <iostream>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <algorithm>
#include <sys/wait.h>

inline std::string shellEscapeSingleQuoted(const std::string &s) {
    std::string result;
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result;
}

inline std::string escapeForDoubleQuotedShell(const std::string &s) {
    std::string result;
    for (char c : s) {
        if (c == '\\' || c == '$' || c == '`' || c == '"') result += '\\';
        result += c;
    }
    return result;
}

inline int runWithError(const std::string &cmd, std::string &errorOut) {
    std::string fullCmd = cmd + " 2>&1";
    FILE* pipe = popen(fullCmd.c_str(), "r");
    if (!pipe) {
        errorOut = "Failed to run command";
        return -1;
    }
    char buffer[256];
    errorOut = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        errorOut += buffer;
    }
    return pclose(pipe);
}

// Formats dev as fs ("ext4", "vfat" or "swap"). Old signatures are wiped first:
// leftover NTFS traces next to a fresh ext4 made mount guess the wrong type.
inline int formatPartition(const std::string &dev, const std::string &fs, std::string &errorOut) {
    int result = runWithError("wipefs -a " + dev, errorOut);
    if (result != 0) return result;

    std::string cmd = "mkfs.ext4 -F ";
    if (fs == "vfat") cmd = "mkfs.fat -F32 ";
    else if (fs == "swap") cmd = "mkswap ";

    result = runWithError(cmd + dev, errorOut);
    system("udevadm settle");
    return result;
}

// Always names the filesystem type; mount's own guess is not trusted.
inline int mountPartition(const std::string &dev, const std::string &target, const std::string &fs, std::string &errorOut) {
    return runWithError("mount -t " + fs + " " + dev + " " + target, errorOut);
}

struct UserAccount {
    std::string username;
    std::string password;
    bool sudoUser = true;
};

struct InstallConfig {
    std::string keyboardLayout = "";
    std::string locale = "";
    std::string diskDevice = "";
    std::string hostname = "";
    std::vector<UserAccount> users;
    std::string rootPassword = "";
    std::string uiChoice = "";
    std::string audioSystem = "";
    std::string timezone = "";
    std::string networkBackend = "";
    std::string kernel = "LTS";
    std::string bootloader = "GRUB";   // "GRUB" or "Existing" (install none, keep the machine's current one)
    bool freshEsp = false;             // the disk setup created a new, empty EFI partition
};

// Labels of the firmware's boot entries (efibootmgr, read-only), e.g.
// "Windows Boot Manager", "GRUB", "Linux Boot Manager" (systemd-boot), "Limine".
std::vector<std::string> detectBootEntries() {
    std::vector<std::string> labels;
    FILE *pipe = popen("efibootmgr 2>/dev/null", "r");
    if (!pipe) return labels;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        // Boot0003* Windows Boot Manager<TAB>HD(...)
        if (line.rfind("Boot", 0) != 0 || line.size() < 10) continue;
        bool numbered = true;
        for (int i = 4; i < 8; i++) numbered = numbered && isxdigit((unsigned char)line[i]);
        if (!numbered || (line[8] != '*' && line[8] != ' ')) continue;   // skips BootCurrent/BootOrder
        // only loaders on a disk partition; CD/USB/network slots have no HD() path
        if (line.find("HD(") == std::string::npos) continue;
        size_t start = line.find_first_not_of("* ", 8);
        if (start == std::string::npos) continue;
        std::string label = line.substr(start, line.find_first_of("\t\n", start) - start);
        while (!label.empty() && label.back() == ' ') label.pop_back();
        // the live USB and a previous NaiadOS install are not "other systems"
        std::string lower = label;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (label.empty() || lower.find("usb") != std::string::npos || lower.find("naiados") != std::string::npos ||
            lower.find("hard disk") != std::string::npos || lower.find("hard drive") != std::string::npos) continue;
        labels.push_back(label);
    }
    pclose(pipe);
    return labels;
}

// "Both" installs stable + LTS side by side, LTS stays in GRUB as the fallback
std::vector<std::string> kernelPackages(const InstallConfig &config) {
    if (config.kernel == "Stable") return {"linux-naiados"};
    if (config.kernel == "Both") return {"linux-naiados", "linux-naiados-lts"};
    return {"linux-naiados-lts"};
}

// package name -> suffix used by /boot/vmlinuz-<suffix> and /boot/initramfs-<suffix>.img
std::string kernelSuffix(const std::string &package) {
    return package.substr(std::string("linux-").size());
}

struct FreeSpaceRegion {
    std::string diskName;
    long long startS = 0;   // first sector, aligned to 1 MiB
    long long endS = 0;     // last sector (inclusive)
    long long sizeGiB = 0;
};

// Undo whatever an earlier disk step mounted, so a new choice starts clean.
void releaseInstallTarget() {
    system("swapoff -a 2>/dev/null");
    system("umount -R /mnt 2>/dev/null");
}

// Partition names currently on a disk, e.g. {"nvme0n1p1", "nvme0n1p3"}.
std::vector<std::string> partitionNames(const std::string &diskName);


struct PartInfo {
    std::string name;
    std::string size;
    std::string fstype;
    std::string label;
    std::string parttype;
    std::string mountpoints;
    std::string disk;
};

const std::string ESP_GUID = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
const std::string MSR_GUID = "e3c9e316-0b5c-4db8-817d-f92df00215ae";
const std::string WINRE_GUID = "de94bba4-06d1-4d40-a16a-bfd50179d6ac";
const std::string XBOOTLDR_GUID = "bc13c2ff-59e6-4262-a352-b275fd6f7172";

// lsblk -P prints KEY="value" pairs, so labels with spaces parse safely
std::string lsblkField(const std::string &line, const std::string &key) {
    std::string pattern = key + "=\"";
    size_t start = (line.compare(0, pattern.size(), pattern) == 0) ? 0 : line.find(" " + pattern);
    if (start == std::string::npos) return "";
    if (start != 0) start++;
    start += pattern.size();
    size_t end = line.find('"', start);
    return (end == std::string::npos) ? "" : line.substr(start, end - start);
}

std::vector<std::string> lsblkLines(const std::string &args) {
    std::vector<std::string> lines;
    FILE* pipe = popen(("lsblk -P " + args + " 2>/dev/null").c_str(), "r");
    if (!pipe) return lines;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') line.pop_back();
        lines.push_back(line);
    }
    pclose(pipe);
    return lines;
}

// The USB stick the live ISO booted from. Never offered as an install target.
std::string liveDiskName() {
    std::string disk;
    FILE* pipe = popen("findmnt -no SOURCE /run/archiso/bootmnt 2>/dev/null", "r");
    if (pipe) {
        char buffer[256];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string src(buffer);
            if (!src.empty() && src.back() == '\n') src.pop_back();
            for (const auto &line : lsblkLines("-o NAME,PKNAME " + src)) {
                std::string parent = lsblkField(line, "PKNAME");
                disk = parent.empty() ? lsblkField(line, "NAME") : parent;
                break;
            }
        }
        pclose(pipe);
    }
    if (!disk.empty()) return disk;

    // copytoram / img_dev boots: fall back to the ISO label
    for (const auto &line : lsblkLines("-o NAME,PKNAME,LABEL")) {
        if (lsblkField(line, "LABEL").rfind("NAIADOS_", 0) == 0) {
            std::string parent = lsblkField(line, "PKNAME");
            return parent.empty() ? lsblkField(line, "NAME") : parent;
        }
    }
    return "";
}

std::vector<std::string> detectDisks() {
    std::vector<std::string> disks;
    std::string live = liveDiskName();

    for (const auto &line : lsblkLines("-d -o NAME,TYPE")) {
        std::string name = lsblkField(line, "NAME");
        if (lsblkField(line, "TYPE") != "disk") continue;
        if (name.rfind("fd", 0) == 0) continue;
        if (name == live) continue;
        disks.push_back(name);
    }
    return disks;
}

// Free regions of at least 8 GiB (the install minimum). Works in exact sectors:
// parted's MB output is rounded, and a start rounded down would overlap the
// previous partition. Only GPT disks: an msdos label has no ESP type GUID.
std::vector<FreeSpaceRegion> detectFreeSpace(const std::string &diskName, std::string &label) {
    std::vector<FreeSpaceRegion> regions;
    std::string cmd = "parted -m /dev/" + diskName + " unit s print free 2>/dev/null";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return regions;

    long long sectorSize = 512;
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        std::vector<std::string> fields;
        size_t pos = 0;
        while ((pos = line.find(':')) != std::string::npos) {
            fields.push_back(line.substr(0, pos));
            line.erase(0, pos + 1);
        }
        // disk line: /dev/sda:1000215216s:scsi:512:4096:gpt:Model:;
        if (fields.size() >= 6 && fields[0] == "/dev/" + diskName) {
            sectorSize = atoll(fields[3].c_str());
            label = fields[5];
            continue;
        }
        // free line: 1:1026048s:976773119s:975747072s:free;
        if (fields.size() < 4 || line.rfind("free", 0) != 0) continue;

        long long align = 1048576 / (sectorSize > 0 ? sectorSize : 512);
        long long startS = atoll(fields[1].c_str());
        long long endS = atoll(fields[2].c_str());
        startS = ((startS + align - 1) / align) * align;
        endS = ((endS + 1) / align) * align - 1;
        if (endS <= startS) continue;

        long long sizeGiB = (endS - startS + 1) * sectorSize / (1024LL * 1024 * 1024);
        if (sizeGiB < 8) continue;

        FreeSpaceRegion region;
        region.diskName = diskName;
        region.startS = startS;
        region.endS = endS;
        region.sizeGiB = sizeGiB;
        regions.push_back(region);
    }

    pclose(pipe);
    return regions;
}

// Every partition except the live USB's. diskName limits it to one disk.
std::vector<PartInfo> detectPartitions(const std::string &diskName = "") {
    std::vector<PartInfo> parts;
    std::string live = liveDiskName();

    for (const auto &line : lsblkLines("-o NAME,SIZE,FSTYPE,LABEL,PARTTYPE,MOUNTPOINTS,PKNAME,TYPE")) {
        if (lsblkField(line, "TYPE") != "part") continue;
        PartInfo p;
        p.name = lsblkField(line, "NAME");
        p.size = lsblkField(line, "SIZE");
        p.fstype = lsblkField(line, "FSTYPE");
        p.label = lsblkField(line, "LABEL");
        p.parttype = lsblkField(line, "PARTTYPE");
        std::transform(p.parttype.begin(), p.parttype.end(), p.parttype.begin(), ::tolower);
        p.mountpoints = lsblkField(line, "MOUNTPOINTS");
        p.disk = lsblkField(line, "PKNAME");
        if (p.disk == live) continue;
        if (!diskName.empty() && p.disk != diskName) continue;
        parts.push_back(p);
    }
    return parts;
}

std::vector<std::string> partitionNames(const std::string &diskName) {
    std::vector<std::string> names;
    for (const auto &p : detectPartitions(diskName)) names.push_back(p.name);
    return names;
}

bool isEsp(const PartInfo &p) {
    return p.parttype == ESP_GUID;
}

bool hasEsp(const std::string &diskName) {
    for (const auto &p : detectPartitions(diskName)) {
        if (isEsp(p)) return true;
    }
    return false;
}

bool holdsLinux(const PartInfo &p) {
    static const std::vector<std::string> linuxFs = {
        "ext2", "ext3", "ext4", "btrfs", "xfs", "f2fs", "jfs", "reiserfs",
        "swap", "crypto_LUKS", "LVM2_member"
    };
    return p.parttype == XBOOTLDR_GUID
        || std::find(linuxFs.begin(), linuxFs.end(), p.fstype) != linuxFs.end();
}

std::string describePartition(const PartInfo &p) {
    std::string line = p.name + "  " + p.size;
    line += "  " + (p.fstype.empty() ? std::string("(no filesystem)") : p.fstype);
    if (!p.label.empty()) line += "  \"" + p.label + "\"";
    if (isEsp(p)) line += "  [EFI system partition]";
    else if (p.parttype == XBOOTLDR_GUID) line += "  [Linux boot partition]";
    else if (p.parttype == WINRE_GUID) line += "  [Windows recovery]";
    else if (holdsLinux(p)) line += "  [Linux data]";
    else if (p.fstype == "ntfs" || p.fstype == "BitLocker") line += "  [Windows data]";
    return line;
}

// Destructive actions need the word YES typed out, not a single keypress.
bool confirmTypedYes(const std::vector<std::string> &lines) {
    std::string typed;
    while (true) {
        clear();
        int row = 0;
        for (const auto &l : lines) mvprintw(row++, 2, "%s", l.c_str());
        mvprintw(row + 1, 2, "Type YES and press Enter to continue, Esc to cancel: %s", typed.c_str());
        refresh();

        int key = getch();
        if (key == 27) return false;
        if (key == 10 || key == KEY_ENTER) return typed == "YES";
        if (key == KEY_BACKSPACE || key == 127 || key == 8) {
            if (!typed.empty()) typed.pop_back();
        }
        else if (key >= 32 && key < 127 && typed.size() < 8) {
            typed += (char)key;
        }
    }
}

std::vector<std::string> detectTimezones() {
    std::vector<std::string> timezones;
    
    FILE* pipe = popen("timedatectl list-timezones", "r");
    if (!pipe) return timezones;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        timezones.push_back(line);
    }

    pclose(pipe);
    return timezones;
}

std::string toLower(const std::string &s) {
    std::string result = s;
    for (char &c : result) {
        c = tolower(c);
    }
    return result;
}

std::vector<std::string> filterRanked(const std::vector<std::string> &items, const std::string &query) {
    std::string lowerQuery = toLower(query);
    std::vector<std::pair<int, std::string>> matches;

    for (const auto &item : items) {
        std::string lowerItem = toLower(item);
        size_t pos = lowerItem.find(lowerQuery);
        if (pos == std::string::npos) continue;

        int rank = (lowerItem == lowerQuery) ? 0 : (pos == 0) ? 1 : 2;
        matches.push_back({rank, item});
    }

    std::stable_sort(matches.begin(), matches.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) return a.first < b.first;
        return toLower(a.second) < toLower(b.second);
    });

    std::vector<std::string> result;
    result.reserve(matches.size());
    for (auto &m : matches) result.push_back(m.second);
    return result;
}

std::vector<std::string> detectLocales() {
    std::vector<std::string> locales;
    
    FILE* pipe = popen("cat /usr/share/i18n/SUPPORTED 2>/dev/null | cut -d' ' -f1", "r");
    if (!pipe) return locales;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        if (!line.empty()) {
            locales.push_back(line);
        }
    }

    pclose(pipe);
    return locales;
}

std::vector<std::string> detectKeymaps() {
    std::vector<std::string> keymaps;
    
    FILE* pipe = popen("localectl list-keymaps", "r");
    if (!pipe) return keymaps;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        keymaps.push_back(line);
    }

    pclose(pipe);
    return keymaps;
}


void initTUI() {
    initscr();
    cbreak();
    noecho();
    curs_set(0);
    keypad(stdscr, TRUE);
    start_color();
    init_color(COLOR_CYAN, 0, 600, 600);
    init_pair(1, COLOR_CYAN, COLOR_BLACK);
}

void endTUI() {
    endwin();
}

void showWelcomeScreen() {
    clear();
    attron(COLOR_PAIR(1));
    printw("Welcome to NaiadOS Installer");
    attroff(COLOR_PAIR(1));
    printw("\n\nPress any key to continue...");
    refresh();
    getch();
}

void configureHostname(InstallConfig &config) {
    bool valid = false;

    while (!valid) {
        clear();
        printw("Enter hostname (letters, digits, hyphens only): ");
        refresh();

        std::string hostname = "";
        int ch;

        while (true) {
            ch = getch();

            if (ch == 10 || ch == KEY_ENTER) {
                break;
            }
            else if (ch == 27) {
                return;
            }
            else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!hostname.empty()) {
                    hostname.pop_back();
                    int y, x;
                    getyx(stdscr, y, x);
                    mvprintw(y, x - 1, " ");
                    move(y, x - 1);
                }
            }
            else if ((std::isalnum(ch) || ch == '-') && hostname.size() < 63) {
                hostname += (char)ch;
                printw("%c", (char)ch);
            }
            refresh();
        }

        if (hostname.empty() || hostname.front() == '-' || hostname.back() == '-') {
            clear();
            mvprintw(0, 2, "Invalid hostname: must be non-empty and not start or end with '-'.");
            mvprintw(1, 2, "Press any key to try again.");
            refresh();
            getch();
            continue;
        }

        config.hostname = hostname;
        valid = true;
    }
}

void configureKeyboardLayout(InstallConfig &config) {
    std::vector<std::string> allLayouts = detectKeymaps();

    if (allLayouts.empty()) {
        clear();
        mvprintw(0, 2, "No keyboard layouts found!");
        refresh();
        getch();
        return;
    }

    std::vector<std::string> filteredLayouts = allLayouts;
    std::string searchQuery = "";
    int selected = 0;
    int scrollOffset = 0;
    bool choosing = true;

    int boxWidth = 60;
    int visibleRows = 15;
    int boxHeight = visibleRows + 5;
    int startY = (LINES - boxHeight) / 2;
    int startX = (COLS - boxWidth) / 2;
    if (startY < 0) startY = 0;
    if (startX < 0) startX = 0;

    while (choosing) {
        clear();
        mvprintw(startY, startX, "Select Keyboard Layout (%d total) - Press / to search:", (int)filteredLayouts.size());
        if (!searchQuery.empty()) {
            mvprintw(startY + 1, startX, "Search: %s", searchQuery.c_str());
        }

        if (selected < scrollOffset) {
            scrollOffset = selected;
        }
        if (selected >= scrollOffset + visibleRows) {
            scrollOffset = selected - visibleRows + 1;
        }

        for (int i = 0; i < visibleRows; i++) {
            int itemIndex = scrollOffset + i;
            if (itemIndex >= (int)filteredLayouts.size()) break;

            if (itemIndex == selected) attron(A_REVERSE);
            mvprintw(startY + i + 3, startX + 2, "%s", filteredLayouts[itemIndex].c_str());
            if (itemIndex == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == '/') {
            searchQuery = "";
            echo();
            char input[100];
            mvprintw(startY + 1, startX, "Search: ");
            refresh();
            getnstr(input, 99);
            noecho();
            searchQuery = input;

            filteredLayouts = filterRanked(allLayouts, searchQuery);
            selected = 0;
            scrollOffset = 0;
        }
        else if (key == KEY_UP && !filteredLayouts.empty()) {
            selected--;
            if (selected < 0) selected = (int)filteredLayouts.size() - 1;
        }
        else if (key == KEY_DOWN && !filteredLayouts.empty()) {
            selected++;
            if (selected >= (int)filteredLayouts.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (!filteredLayouts.empty()) {
                config.keyboardLayout = filteredLayouts[selected];
                choosing = false;
            }
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}



void configureLocales(InstallConfig &config) {
    std::vector<std::string> allLocales = detectLocales();

    if (allLocales.empty()) {
        clear();
        mvprintw(0, 2, "No locales found!");
        refresh();
        getch();
        return;
    }

    std::vector<std::string> filteredLocales = allLocales;
    std::string searchQuery = "";
    int selected = 0;
    int scrollOffset = 0;
    bool choosing = true;

    int boxWidth = 60;
    int visibleRows = 15;
    int boxHeight = visibleRows + 5;
    int startY = (LINES - boxHeight) / 2;
    int startX = (COLS - boxWidth) / 2;
    if (startY < 0) startY = 0;
    if (startX < 0) startX = 0;

    while (choosing) {
        clear();
        mvprintw(startY, startX, "Select Locale (%d total) - Press / to search:", (int)filteredLocales.size());
        if (!searchQuery.empty()) {
            mvprintw(startY + 1, startX, "Search: %s", searchQuery.c_str());
        }

        if (selected < scrollOffset) {
            scrollOffset = selected;
        }
        if (selected >= scrollOffset + visibleRows) {
            scrollOffset = selected - visibleRows + 1;
        }

        for (int i = 0; i < visibleRows; i++) {
            int itemIndex = scrollOffset + i;
            if (itemIndex >= (int)filteredLocales.size()) break;

            if (itemIndex == selected) attron(A_REVERSE);
            mvprintw(startY + i + 3, startX + 2, "%s", filteredLocales[itemIndex].c_str());
            if (itemIndex == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == '/') {
            searchQuery = "";
            echo();
            char input[100];
            mvprintw(startY + 1, startX, "Search: ");
            refresh();
            getnstr(input, 99);
            noecho();
            searchQuery = input;

            filteredLocales = filterRanked(allLocales, searchQuery);
            selected = 0;
            scrollOffset = 0;
        }
        else if (key == KEY_UP && !filteredLocales.empty()) {
            selected--;
            if (selected < 0) selected = (int)filteredLocales.size() - 1;
        }
        else if (key == KEY_DOWN && !filteredLocales.empty()) {
            selected++;
            if (selected >= (int)filteredLocales.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (!filteredLocales.empty()) {
                config.locale = filteredLocales[selected];
                choosing = false;
            }
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void configureRootPassword(InstallConfig &config) {
    bool passwordMatch = false;

    while (!passwordMatch) {
        clear();
        printw("Enter root password: ");
        refresh();

        std::string password = "";
        int ch;

        while (true) {
            ch = getch();
            if (ch == 10 || ch == KEY_ENTER) break;
            else if (ch == 27) return;
            else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!password.empty()) {
                    password.pop_back();
                    int y, x;
                    getyx(stdscr, y, x);
                    mvprintw(y, x - 1, " ");
                    move(y, x - 1);
                }
            }
            else if (ch >= 32 && ch <= 126) {
                password += (char)ch;
                printw("*");
            }
            refresh();
        }

        clear();
        printw("Confirm root password: ");
        refresh();

        std::string confirmPassword = "";
        while (true) {
            ch = getch();
            if (ch == 10 || ch == KEY_ENTER) break;
            else if (ch == 27) return;
            else if (ch == KEY_BACKSPACE || ch == 127) {
                if (!confirmPassword.empty()) {
                    confirmPassword.pop_back();
                    int y, x;
                    getyx(stdscr, y, x);
                    mvprintw(y, x - 1, " ");
                    move(y, x - 1);
                }
            }
            else if (ch >= 32 && ch <= 126) {
                confirmPassword += (char)ch;
                printw("*");
            }
            refresh();
        }

        if (password == confirmPassword) {
            config.rootPassword = password;
            passwordMatch = true;
        } else {
            clear();
            mvprintw(0, 2, "Passwords do not match! Press any key to try again.");
            refresh();
            getch();
        }
    }
}

void configureUserAccount(InstallConfig &config) {
    bool addingUsers = true;

    while (addingUsers) {
        std::string username;
        bool validUsername = false;
        int ch;

        while (!validUsername) {
            clear();
            mvprintw(0, 2, "Add User Account");
            mvprintw(2, 2, "Enter username (letters, digits, '_', '-' only; leave empty to finish):");
            mvprintw(3, 2, "> ");
            refresh();

            username = "";
            while (true) {
                ch = getch();
                if (ch == 10 || ch == KEY_ENTER) break;
                else if (ch == 27) return;
                else if (ch == KEY_BACKSPACE || ch == 127) {
                    if (!username.empty()) {
                        username.pop_back();
                        int y, x;
                        getyx(stdscr, y, x);
                        mvprintw(y, x - 1, " ");
                        move(y, x - 1);
                    }
                }
                else if ((std::isalnum(ch) || ch == '_' || ch == '-') && username.size() < 32) {
                    username += (char)ch;
                    printw("%c", (char)ch);
                }
                refresh();
            }

            if (username.empty()) {
                validUsername = true;
                break;
            }

            if (std::isdigit((unsigned char)username.front()) || username.front() == '-') {
                clear();
                mvprintw(0, 2, "Invalid username: must start with a letter or '_'.");
                mvprintw(1, 2, "Press any key to try again.");
                refresh();
                getch();
                continue;
            }

            validUsername = true;
        }

        if (username.empty()) {
            addingUsers = false;
            break;
        }

        std::string password = "";
        bool passwordMatch = false;

        while (!passwordMatch) {
            clear();
            mvprintw(0, 2, "Add User Account - %s", username.c_str());
            mvprintw(2, 2, "Enter password:");
            mvprintw(3, 2, "> ");
            refresh();

            password = "";
            while (true) {
                ch = getch();
                if (ch == 10 || ch == KEY_ENTER) break;
                else if (ch == 27) return;
                else if (ch == KEY_BACKSPACE || ch == 127) {
                    if (!password.empty()) {
                        password.pop_back();
                        int y, x;
                        getyx(stdscr, y, x);
                        mvprintw(y, x - 1, " ");
                        move(y, x - 1);
                    }
                }
                else {
                    password += (char)ch;
                    printw("*");
                }
                refresh();
            }

            clear();
            mvprintw(0, 2, "Add User Account - %s", username.c_str());
            mvprintw(2, 2, "Confirm password:");
            mvprintw(3, 2, "> ");
            refresh();

            std::string confirmPassword = "";
            while (true) {
                ch = getch();
                if (ch == 10 || ch == KEY_ENTER) break;
                else if (ch == 27) return;
                else if (ch == KEY_BACKSPACE || ch == 127) {
                    if (!confirmPassword.empty()) {
                        confirmPassword.pop_back();
                        int y, x;
                        getyx(stdscr, y, x);
                        mvprintw(y, x - 1, " ");
                        move(y, x - 1);
                    }
                }
                else {
                    confirmPassword += (char)ch;
                    printw("*");
                }
                refresh();
            }

            if (password == confirmPassword) {
                passwordMatch = true;
            } else {
                clear();
                mvprintw(0, 2, "Passwords do not match! Press any key to try again.");
                refresh();
                getch();
            }
        }

        bool sudoUser = true;
        bool choosingSudo = true;
        int selected = 0;
        std::vector<std::string> sudoOptions = {"Yes (recommended)", "No"};

        while (choosingSudo) {
            clear();
            mvprintw(0, 2, "Add User Account - %s", username.c_str());
            mvprintw(2, 2, "Grant sudo privileges to this user?");

            for (int i = 0; i < (int)sudoOptions.size(); i++) {
                if (i == selected) attron(A_REVERSE);
                mvprintw(i + 4, 4, "%s", sudoOptions[i].c_str());
                if (i == selected) attroff(A_REVERSE);
            }

            refresh();
            int key = getch();

            if (key == KEY_UP) {
                selected--;
                if (selected < 0) selected = 1;
            }
            else if (key == KEY_DOWN) {
                selected++;
                if (selected > 1) selected = 0;
            }
            else if (key == 10 || key == KEY_ENTER) {
                sudoUser = (selected == 0);
                choosingSudo = false;
            }
            else if (key == 27) {
                return;
            }
        }

        UserAccount user;
        user.username = username;
        user.password = password;
        user.sudoUser = sudoUser;
        config.users.push_back(user);

        clear();
        mvprintw(0, 2, "User '%s' added successfully!", username.c_str());
        mvprintw(2, 2, "Currently configured users:");
        int row = 3;
        for (const auto &u : config.users) {
            mvprintw(row++, 4, "- %s (%s)", u.username.c_str(), u.sudoUser ? "sudo" : "no sudo");
        }
        mvprintw(row + 1, 2, "Add another user? [y/N]");
        refresh();

        int key = getch();
        if (key != 'y' && key != 'Y') {
            addingUsers = false;
        }
    }
}

void configureUI(InstallConfig & config) {
    std::vector<std::string> items = {
        "-- Window Managers --",
        "Hyprland",
        "i3",
        "bspwm",
        "Sway",
        "Niri",
        "Qtile",
        "Wayfire",
        "-- Desktop Environments --",
        "GNOME",
        "KDE Plasma",
        "Xfce",
        "Budgie",
        "Cinnamon",
        "COSMIC",
        "LXDE",
        "MATE"
    };

    int selected = 1;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select UI:");

        for (int i = 0; i < (int)items.size(); i++) {
            bool isHeader = (items[i].substr(0, 2) == "--");
            if (i == selected && !isHeader) attron(A_REVERSE);
            mvprintw(i + 2, isHeader ? 2 : 4, "%s", items[i].c_str());
            if (i == selected && !isHeader) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            do {
                selected--;
                if (selected < 0) selected = (int)items.size() - 1;
            } while (items[selected].substr(0, 2) == "--");
        }
        else if (key == KEY_DOWN) {
            do {
                selected++;
                if (selected >= (int)items.size()) selected = 0;
            } while (items[selected].substr(0, 2) == "--");
        }
        else if (key == 10 || key == KEY_ENTER) {
        config.uiChoice = items[selected];
        choosing = false;
        }
        else if (key == 27) {
        choosing = false;
    }
    }
}

void configureAudio(InstallConfig &config) {
    std::vector<std::string> options = {
        "pipewire", 
        "pulseaudio", 
        "none"
    };

    int selected = 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select Audio Drivers:");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", options[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            config.audioSystem = options[selected];
            choosing = false;
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void configureKernel(InstallConfig &config) {
    std::vector<std::string> options = {
        "LTS",
        "Stable",
        "Both"
    };
    std::vector<std::string> descriptions = {
        "linux-naiados-lts  - long-term support, the safe default",
        "linux-naiados      - latest stable kernel",
        "both               - stable by default, LTS as a fallback GRUB entry"
    };

    int selected = 0;
    for (int i = 0; i < (int)options.size(); i++) {
        if (options[i] == config.kernel) selected = i;
    }
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select Kernel:");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", descriptions[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            config.kernel = options[selected];
            choosing = false;
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void showMessage(const std::vector<std::string> &lines);

void configureBootloader(InstallConfig &config) {
    std::vector<std::string> found = detectBootEntries();

    std::vector<std::string> options = {"GRUB", "Existing"};
    std::vector<std::string> descriptions = {
        "NaiadOS GRUB  - its own GRUB; other systems (Windows, other Linux) appear in its menu",
        "Keep existing - install no bootloader; add NaiadOS from your other system's bootloader"
    };

    int selected = (config.bootloader == "Existing") ? 1 : 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select Bootloader:");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", descriptions[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        int row = (int)options.size() + 3;
        mvprintw(row++, 2, "Boot entries already on this machine:");
        if (found.empty()) mvprintw(row++, 4, "(none found)");
        for (const auto &label : found) mvprintw(row++, 4, "- %s", label.c_str());
        row++;
        mvprintw(row++, 2, "NaiadOS GRUB puts its own folder (EFI/NaiadOS) on the EFI partition and");
        mvprintw(row++, 2, "boots first. Other bootloaders stay untouched and reachable in the firmware boot menu.");

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (options[selected] == "Existing" && config.freshEsp) {
                showMessage({"The disk setup created a new, empty EFI partition, so there is no",
                             "existing bootloader left to keep. Choose NaiadOS GRUB."});
                continue;
            }
            if (options[selected] == "Existing" && found.empty()) {
                showMessage({"No other bootloader was found on this machine.",
                             "Without one, NaiadOS would not boot. Choose NaiadOS GRUB."});
                continue;
            }
            config.bootloader = options[selected];
            choosing = false;
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void configureProfile(InstallConfig &config) {
    std::vector<std::string> options = {
        "UI",
        "Audio",
        "Back"
    };

    int selected = 0;
    bool running = true;

    while (running) {
        clear();
        mvprintw(0, 2, "Profile Configuration");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) attron(A_REVERSE);

            std::string label = options[i];
            if (options[i] == "UI" && !config.uiChoice.empty()) {
                label = "UI: " + config.uiChoice;
            }

            if (options[i] == "Audio" && !config.audioSystem.empty()) {
                label = "Audio: " + config.audioSystem;
            }

            mvprintw(i + 2, 2, "%s", label.c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (options[selected] == "UI") {
            configureUI(config);
        }
        else if (options[selected] == "Audio") {
        configureAudio(config);
        }
        else if (options[selected] == "Back") {
        running = false;
        }
    }
        else if (key == 27) {
        running = false;
        }
    }
}

bool confirmDiskWipe(const std::string &diskName) {
    std::vector<std::string> warning = {
        "You are about to ERASE THE WHOLE DISK /dev/" + diskName + ".",
        "",
        "Partitions on it (all of them will be destroyed):"
    };

    std::vector<PartInfo> parts = detectPartitions(diskName);
    bool otherSystems = false;
    for (const auto &p : parts) {
        warning.push_back("    /dev/" + describePartition(p));
        if (holdsLinux(p) || p.fstype == "ntfs" || p.fstype == "BitLocker" || isEsp(p)) otherSystems = true;
    }
    if (parts.empty()) warning.push_back("    (none)");

    if (otherSystems) {
        warning.push_back("");
        warning.push_back("WARNING: this disk holds other operating systems or their boot files.");
        warning.push_back("To keep them, go back and use 'free space' or 'an existing partition'.");
    }

    bool confirmed = confirmTypedYes(warning);

    if (confirmed) releaseInstallTarget();

    return confirmed;
}

void showDiskResultSummary() {
    clear();
    mvprintw(0, 2, "Disk Configuration Complete:");
    mvprintw(2, 2, "Current mounted layout:");

    std::string cmd = "lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINT --ascii 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    int row = 3;
    if (pipe) {
        char buffer[256];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            mvprintw(row, 4, "%s", buffer);
            row++;
        }
        pclose(pipe);
    }

    mvprintw(row + 1, 2, "Press any key to continue.");
    refresh();
    getch();
}


bool autoPartitionDisk(const std::string &diskName) {
    std::string disk = "/dev/" + diskName;
    std::string err;

    int result1 = runWithError("parted --script " + disk + " mklabel gpt", err);
    if (result1 != 0) {
        std::cout << "\n[ERROR] Failed to create GPT label: " << err << std::endl;
        return false;
    }

    int result2 = runWithError("parted --script " + disk + " mkpart \"EFI\" fat32 1MiB 513MiB", err);
    if (result2 != 0) {
        std::cout << "\n[ERROR] Failed to create EFI partition: " << err << std::endl;
        return false;
    }

    int result3 = runWithError("parted --script " + disk + " mkpart \"root\" ext4 513MiB 100%", err);
    if (result3 != 0) {
        std::cout << "\n[ERROR] Failed to create root partition: " << err << std::endl;
        return false;
    }

    int result4 = runWithError("parted --script " + disk + " set 1 esp on", err);
    if (result4 != 0) {
        std::cout << "\n[ERROR] Failed to set ESP flag: " << err << std::endl;
        return false;
    }

    system(("partprobe " + disk).c_str());
    system("udevadm settle");

    return true;
}

bool formatPartitions(const std::string &diskName) {
    std::string partPrefix = diskName;
    if (diskName.find("nvme") != std::string::npos || diskName.find("mmcblk") != std::string::npos) {
        partPrefix += "p";
    }

    std::string efiPartition = "/dev/" + partPrefix + "1";
    std::string rootPartition = "/dev/" + partPrefix + "2";
    std::string err;

    int result1 = formatPartition(efiPartition, "vfat", err);
    if (result1 != 0) {
        std::cout << "\n[ERROR] Failed to format EFI partition " << efiPartition << ": " << err << std::endl;
        return false;
    }

    int result2 = formatPartition(rootPartition, "ext4", err);
    if (result2 != 0) {
        std::cout << "\n[ERROR] Failed to format root partition " << rootPartition << ": " << err << std::endl;
        return false;
    }

    return true;
}

bool mountPartitions(const std::string &diskName) {
    std::string partPrefix = diskName;
    if (diskName.find("nvme") != std::string::npos || diskName.find("mmcblk") != std::string::npos) {
        partPrefix += "p";
    }

    std::string efiPartition = "/dev/" + partPrefix + "1";
    std::string rootPartition = "/dev/" + partPrefix + "2";
    std::string err;

    int result1 = mountPartition(rootPartition, "/mnt", "ext4", err);
    if (result1 != 0) {
        std::cout << "\n[ERROR] Failed to mount root partition " << rootPartition << ": " << err << std::endl;
        return false;
    }

    system("mkdir -p /mnt/boot/efi");

    int result3 = mountPartition(efiPartition, "/mnt/boot/efi", "vfat", err);
    if (result3 != 0) {
        std::cout << "\n[ERROR] Failed to mount EFI partition " << efiPartition << ": " << err << std::endl;
        return false;
    }

    return true;
}

// Finds the ESP by its partition type GUID, not "first FAT partition": a FAT
// data partition would otherwise end up as /boot/efi. Only mounted, never formatted.
bool mountExistingEFI(const std::string &diskName) {
    std::vector<PartInfo> esps;
    for (const auto &p : detectPartitions(diskName)) {
        if (isEsp(p)) esps.push_back(p);
    }
    if (esps.empty()) return false;

    int selected = 0;
    if (esps.size() > 1) {
        bool choosing = true;
        while (choosing) {
            clear();
            mvprintw(0, 2, "%s has more than one EFI system partition. Which one should NaiadOS use?", diskName.c_str());
            mvprintw(1, 2, "It is only mounted, never formatted: other systems' boot files stay untouched.");
            for (int i = 0; i < (int)esps.size(); i++) {
                if (i == selected) attron(A_REVERSE);
                mvprintw(i + 3, 4, "%s", describePartition(esps[i]).c_str());
                if (i == selected) attroff(A_REVERSE);
            }
            refresh();

            int key = getch();
            if (key == KEY_UP) {
                selected--;
                if (selected < 0) selected = (int)esps.size() - 1;
            }
            else if (key == KEY_DOWN) {
                selected++;
                if (selected >= (int)esps.size()) selected = 0;
            }
            else if (key == 10 || key == KEY_ENTER) {
                choosing = false;
            }
            else if (key == 27) {
                return false;
            }
        }
    }

    system("mkdir -p /mnt/boot/efi");
    std::string err;
    if (mountPartition("/dev/" + esps[selected].name, "/mnt/boot/efi", "vfat", err) != 0) {
        clear();
        mvprintw(0, 2, "Failed to mount EFI partition /dev/%s:", esps[selected].name.c_str());
        mvprintw(1, 2, "%s", err.c_str());
        mvprintw(3, 2, "Press any key.");
        refresh();
        getch();
        return false;
    }
    return true;
}

bool createSwapfile() {
    std::string memCmd = "free -g | awk '/^Mem:/{print $2}'";
    FILE* pipe = popen(memCmd.c_str(), "r");
    int ramGB = 4;
    if (pipe) {
        char buffer[32];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            ramGB = atoi(buffer);
        }
        pclose(pipe);
    }

    int swapGB = 4;
    if (ramGB <= 4) swapGB = 8;
    else if (ramGB <= 16) swapGB = 4;
    else swapGB = 2;

    std::cout << "\n==> RAM detected: " << ramGB << "GB, creating " << swapGB << "GB swapfile" << std::endl;

    std::string err;

    int result1 = runWithError("fallocate -l " + std::to_string(swapGB) + "G /mnt/swapfile", err);
    if (result1 != 0) {
        std::cout << "\n[ERROR] Failed to create swapfile: " << err << std::endl;
        return false;
    }

    system("chmod 600 /mnt/swapfile");

    int result3 = runWithError("mkswap /mnt/swapfile", err);
    if (result3 != 0) {
        std::cout << "\n[ERROR] Failed to set up swap: " << err << std::endl;
        return false;
    }

    int result4 = runWithError("swapon /mnt/swapfile", err);
    if (result4 != 0) {
        std::cout << "\n[ERROR] Failed to enable swap: " << err << std::endl;
        return false;
    }

    return true;
}

void configureExistingPartition(InstallConfig &config) {
    std::vector<PartInfo> partitions;
    for (const auto &p : detectPartitions()) {
        // mounted partitions are in use; ESPs hold other systems' bootloaders;
        // the Microsoft reserved partition is a tiny placeholder, never a target
        if (!p.mountpoints.empty() || isEsp(p) || p.parttype == MSR_GUID) continue;
        partitions.push_back(p);
    }

    if (partitions.empty()) {
        clear();
        mvprintw(0, 2, "No usable partitions found.");
        mvprintw(1, 2, "The live USB, mounted partitions and EFI partitions are never offered.");
        mvprintw(3, 2, "Press any key.");
        refresh();
        getch();
        return;
    }

    int selected = 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select an existing partition for the NaiadOS root (it will be FORMATTED as ext4):");
        mvprintw(1, 2, "Not listed: the live USB, mounted partitions and EFI partitions.");

        for (int i = 0; i < (int)partitions.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 3, 4, "%s", describePartition(partitions[i]).c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)partitions.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)partitions.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            const PartInfo &part = partitions[selected];

            std::vector<std::string> warning = {
                "You are about to FORMAT this partition as ext4:",
                "",
                "    /dev/" + describePartition(part),
                "",
                "EVERYTHING on it will be erased. Nothing else on the disk is touched."
            };
            if (holdsLinux(part)) {
                warning.push_back("");
                warning.push_back("WARNING: it holds Linux data (" + (part.fstype.empty() ? std::string("boot partition") : part.fstype) + ").");
                warning.push_back("If another Linux system lives here, it will be DESTROYED.");
            }
            else if (part.fstype == "ntfs" || part.fstype == "BitLocker") {
                warning.push_back("");
                warning.push_back("WARNING: it holds Windows data (" + part.fstype + "). It will be lost.");
            }
            else if (!part.fstype.empty()) {
                warning.push_back("");
                warning.push_back("WARNING: it holds a " + part.fstype + " filesystem. Its files will be lost.");
            }

            if (!hasEsp(part.disk)) {
                showMessage({"/dev/" + part.disk + " has no EFI system partition, so NaiadOS could not boot from it.",
                             "Nothing was formatted. Use manual partitioning (cfdisk) to create one."});
                continue;
            }

            if (!confirmTypedYes(warning)) continue;
            releaseInstallTarget();

            clear();
            mvprintw(0, 2, "Formatting /dev/%s as ext4, please wait...", part.name.c_str());
            refresh();

            std::string err;
            if (formatPartition("/dev/" + part.name, "ext4", err) != 0) {
                clear();
                mvprintw(0, 2, "Formatting failed! Press any key to return.");
                mvprintw(1, 2, "%s", err.c_str());
                refresh();
                getch();
                continue;
            }

            std::string mountErr;
            if (mountPartition("/dev/" + part.name, "/mnt", "ext4", mountErr) != 0) {
                clear();
                mvprintw(0, 2, "Mounting failed! Press any key to return.");
                mvprintw(1, 2, "%s", mountErr.c_str());
                refresh();
                getch();
                continue;
            }

            if (!mountExistingEFI(part.disk)) {
                releaseInstallTarget();
                showMessage({"The EFI partition could not be mounted, so the disk setup was undone.",
                             "Choose the partition again."});
                continue;
            }

            config.diskDevice = part.name;
            config.freshEsp = false;
            showDiskResultSummary();
            choosing = false;
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void configureFreeSpace(InstallConfig &config) {
    std::vector<std::string> disks = detectDisks();

    if (disks.empty()) {
        clear();
        mvprintw(0, 2, "No disks found!");
        refresh();
        getch();
        return;
    }

    int selected = 0;
    bool choosingDisk = true;
    std::string chosenDisk;

    while (choosingDisk) {
        clear();
        mvprintw(0, 2, "Select disk to check for free space:");

        for (int i = 0; i < (int)disks.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", disks[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)disks.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)disks.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            chosenDisk = disks[selected];
            choosingDisk = false;
        }
        else if (key == 27) {
            return;
        }
    }

    std::string label;
    std::vector<FreeSpaceRegion> regions = detectFreeSpace(chosenDisk, label);

    if (label != "gpt") {
        showMessage({"/dev/" + chosenDisk + " has no GPT partition table (found: " + (label.empty() ? "none" : label) + ").",
                     "NaiadOS boots in UEFI mode and needs GPT. Use manual partitioning (cfdisk) instead."});
        return;
    }
    if (!hasEsp(chosenDisk)) {
        showMessage({"/dev/" + chosenDisk + " has no EFI system partition, so NaiadOS could not boot from it.",
                     "Use manual partitioning (cfdisk) to create one."});
        return;
    }
    if (regions.empty()) {
        showMessage({"No free space of 8 GB or more on /dev/" + chosenDisk + "."});
        return;
    }

    selected = 0;
    bool choosingRegion = true;

    while (choosingRegion) {
        clear();
        mvprintw(0, 2, "Select free space region to use (8 GB or more):");

        for (int i = 0; i < (int)regions.size(); i++) {
            std::string line = std::to_string(regions[i].sizeGiB) + " GB free  (sectors " +
                               std::to_string(regions[i].startS) + " - " + std::to_string(regions[i].endS) + ")";
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", line.c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)regions.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)regions.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            FreeSpaceRegion chosen = regions[selected];
            std::string disk = "/dev/" + chosenDisk;

            releaseInstallTarget();

            clear();
            mvprintw(0, 2, "Creating partition in free space, please wait...");
            refresh();

            // the new partition is whatever appears on the disk now; parted lists
            // partitions by position, and a new one takes the lowest free number
            std::vector<std::string> before = partitionNames(chosenDisk);

            std::string cmd = "parted -m --script " + disk + " unit s mkpart root ext4 " +
                              std::to_string(chosen.startS) + "s " + std::to_string(chosen.endS) + "s";
            std::string partErr;
            if (runWithError(cmd, partErr) != 0) {
                showMessage({"Partitioning failed:", partErr});
                choosingRegion = false;
                continue;
            }

            system(("partprobe " + disk).c_str());
            system("udevadm settle");

            std::vector<std::string> added;
            for (const auto &name : partitionNames(chosenDisk)) {
                if (std::find(before.begin(), before.end(), name) == before.end()) added.push_back(name);
            }
            if (added.size() != 1) {
                showMessage({"Could not tell which partition is the new one, so nothing was formatted.",
                             "Check the disk with lsblk and use manual partitioning instead."});
                choosingRegion = false;
                continue;
            }
            std::string newPartition = "/dev/" + added[0];

            clear();
            mvprintw(0, 2, "Formatting %s, please wait...", newPartition.c_str());
            refresh();

            std::string err;
            if (formatPartition(newPartition, "ext4", err) != 0) {
                showMessage({"Formatting " + newPartition + " failed:", err});
                choosingRegion = false;
                continue;
            }

            if (mountPartition(newPartition, "/mnt", "ext4", err) != 0) {
                showMessage({"Mounting " + newPartition + " failed:", err});
                choosingRegion = false;
                continue;
            }

            if (!mountExistingEFI(chosenDisk)) {
                releaseInstallTarget();
                showMessage({"The EFI partition could not be mounted, so the disk setup was undone.",
                             newPartition + " stays on the disk (empty); choose it again under 'Use an existing partition'."});
                choosingRegion = false;
                continue;
            }

            config.diskDevice = added[0];
            config.freshEsp = false;
            showDiskResultSummary();
            choosingRegion = false;
        }
        else if (key == 27) {
            choosingRegion = false;
        }
    }
}


void configureWholeDiskInstall(InstallConfig &config) {
    std::vector<std::string> disks = detectDisks();

    if (disks.empty()) {
        clear();
        mvprintw(0, 2, "No disks found!");
        refresh();
        getch();
        return;
    }

    int selected = 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select disk to install NaiadOS on:");

        for (int i = 0; i < (int)disks.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", disks[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)disks.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)disks.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
if (confirmDiskWipe(disks[selected])) {
    clear();
    mvprintw(0, 2, "==> Partitioning %s...", disks[selected].c_str());
    mvprintw(1, 2, "Please wait.");
    refresh();

    if (autoPartitionDisk(disks[selected])) {
        clear();
        mvprintw(0, 2, "==> Formatting partitions on %s...", disks[selected].c_str());
        mvprintw(1, 2, "Please wait.");
        refresh();

    if (formatPartitions(disks[selected])) {
        clear();
        mvprintw(0, 2, "==> Mounting partitions...");
        mvprintw(1, 2, "Please wait.");
        refresh();
        sleep(2);

    if (mountPartitions(disks[selected])) {
        clear();
        mvprintw(0, 2, "Create a swapfile?");
        mvprintw(1, 2, "Recommended if you have 8GB RAM or less.");
        mvprintw(3, 2, "[Y] Yes    [N] No");
        refresh();
        int swapKey = getch();

    if (swapKey == 'y' || swapKey == 'Y') {
        clear();
        mvprintw(0, 2, "==> Creating swapfile...");
        mvprintw(1, 2, "Please wait.");
        refresh();
    if (!createSwapfile()) {
        clear();
        mvprintw(0, 2, "Swapfile creation failed! Continuing without swap.");
        mvprintw(1, 2, "Press any key to continue.");
        refresh();
        getch();
        }
    }

        config.diskDevice = disks[selected];
        config.freshEsp = true;
        showDiskResultSummary();
        choosing = false;
    }
    else {
        clear();
        mvprintw(0, 2, "Mounting failed! Press any key to return.");
        refresh();
        getch();
        }
    }
    else {
            clear();
            mvprintw(0, 2, "Formatting failed! Press any key to return.");
            refresh();
            getch();
        }
    }
    else {
        clear();
        mvprintw(0, 2, "Partitioning failed! Press any key to return.");
        refresh();
        getch();
    }
}
}
        else if (key == 27) {
            choosing = false;
        }
    }
}

void showMessage(const std::vector<std::string> &lines) {
    clear();
    int row = 0;
    for (const auto &l : lines) mvprintw(row++, 2, "%s", l.c_str());
    mvprintw(row + 1, 2, "Press any key to continue.");
    refresh();
    getch();
}

// Returns the index into parts, parts.size() + i for extras[i], or -1 on Esc.
int pickPartition(const std::string &title, const std::string &hint,
                  const std::vector<PartInfo> &parts,
                  const std::vector<std::string> &extras = {}) {
    int total = (int)parts.size() + (int)extras.size();
    if (total == 0) return -1;
    int selected = 0;

    while (true) {
        clear();
        mvprintw(0, 2, "%s", title.c_str());
        mvprintw(1, 2, "%s", hint.c_str());
        for (int i = 0; i < total; i++) {
            std::string line = (i < (int)parts.size())
                ? "/dev/" + describePartition(parts[i])
                : extras[i - parts.size()];
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 3, 4, "%s", line.c_str());
            if (i == selected) attroff(A_REVERSE);
        }
        refresh();

        int key = getch();
        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = total - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= total) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            return selected;
        }
        else if (key == 27) {
            return -1;
        }
    }
}

std::string dataWarning(const PartInfo &p) {
    if (holdsLinux(p) && p.fstype != "swap") return "/dev/" + p.name + " holds Linux data (" + p.fstype + ") - it will be DESTROYED.";
    if (p.fstype == "ntfs" || p.fstype == "BitLocker") return "/dev/" + p.name + " holds Windows data - it will be lost.";
    if (!p.fstype.empty() && p.fstype != "swap") return "/dev/" + p.name + " holds a " + p.fstype + " filesystem - its files will be lost.";
    return "";
}

void configureManualPartitioning(InstallConfig &config) {
    std::vector<std::string> disks = detectDisks();
    if (disks.empty()) {
        showMessage({"No disks found (the live USB is never offered)."});
        return;
    }

    std::vector<PartInfo> none;
    std::vector<std::string> diskLabels;
    for (const auto &d : disks) diskLabels.push_back("/dev/" + d);
    int d = pickPartition("Manual partitioning: which disk should cfdisk open?",
                          "cfdisk starts next. Nothing is written until you choose 'Write' inside cfdisk.",
                          none, diskLabels);
    if (d < 0) return;
    std::string disk = disks[d];

    // release anything an earlier disk step mounted, so cfdisk can rewrite the table
    releaseInstallTarget();

    def_prog_mode();
    endwin();
    std::cout << "\n==> Starting cfdisk on /dev/" << disk << std::endl;
    std::cout << "    NaiadOS needs: one partition for root (type 'Linux filesystem') and an" << std::endl;
    std::cout << "    'EFI System' partition (300M+). An existing EFI partition can be reused." << std::endl;
    std::cout << "    Optional: swap ('Linux swap') and /home. Press Enter to open cfdisk." << std::endl;
    std::cin.get();
    system(("cfdisk /dev/" + disk).c_str());
    system(("partprobe /dev/" + disk + " 2>/dev/null; udevadm settle").c_str());
    reset_prog_mode();
    refresh();

    std::vector<PartInfo> parts;
    for (const auto &p : detectPartitions(disk)) {
        if (p.mountpoints.empty()) parts.push_back(p);
    }

    // --- root
    std::vector<PartInfo> rootCands;
    for (const auto &p : parts) {
        if (!isEsp(p) && p.parttype != MSR_GUID) rootCands.push_back(p);
    }
    if (rootCands.empty()) {
        showMessage({"No partition available for root on /dev/" + disk + ".",
                     "Run manual partitioning again and create one (type 'Linux filesystem')."});
        return;
    }
    int r = pickPartition("Which partition is the NaiadOS root ( / )?",
                          "It will be FORMATTED as ext4.", rootCands);
    if (r < 0) return;
    PartInfo root = rootCands[r];

    // --- EFI
    std::vector<PartInfo> esps;
    for (const auto &p : parts) {
        if (isEsp(p)) esps.push_back(p);
    }
    if (esps.empty()) {
        showMessage({"No EFI system partition on /dev/" + disk + ".",
                     "Run manual partitioning again: create a partition (300M or more)",
                     "and set its type to 'EFI System' in cfdisk."});
        return;
    }
    int e = 0;
    if (esps.size() > 1) {
        e = pickPartition("Which EFI system partition should NaiadOS use?",
                          "An existing one is only mounted: other systems' boot files stay.", esps);
        if (e < 0) return;
    }
    PartInfo esp = esps[e];
    // a freshly created ESP has no filesystem yet; an existing one is never formatted
    bool formatEsp = esp.fstype.empty();

    // --- swap
    std::vector<PartInfo> swapCands;
    for (const auto &p : parts) {
        if (p.name != root.name && !isEsp(p) && p.parttype != MSR_GUID) swapCands.push_back(p);
    }
    int s = pickPartition("Swap (optional):",
                          "A partition picked here becomes swap.",
                          swapCands, {"Swapfile on the root partition", "No swap"});
    if (s < 0) return;
    bool useSwapPart = s < (int)swapCands.size();
    bool useSwapfile = s == (int)swapCands.size();
    PartInfo swapPart;
    if (useSwapPart) swapPart = swapCands[s];

    // --- /home
    std::vector<PartInfo> homeCands;
    for (const auto &p : swapCands) {
        if (!useSwapPart || p.name != swapPart.name) homeCands.push_back(p);
    }
    int h = pickPartition("Separate /home (optional):",
                          "Keep an existing /home to reuse your files, or format it.",
                          homeCands, {"No separate /home"});
    if (h < 0) return;
    bool useHome = h < (int)homeCands.size();
    PartInfo home;
    bool formatHome = false;
    if (useHome) {
        home = homeCands[h];
        if (home.fstype.empty()) {
            formatHome = true;
        } else {
            clear();
            mvprintw(0, 2, "/dev/%s already holds a %s filesystem.", home.name.c_str(), home.fstype.c_str());
            mvprintw(2, 2, "[K] Keep it and its files (reuse an old /home)");
            mvprintw(3, 2, "[F] Format it as ext4 (erase everything on it)");
            mvprintw(5, 2, "Esc to cancel.");
            refresh();
            int key;
            do { key = getch(); } while (key != 'k' && key != 'K' && key != 'f' && key != 'F' && key != 27);
            if (key == 27) return;
            formatHome = (key == 'f' || key == 'F');
        }
    }

    // --- summary + typed YES
    std::vector<std::string> summary = {
        "Manual partitioning on /dev/" + disk + ":",
        "",
        "  /           /dev/" + root.name + "  -> FORMAT as ext4",
        "  /boot/efi   /dev/" + esp.name + "  -> " + (formatEsp ? "FORMAT as FAT32 (new, empty)" : "keep, mount only"),
    };
    if (useSwapPart) summary.push_back("  swap        /dev/" + swapPart.name + "  -> " + (swapPart.fstype == "swap" ? "use existing swap" : "FORMAT as swap"));
    else if (useSwapfile) summary.push_back("  swap        swapfile on root");
    else summary.push_back("  swap        none");
    if (useHome) summary.push_back("  /home       /dev/" + home.name + "  -> " + (formatHome ? "FORMAT as ext4" : "keep existing files"));

    std::vector<std::string> warnings;
    if (!dataWarning(root).empty()) warnings.push_back(dataWarning(root));
    if (useSwapPart && swapPart.fstype != "swap" && !dataWarning(swapPart).empty()) warnings.push_back(dataWarning(swapPart));
    if (useHome && formatHome && !dataWarning(home).empty()) warnings.push_back(dataWarning(home));
    if (!warnings.empty()) {
        summary.push_back("");
        summary.push_back("WARNING:");
        for (const auto &w : warnings) summary.push_back("  " + w);
    }
    if (!confirmTypedYes(summary)) return;

    // --- format + mount
    clear();
    mvprintw(0, 2, "==> Formatting and mounting, please wait...");
    refresh();

    std::string err;
    if (formatPartition("/dev/" + root.name, "ext4", err) != 0) {
        showMessage({"Formatting root /dev/" + root.name + " failed:", err});
        return;
    }
    if (formatEsp && formatPartition("/dev/" + esp.name, "vfat", err) != 0) {
        showMessage({"Formatting EFI partition /dev/" + esp.name + " failed:", err});
        return;
    }
    if (useSwapPart && swapPart.fstype != "swap" && formatPartition("/dev/" + swapPart.name, "swap", err) != 0) {
        showMessage({"Creating swap on /dev/" + swapPart.name + " failed:", err});
        return;
    }
    if (useHome && formatHome && formatPartition("/dev/" + home.name, "ext4", err) != 0) {
        showMessage({"Formatting /home /dev/" + home.name + " failed:", err});
        return;
    }

    if (mountPartition("/dev/" + root.name, "/mnt", "ext4", err) != 0) {
        showMessage({"Mounting root failed:", err});
        return;
    }
    system("mkdir -p /mnt/boot/efi");
    if (mountPartition("/dev/" + esp.name, "/mnt/boot/efi", "vfat", err) != 0) {
        showMessage({"Mounting the EFI partition failed:", err});
        return;
    }
    if (useHome) {
        system("mkdir -p /mnt/home");
        if (mountPartition("/dev/" + home.name, "/mnt/home", formatHome ? "ext4" : home.fstype, err) != 0) {
            showMessage({"Mounting /home failed:", err});
            return;
        }
    }
    // genfstab picks up active swap, so it lands in the installed fstab
    if (useSwapPart && runWithError("swapon /dev/" + swapPart.name, err) != 0) {
        showMessage({"Enabling swap failed (continuing without it):", err});
    }
    if (useSwapfile && !createSwapfile()) {
        showMessage({"Swapfile creation failed! Continuing without swap."});
    }

    config.diskDevice = root.name;
    config.freshEsp = formatEsp;
    showDiskResultSummary();
}

void configureDiskConfiguration(InstallConfig &config) {
    std::vector<std::string> diskOptions = {
        "Use entire disk (erase everything)",
        "Use free space",
        "Use an existing partition",
        "Manual partitioning (cfdisk)",
        "Back"
    };

    int selected = 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Disk Configuration Method:");

        for (int i = 0; i < (int)diskOptions.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", diskOptions[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)diskOptions.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)diskOptions.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (diskOptions[selected] == "Use entire disk (erase everything)") {
                configureWholeDiskInstall(config);
                choosing = false;
            }
            else if (diskOptions[selected] == "Use free space") {
                configureFreeSpace(config);
                choosing = false;
            }
            else if (diskOptions[selected] == "Use an existing partition") {
                configureExistingPartition(config);
                choosing = false;
            }
            else if (diskOptions[selected] == "Manual partitioning (cfdisk)") {
                configureManualPartitioning(config);
                choosing = false;
            }
            else if (diskOptions[selected] == "Back") {
                choosing = false;
            }
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}


void configureTimezone(InstallConfig &config) {
    std::vector<std::string> allTimezones = detectTimezones();

    if (allTimezones.empty()) {
        clear();
        mvprintw(0, 2, "No timezones found!");
        refresh();
        getch();
        return;
    }

    std::vector<std::string> filteredTimezones = allTimezones;
    std::string searchQuery = "";
    int selected = 0;
    int scrollOffset = 0;
    bool choosing = true;

    int boxWidth = 60;
    int visibleRows = 15;
    int boxHeight = visibleRows + 5;
    int startY = (LINES - boxHeight) / 2;
    int startX = (COLS - boxWidth) / 2;
    if (startY < 0) startY = 0;
    if (startX < 0) startX = 0;

    while (choosing) {
        clear();
        mvprintw(startY, startX, "Select Timezone (%d total) - Press / to search:", (int)filteredTimezones.size());
        if (!searchQuery.empty()) {
            mvprintw(startY + 1, startX, "Search: %s", searchQuery.c_str());
        }

        if (selected < scrollOffset) {
            scrollOffset = selected;
        }
        if (selected >= scrollOffset + visibleRows) {
            scrollOffset = selected - visibleRows + 1;
        }

        for (int i = 0; i < visibleRows; i++) {
            int itemIndex = scrollOffset + i;
            if (itemIndex >= (int)filteredTimezones.size()) break;

            if (itemIndex == selected) attron(A_REVERSE);
            mvprintw(startY + i + 3, startX + 2, "%s", filteredTimezones[itemIndex].c_str());
            if (itemIndex == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == '/') {
            searchQuery = "";
            echo();
            char input[100];
            mvprintw(startY + 1, startX, "Search: ");
            refresh();
            getnstr(input, 99);
            noecho();
            searchQuery = input;

            filteredTimezones = filterRanked(allTimezones, searchQuery);
            selected = 0;
            scrollOffset = 0;
        }
        else if (key == KEY_UP && !filteredTimezones.empty()) {
            selected--;
            if (selected < 0) selected = (int)filteredTimezones.size() - 1;
        }
        else if (key == KEY_DOWN && !filteredTimezones.empty()) {
            selected++;
            if (selected >= (int)filteredTimezones.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            if (!filteredTimezones.empty()) {
                config.timezone = filteredTimezones[selected];
                choosing = false;
            }
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void configureNetwork(InstallConfig &config) {
    std::vector<std::string> options = {"NetworkManager (default backend)", "NetworkManager (iwd backend)"};
    int selected = 0;
    bool choosing = true;

    while (choosing) {
        clear();
        mvprintw(0, 2, "Select Network Configuration:");

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) attron(A_REVERSE);
            mvprintw(i + 2, 4, "%s", options[i].c_str());
            if (i == selected) attroff(A_REVERSE);
        }

        refresh();
        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        }
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        }
        else if (key == 10 || key == KEY_ENTER) {
            config.networkBackend = options[selected];
            choosing = false;
        }
        else if (key == 27) {
            choosing = false;
        }
    }
}

void showInstallProgress(const std::string &message) {
    std::cout << "\n==> " << message << std::endl;
}

bool checkInternet() {
    showInstallProgress("Checking internet connection...");
    int result = system("ping -c 1 -W 3 8.8.8.8 > /dev/null 2>&1");
    return (result == 0);
}

// Online half of the live-ISO setup. The boot only does the offline keyring
// init, so nothing waits for a network that Wi-Fi-only machines don't have yet.
bool prepareLiveSystem() {
    // no-op if the boot-time keyring init already finished; waits for it otherwise
    system("systemctl start naiadinstall-keyring.service");

    // reflector at boot fails without internet; the live ISO's fallback mirrors still work
    if (system("grep -qi reflector /etc/pacman.d/mirrorlist") != 0) {
        showInstallProgress("Refreshing mirror list...");
        if (system("reflector @/etc/xdg/reflector/reflector.conf") != 0) {
            std::cout << "\n[WARNING] reflector failed, continuing with the default mirrors." << std::endl;
        }
    }

    showInstallProgress("Updating pacman keyring...");
    return (system("pacman -Sy --noconfirm archlinux-keyring") == 0);
}

bool checkDiskSpace() {
    showInstallProgress("Checking available disk space...");
    FILE* pipe = popen("df -BG /mnt | awk 'NR==2 {print $4}' | tr -d 'G'", "r");
    if (!pipe) return false;
    
    char buffer[32];
    int freeGB = 0;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        freeGB = atoi(buffer);
    }
    pclose(pipe);
    
    return (freeGB >= 8);
}

bool installBasePackages(InstallConfig &config) {
    showInstallProgress("Installing base system...");

    // fastfetch: the installer ships its NaiadOS config, the program has to come with it
    std::string packages = "base linux-firmware efibootmgr networkmanager sudo mkinitcpio fastfetch";
    // os-prober lets grub-mkconfig list Windows and other Linux installs; it can only
    // look inside unmounted partitions through grub-mount, which needs fuse3
    if (config.bootloader == "GRUB") packages += " grub os-prober fuse3";

    for (const auto &kernel : kernelPackages(config)) packages += " " + kernel;

    if (config.uiChoice == "Hyprland") packages += " hyprland waybar wofi kitty sddm";
    else if (config.uiChoice == "i3") packages += " xorg-server i3-wm i3status dmenu rxvt-unicode lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "bspwm") packages += " xorg-server bspwm sxhkd dmenu rxvt-unicode lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "Sway") packages += " sway waybar dmenu foot sddm";
    else if (config.uiChoice == "Niri") packages += " niri waybar fuzzel foot sddm";
    else if (config.uiChoice == "Qtile") packages += " xorg-server qtile dmenu rxvt-unicode lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "Wayfire") packages += " wayfire wf-shell wofi foot sddm";
    else if (config.uiChoice == "GNOME") packages += " gnome gnome-extra gdm";
    else if (config.uiChoice == "KDE Plasma") packages += " plasma kde-applications sddm";
    else if (config.uiChoice == "Xfce") packages += " xorg-server xfce4 xfce4-goodies lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "Budgie") packages += " budgie gdm";
    else if (config.uiChoice == "Cinnamon") packages += " cinnamon lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "COSMIC") packages += " cosmic greetd";
    else if (config.uiChoice == "LXDE") packages += " lxde lightdm lightdm-gtk-greeter";
    else if (config.uiChoice == "MATE") packages += " mate mate-extra lightdm lightdm-gtk-greeter";

    if (config.audioSystem == "pipewire") packages += " pipewire pipewire-pulse wireplumber";
    else if (config.audioSystem == "pulseaudio") packages += " pulseaudio";

    if (config.networkBackend == "NetworkManager (iwd backend)") packages += " iwd";

    std::string cmd = "pacstrap /mnt " + packages;
    int result = system(cmd.c_str());

    if (result == 0) {
        system("cp -r /root/naiados-repo /mnt/root/naiados-repo");
        system("sed -i '/\\[naiados-repo\\]/,/^$/d' /mnt/etc/pacman.conf");
    }

    return (result == 0);
}

bool generateFstab() {
    showInstallProgress("Generating fstab...");
    system("truncate -s 0 /mnt/etc/fstab");
    int result = system("genfstab -U /mnt >> /mnt/etc/fstab");
    return (result == 0);
}

bool configureChrootSystem(InstallConfig &config) {
    showInstallProgress("Configuring system...");

    auto chrootRun = [](const std::string &cmd, const std::string &stepName) -> bool {
        std::string full = "arch-chroot /mnt /bin/bash -c \"" + escapeForDoubleQuotedShell(cmd) + "\"";
        int result = system(full.c_str());
        if (result != 0) {
            std::cout << "\n[ERROR] Step failed: " << stepName << std::endl;
        }
        return (result == 0);
    };

    // Same as the live ISO's. Values must stay quoted: scripts source this file
    // under set -e, and an unquoted ANSI_COLOR=1;36 runs "36" as a command
    // (killed grub-mkconfig's 31_efi_bootnext silently).
    if (!chrootRun(
        "printf '%s\\n' "
        "'NAME=\"NaiadOS\"' "
        "'PRETTY_NAME=\"NaiadOS\"' "
        "'ID=naiados' "
        "'ID_LIKE=arch' "
        "'BUILD_ID=rolling' "
        "'ANSI_COLOR=\"1;36\"' "
        "'HOME_URL=\"https://naiados.org\"' "
        "'LOGO=naiados' > /etc/os-release",
        "OS branding")) return false;

    if (config.bootloader == "GRUB") {
        if (!chrootRun(
            "sed -i 's/GRUB_DISTRIBUTOR=.*/GRUB_DISTRIBUTOR=NaiadOS/' /etc/default/grub",
            "GRUB branding")) return false;

        if (!chrootRun(
            "sed -i '/^GRUB_CMDLINE_LINUX_DEFAULT/ s/\\<quiet\\>[[:space:]]*//' /etc/default/grub",
            "Remove quiet boot flag")) return false;

        // other systems on the machine (Windows, another Linux) show up in the NaiadOS menu
        if (!chrootRun(
            "sed -i '/^#\\?GRUB_DISABLE_OS_PROBER=/d' /etc/default/grub && "
            "printf '%s\\n' 'GRUB_DISABLE_OS_PROBER=false' >> /etc/default/grub",
            "Enable os-prober")) return false;

        // grub 2.16 adds an "(EFI BootNext)" entry for every firmware slot: NaiadOS
        // itself, dead entries, USB, "Internal Hard Disk". os-prober covers the real systems.
        if (!chrootRun(
            "sed -i '/^#\\?GRUB_DISABLE_BOOTNEXT=/d' /etc/default/grub && "
            "printf '%s\\n' 'GRUB_DISABLE_BOOTNEXT=true' >> /etc/default/grub",
            "Disable BootNext entries")) return false;

        // grub-mkconfig would otherwise sort naiados-lts above naiados and boot LTS by default
        if (config.kernel == "Both") {
            if (!chrootRun(
                "sed -i '/^#\\?GRUB_TOP_LEVEL=/d' /etc/default/grub && "
                "printf '%s\\n' 'GRUB_TOP_LEVEL=\"/boot/vmlinuz-naiados\"' >> /etc/default/grub",
                "GRUB default kernel")) return false;
        }
    }

    if (!chrootRun(
        "printf '%s' '" + config.hostname + "' > /etc/hostname && "
        "printf '%s\\n' '127.0.0.1 localhost' > /etc/hosts && "
        "printf '%s\\n' '::1 localhost' >> /etc/hosts && "
        "printf '%s\\n' '127.0.1.1 " + config.hostname + ".localdomain " + config.hostname + "' >> /etc/hosts",
        "Hostname")) return false;

    if (!chrootRun(
        "printf '%s\\n' '" + config.locale + " UTF-8' >> /etc/locale.gen && "
        "locale-gen && "
        "printf '%s\\n' 'LANG=" + config.locale + "' > /etc/locale.conf",
        "Locale")) return false;

    if (!chrootRun(
        "ln -sf /usr/share/zoneinfo/" + config.timezone + " /etc/localtime && "
        "hwclock --systohc",
        "Timezone")) return false;

    if (!chrootRun(
        "printf '%s\\n' 'KEYMAP=" + config.keyboardLayout + "' > /etc/vconsole.conf",
        "Keymap")) return false;

    {
        std::string serviceCmd = "systemctl enable NetworkManager && systemctl enable systemd-timesyncd";

        if (config.uiChoice == "GNOME" || config.uiChoice == "Budgie")
            serviceCmd += " && systemctl enable gdm";
        else if (config.uiChoice == "KDE Plasma" || config.uiChoice == "Hyprland" || config.uiChoice == "Sway" || config.uiChoice == "Niri" || config.uiChoice == "Wayfire")
            serviceCmd += " && systemctl enable sddm";
        else if (config.uiChoice == "i3" || config.uiChoice == "bspwm" || config.uiChoice == "Xfce" || config.uiChoice == "Qtile" || config.uiChoice == "Cinnamon" || config.uiChoice == "LXDE" || config.uiChoice == "MATE")
            serviceCmd += " && systemctl enable lightdm";
        else if (config.uiChoice == "COSMIC")
            serviceCmd += " && systemctl enable cosmic-greeter";

        if (config.networkBackend == "NetworkManager (iwd backend)")
            serviceCmd += " && systemctl enable iwd";

        if (!chrootRun(serviceCmd, "Enable services")) return false;
    }

    for (const auto &user : config.users) {
        std::string addCmd;
        if (user.sudoUser)
            addCmd = "useradd -m -G wheel -s /bin/bash " + user.username;
        else
            addCmd = "useradd -m -s /bin/bash " + user.username;

        if (!chrootRun(addCmd, "Create user: " + user.username)) return false;

        if (!chrootRun(
            "printf '%s:%s' '" + user.username + "' '" + shellEscapeSingleQuoted(user.password) + "' | chpasswd",
            "Set password for: " + user.username)) return false;
    }

    if (!chrootRun(
        "printf '%s:%s' 'root' '" + shellEscapeSingleQuoted(config.rootPassword) + "' | chpasswd",
        "Root password")) return false;

    if (!chrootRun(
        "sed -i 's/# %wheel ALL=(ALL:ALL) ALL/%wheel ALL=(ALL:ALL) ALL/' /etc/sudoers",
        "Sudoers")) return false;

    if (!chrootRun(
        "printf 'MODULES=()\\nBINARIES=()\\nFILES=()\\nHOOKS=(base udev autodetect modconf block filesystems keyboard fsck)\\n' > /etc/mkinitcpio.conf",
        "mkinitcpio config")) return false;

    for (const auto &kernel : kernelPackages(config)) {
        std::string suffix = kernelSuffix(kernel);
        if (!chrootRun(
            "mkdir -p /etc/mkinitcpio.d && "
            "printf 'ALL_config=\"/etc/mkinitcpio.conf\"\\nALL_kver=\"/boot/vmlinuz-" + suffix + "\"\\nPRESETS=(\"default\")\\ndefault_image=\"/boot/initramfs-" + suffix + ".img\"\\n' "
            "> /etc/mkinitcpio.d/" + kernel + ".preset",
            "mkinitcpio preset: " + kernel)) return false;
    }

    if (!chrootRun("mkinitcpio -P", "mkinitcpio build")) return false;

    std::cout << "\n==> Verifying presets..." << std::endl;
    system("arch-chroot /mnt sh -c 'for p in /etc/mkinitcpio.d/*.preset; do echo \"-- $p\"; cat \"$p\"; done'");
    std::cout << "\n==> Verifying initramfs was built..." << std::endl;
    system("ls -lh /mnt/boot/");
    system("mkdir -p /mnt/etc/fastfetch");
    system("cp /etc/fastfetch/naiados.txt /mnt/etc/fastfetch/naiados.txt");
    system("cp /etc/fastfetch/config.jsonc /mnt/etc/fastfetch/config.jsonc");
    // the icon behind LOGO=naiados in os-release (HyDE, GNOME/KDE about dialogs, login screens)
    system("mkdir -p /mnt/usr/share/pixmaps");
    system("cp /usr/share/pixmaps/naiados.png /mnt/usr/share/pixmaps/naiados.png");

    return true;
}

// "Existing": NaiadOS installs no bootloader and leaves the boot order alone;
// the system that owns the current bootloader adds NaiadOS itself.
void printExistingBootloaderSteps(const InstallConfig &config) {
    std::string uuid;
    FILE *pipe = popen(("blkid -s UUID -o value /dev/" + config.diskDevice + " 2>/dev/null").c_str(), "r");
    if (pipe) {
        char buffer[128];
        if (fgets(buffer, sizeof(buffer), pipe) != nullptr) uuid = buffer;
        pclose(pipe);
        while (!uuid.empty() && (uuid.back() == '\n' || uuid.back() == ' ')) uuid.pop_back();
    }

    std::cout << "\n==> No bootloader was installed (you chose to keep the existing one)." << std::endl;
    std::cout << "    NaiadOS root: /dev/" << config.diskDevice << "  UUID=" << uuid << std::endl;
    std::cout << "\n    Boot your other Linux system and add NaiadOS to its bootloader:" << std::endl;
    std::cout << "    GRUB:   sudo pacman -S os-prober   (or your distro's package manager)" << std::endl;
    std::cout << "            add GRUB_DISABLE_OS_PROBER=false to /etc/default/grub" << std::endl;
    std::cout << "            sudo grub-mkconfig -o /boot/grub/grub.cfg" << std::endl;
    std::cout << "    Others: add an entry by hand: kernel /boot/vmlinuz-naiados(-lts)," << std::endl;
    std::cout << "            initrd /boot/initramfs-naiados(-lts).img, options root=UUID=" << uuid << " rw" << std::endl;
    std::cout << "            (systemd-boot can only read kernels from the EFI partition: copy them there first)" << std::endl;
}

// Boot numbers of firmware entries whose loader path contains `path`
// (case-insensitive), e.g. "\\efi\\naiados\\".
std::vector<std::string> bootEntriesWithPath(const std::string &path) {
    std::vector<std::string> nums;
    FILE *pipe = popen("efibootmgr 2>/dev/null", "r");
    if (!pipe) return nums;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
        if (line.rfind("boot", 0) != 0 || line.size() < 10 || !isxdigit((unsigned char)line[4])) continue;
        if (line.find(path) != std::string::npos) nums.push_back(line.substr(4, 4));
    }
    pclose(pipe);
    return nums;
}

bool installBootloader(InstallConfig &config) {
    if (config.bootloader == "Existing") return true;

    showInstallProgress("Installing bootloader...");

    std::string diskName = config.diskDevice;

    if (diskName.find("nvme") != std::string::npos || diskName.find("mmcblk") != std::string::npos) {
    size_t pPos = diskName.rfind('p');
    if (pPos != std::string::npos && pPos > 0) {
        std::string afterP = diskName.substr(pPos + 1);
        bool allDigits = !afterP.empty() && std::all_of(afterP.begin(), afterP.end(), ::isdigit);
        if (allDigits) diskName = diskName.substr(0, pPos);
    }
}   
    else {
    size_t digitPos = diskName.find_first_of("0123456789");
    if (digitPos != std::string::npos) diskName = diskName.substr(0, digitPos);
}

    std::string disk = "/dev/" + diskName;

    std::cout << "\n==> Installing bootloader to: " << disk << std::endl;

    // grub 2.16 keeps an existing entry with the same path untouched, even when
    // it points at an EFI partition that no longer exists (reinstall with a new
    // ESP): the firmware then finds nothing to boot. Old NaiadOS entries go first.
    for (const auto &num : bootEntriesWithPath("\\efi\\naiados\\")) {
        std::cout << "==> Removing old NaiadOS boot entry Boot" << num << std::endl;
        system(("efibootmgr -q -b " + num + " -B").c_str());
    }

    std::string cmd1 = "arch-chroot /mnt grub-install --target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=NaiadOS " + disk;
    int result1 = system(cmd1.c_str());
    if (result1 != 0) {
        std::cout << "[ERROR] grub-install failed on " << disk << std::endl;
        return false;
    }

    // The standard fallback path, for firmware that ignores or loses boot entries
    // (HP). Never overwrites another system's fallback loader.
    if (system("test -e /mnt/boot/efi/EFI/BOOT/BOOTX64.EFI") != 0) {
        std::cout << "==> Installing fallback loader EFI/BOOT/BOOTX64.EFI" << std::endl;
        system("mkdir -p /mnt/boot/efi/EFI/BOOT && cp /mnt/boot/efi/EFI/NaiadOS/grubx64.efi /mnt/boot/efi/EFI/BOOT/BOOTX64.EFI");
    }

    if (bootEntriesWithPath("\\efi\\naiados\\").empty()) {
        std::cout << "[WARNING] No NaiadOS boot entry in the firmware (NVRAM full or read-only?)." << std::endl;
        std::cout << "          The fallback loader should still boot it; otherwise pick the disk in the boot menu." << std::endl;
    }

    std::string cmd2 = "arch-chroot /mnt grub-mkconfig -o /boot/grub/grub.cfg";
    int result2 = system(cmd2.c_str());
    if (result2 != 0) {
        std::cout << "[ERROR] grub-mkconfig failed" << std::endl;
        return false;
    }

    std::cout << "\n==> Verifying grub.cfg references correct kernel..." << std::endl;
    system("grep -i 'naiados\\|initramfs' /mnt/boot/grub/grub.cfg | head -10");

    return true;
}

bool unmountAll() {
    showInstallProgress("Unmounting partitions...");
    system("swapoff /mnt/swapfile 2>/dev/null");
    system("umount /mnt/boot/efi 2>/dev/null");
    system("umount -R /mnt 2>/dev/null");
    return true;
}

// Everything the install prints (ours and every command's) also goes to
// /var/log/naiadinstall.log; it is copied into the installed system at the end.
// tee is started by hand and never waited on: a daemon started during the
// install (gpg-agent from pacman-key) can keep the pipe open, and pclose()
// would hang until it exits.
struct InstallLog {
    int savedOut = -1, savedErr = -1;
    pid_t teePid = -1;

    void start() {
        std::cout.flush();
        fflush(stdout);
        int fds[2];
        if (pipe(fds) != 0) return;
        teePid = fork();
        if (teePid < 0) { close(fds[0]); close(fds[1]); return; }
        if (teePid == 0) {
            close(fds[1]);
            dup2(fds[0], 0);
            close(fds[0]);
            execlp("tee", "tee", "-a", "/var/log/naiadinstall.log", (char *)nullptr);
            _exit(127);
        }
        close(fds[0]);
        savedOut = dup(1);
        savedErr = dup(2);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
    }

    // ncurses needs the real terminal back before initTUI()
    void stop() {
        if (teePid < 0) return;
        std::cout.flush();
        fflush(stdout);
        fflush(stderr);
        dup2(savedOut, 1);
        dup2(savedErr, 2);
        close(savedOut);
        close(savedErr);
        waitpid(teePid, nullptr, WNOHANG);
        teePid = -1;
    }

    ~InstallLog() { stop(); }
};

// Windows counts as still installed while any NTFS/BitLocker data partition is
// left (recovery partitions don't count). A plain NTFS data drive also keeps
// the cleanup from being offered: better a leftover entry than a dead Windows.
bool windowsStillInstalled() {
    for (const auto &p : detectPartitions()) {
        if ((p.fstype == "ntfs" || p.fstype == "BitLocker") && p.parttype != WINRE_GUID) return true;
    }
    return false;
}

// Offered after the install replaced the last Windows partition: its boot
// entries, the EFI/Microsoft folder on the ESP NaiadOS uses, and the Windows-only
// partitions (reserved + recovery). Each group asks separately; default is no.
void cleanupWindowsLeftovers() {
    if (windowsStillInstalled()) return;

    std::vector<std::pair<std::string, std::string>> entries;   // boot number, label
    FILE *pipe = popen("efibootmgr 2>/dev/null", "r");
    if (pipe) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            std::string line(buffer);
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (line.rfind("Boot", 0) != 0 || line.size() < 10 || lower.find("\\efi\\microsoft\\") == std::string::npos) continue;
            size_t start = line.find_first_not_of("* ", 8);
            std::string label = (start == std::string::npos) ? "" : line.substr(start, line.find_first_of("\t\n", start) - start);
            entries.push_back({line.substr(4, 4), label});
        }
        pclose(pipe);
    }
    bool folder = (system("test -d /mnt/boot/efi/EFI/Microsoft") == 0);

    std::vector<PartInfo> parts;
    for (const auto &p : detectPartitions()) {
        if (p.parttype == MSR_GUID || p.parttype == WINRE_GUID) parts.push_back(p);
    }

    if (entries.empty() && !folder && parts.empty()) return;

    std::cout << "\n==> Windows is no longer installed on this machine, but it left things behind:" << std::endl;
    for (const auto &e : entries) std::cout << "    boot entry Boot" << e.first << " \"" << e.second << "\"" << std::endl;
    if (folder) std::cout << "    folder EFI/Microsoft on the EFI partition" << std::endl;

    if (!entries.empty() || folder) {
        std::cout << "\nRemove the Windows boot entries and the EFI/Microsoft folder? [y/N] " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "y" || answer == "Y") {
            for (const auto &e : entries) system(("efibootmgr -q -b " + e.first + " -B").c_str());
            if (folder) system("rm -rf /mnt/boot/efi/EFI/Microsoft");
            std::cout << "    removed." << std::endl;
        }
    }

    if (!parts.empty()) {
        std::cout << "\nWindows-only partitions (useless without Windows):" << std::endl;
        for (const auto &p : parts) std::cout << "    /dev/" << describePartition(p) << std::endl;
        std::cout << "Delete them? The space becomes free space. [y/N] " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        if (answer == "y" || answer == "Y") {
            for (const auto &p : parts) {
                std::string num;
                FILE *f = fopen(("/sys/class/block/" + p.name + "/partition").c_str(), "r");
                if (f) {
                    char buf[16];
                    if (fgets(buf, sizeof(buf), f)) num = buf;
                    fclose(f);
                }
                while (!num.empty() && (num.back() == '\n' || num.back() == ' ')) num.pop_back();
                if (num.empty() || p.disk.empty()) continue;
                // the disk is in use (NaiadOS is mounted), so no full re-read:
                // drop just this partition from the kernel's table
                std::string err;
                if (runWithError("sfdisk --no-reread --delete /dev/" + p.disk + " " + num, err) != 0) {
                    std::cout << "    [WARNING] could not delete /dev/" << p.name << ": " << err << std::endl;
                    continue;
                }
                system(("partx -d --nr " + num + " /dev/" + p.disk + " 2>/dev/null").c_str());
                std::cout << "    deleted /dev/" << p.name << std::endl;
            }
        }
    }
}

void runInstallation(InstallConfig &config) {
    InstallLog log;
    system("mkdir -p /var/log && : > /var/log/naiadinstall.log");
    log.start();

    std::cout << "\033[2J\033[H";
    std::cout << "========================================" << std::endl;
    std::cout << "      NaiadOS Installation Started      " << std::endl;
    std::cout << "========================================" << std::endl;

    if (!checkInternet()) {
        std::cout << "\n[ERROR] No internet connection detected!" << std::endl;
        std::cout << "Connect first (Wi-Fi: run 'nmtui' outside the installer), then try again." << std::endl;
        std::cout << "Press Enter to return." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    if (!prepareLiveSystem()) {
        std::cout << "\n[ERROR] Could not update the pacman keyring." << std::endl;
        std::cout << "Check your connection and try again." << std::endl;
        std::cout << "Press Enter to return." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    if (!checkDiskSpace()) {
        std::cout << "\n[ERROR] Not enough disk space! Minimum 8GB required." << std::endl;
        std::cout << "Press Enter to return." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    if (system("mountpoint -q /mnt") != 0) {
    std::cout << "\n[ERROR] /mnt is not mounted! Did disk setup complete?" << std::endl;
    std::cout << "Press Enter to return to live environment." << std::endl;
    std::cin.get();
    log.stop(); initTUI();
    return;
    }


    if (!installBasePackages(config)) {
        std::cout << "\n[ERROR] Base package installation failed!" << std::endl;
        std::cout << "Press Enter to return to live environment." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    if (!generateFstab()) {
        std::cout << "\n[ERROR] Failed to generate fstab!" << std::endl;
        std::cout << "Press Enter to return to live environment." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    if (!configureChrootSystem(config)) {
        std::cout << "\n[ERROR] System configuration failed!" << std::endl;
        std::cout << "Press Enter to return to live environment." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    cleanupWindowsLeftovers();

    if (!installBootloader(config)) {
        std::cout << "\n[ERROR] Bootloader installation failed!" << std::endl;
        std::cout << "Press Enter to return to live environment." << std::endl;
        std::cin.get();
        log.stop(); initTUI();
        return;
    }

    std::cout << "\n========================================" << std::endl;
    std::cout << "   NaiadOS installed successfully!      " << std::endl;
    std::cout << "========================================" << std::endl;
    if (config.bootloader == "Existing") printExistingBootloaderSteps(config);
    std::cout << "\nThe full install log is saved as /var/log/naiadinstall.log on the new system." << std::endl;
    std::cout << "\nPress Enter to continue." << std::endl;
    std::cin.get();
    log.stop();
    system("cp /var/log/naiadinstall.log /mnt/var/log/naiadinstall.log");

log.stop(); initTUI();

std::vector<std::string> options = {
    "Reboot system",
    "Stay in live environment",
    "chroot into installation for post-install configuration"
};

int selected = 0;
bool choosing = true;

while (choosing) {
    clear();
    mvprintw(LINES/2 - 4, (COLS/2) - 15, "Installation completed successfully!");
    mvprintw(LINES/2 - 2, (COLS/2) - 20, "What would you like to do next?");

    for (int i = 0; i < (int)options.size(); i++) {
        if (i == selected) attron(A_REVERSE);
        mvprintw(LINES/2 + i, (COLS/2) - 25, "%s", options[i].c_str());
        if (i == selected) attroff(A_REVERSE);
    }

    refresh();
    int key = getch();

    if (key == KEY_UP) {
        selected--;
        if (selected < 0) selected = (int)options.size() - 1;
    }
    else if (key == KEY_DOWN) {
        selected++;
        if (selected >= (int)options.size()) selected = 0;
    }
    else if (key == 10 || key == KEY_ENTER) {
        if (options[selected] == "Reboot system") {
            endwin();
            unmountAll();
            system("reboot");
            choosing = false;
        }
        else if (options[selected] == "Stay in live environment") {
            endwin();
            unmountAll();
            choosing = false;
        }
        else if (options[selected] == "chroot into installation for post-install configuration") {
            endwin();
            system("arch-chroot /mnt");
            unmountAll();
            initTUI();
            choosing = false;
        }
    }
}
}

bool validateConfig(InstallConfig &config) {
    if (config.bootloader == "Existing" && config.freshEsp) {
        showMessage({"Bootloader is set to 'keep existing', but the disk setup created a new, empty",
                     "EFI partition: nothing would boot NaiadOS. Set Bootloader to NaiadOS GRUB."});
        return false;
    }

    std::vector<std::string> missing;
    if (config.hostname.empty()) missing.push_back("Hostname");
    if (config.keyboardLayout.empty()) missing.push_back("Keyboard Layout");
    if (config.locale.empty()) missing.push_back("Locale");
    if (config.timezone.empty()) missing.push_back("Timezone");
    if (config.diskDevice.empty()) missing.push_back("Disk Configuration");
    if (config.rootPassword.empty()) missing.push_back("Root Password");
    if (config.users.empty()) missing.push_back("User Account");
    if (config.networkBackend.empty()) missing.push_back("Network Configuration");
    if (config.uiChoice.empty()) missing.push_back("Profile (UI)");

    if (missing.empty()) return true;
    clear();
    mvprintw(0, 2, "WARNING: The following items are not configured:");

    int row = 2;
    for (const auto &item : missing) {
        mvprintw(row++, 4, "- %s", item.c_str());
    }

    mvprintw(row + 1, 2, "Are you sure you want to proceed? [y/n]");
    refresh();

    int key = getch();
    return (key == 'y' || key == 'Y');
}



void showInstallSummary(InstallConfig &config) {
    clear();
    mvprintw(0, 2, "Install Summary");
    mvprintw(2, 2, "Review your choices before installing:");

    int row = 4;
    mvprintw(row++, 4, "Keyboard Layout: %s", config.keyboardLayout.empty() ? "Not set" : config.keyboardLayout.c_str());
    mvprintw(row++, 4, "Locale: %s", config.locale.empty() ? "Not set" : config.locale.c_str());
    mvprintw(row++, 4, "Disk: %s", config.diskDevice.empty() ? "Not set" : config.diskDevice.c_str());
    mvprintw(row++, 4, "Hostname: %s", config.hostname.empty() ? "Not set" : config.hostname.c_str());
    mvprintw(row++, 4, "Root Password: %s", config.rootPassword.empty() ? "Not set" : "********");
    if (config.users.empty()) {
        mvprintw(row++, 4, "Users: Not set");
    } else {
        for (const auto &user : config.users) {
            mvprintw(row++, 4, "User: %s (%s) password set", 
                user.username.c_str(),
                user.sudoUser ? "sudo" : "no sudo");
        }
    }
    mvprintw(row++, 4, "UI: %s", config.uiChoice.empty() ? "Not set" : config.uiChoice.c_str());
    mvprintw(row++, 4, "Audio: %s", config.audioSystem.empty() ? "Not set" : config.audioSystem.c_str());
    mvprintw(row++, 4, "Timezone: %s", config.timezone.empty() ? "Not set" : config.timezone.c_str());
    mvprintw(row++, 4, "Network: %s", config.networkBackend.empty() ? "Not set" : config.networkBackend.c_str());
    mvprintw(row++, 4, "Kernel: %s", config.kernel.c_str());
    mvprintw(row++, 4, "Bootloader: %s", config.bootloader == "GRUB" ? "NaiadOS GRUB" : "keep existing (none installed)");

    mvprintw(row + 1, 2, "Press Y to begin with installation, any other key to go back.");
    refresh();

    int key = getch();
    if (key == 'y' || key == 'Y') {
        if (validateConfig(config)) {
            endwin();
            runInstallation(config);
        }
    }
}



void showMainMenu(InstallConfig &config) {
    std::vector<std::string> options = {
        "Keyboard Layout",
        "Locale",
        "Disk Configuration",
        "Hostname",
        "User Account",
        "Root Password",
        "Profile",
        "Kernel",
        "Bootloader",
        "Timezone",
        "Network Configuration",
        "Install",
        "Abort"
    };

    int selected = 0;
    bool running = true;

    int height = LINES - 4;
    int leftWidth = COLS / 3;
    int rightWidth = COLS - leftWidth - 3;
    int starty = 2;

    WINDOW *leftPanel = newwin(height, leftWidth, starty, 1);
    WINDOW *rightPanel = newwin(height, rightWidth, starty, leftWidth + 2);

    while (running) {
        clear();
        refresh();
        werase(leftPanel);
        werase(rightPanel);
        wattron(leftPanel, COLOR_PAIR(1));
        box(leftPanel, 0, 0);
        mvwprintw(leftPanel, 0, 2, " NaiadOS Installer ");
        wattroff(leftPanel, COLOR_PAIR(1));
        wattron(rightPanel, COLOR_PAIR(1));
        box(rightPanel, 0, 0);
        mvwprintw(rightPanel, 0, 2, " Info ");
        wattroff(rightPanel, COLOR_PAIR(1));

        for (int i = 0; i < (int)options.size(); i++) {
            if (i == selected) {
                wattron(leftPanel, A_REVERSE);
            }

            bool isConfigured = false;
            if (options[i] == "Hostname") isConfigured = !config.hostname.empty();
            else if (options[i] == "Network Configuration") isConfigured = !config.networkBackend.empty();
            else if (options[i] == "Timezone") isConfigured = !config.timezone.empty();
            else if (options[i] == "Keyboard Layout") isConfigured = !config.keyboardLayout.empty();
            else if (options[i] == "Locale") isConfigured = !config.locale.empty();
            else if (options[i] == "Root Password") isConfigured = !config.rootPassword.empty();
            else if (options[i] == "User Account") isConfigured = !config.users.empty();     
            else if (options[i] == "Profile") isConfigured = !config.uiChoice.empty();
            else if (options[i] == "Disk Configuration") isConfigured = !config.diskDevice.empty();
            else if (options[i] == "Kernel") isConfigured = !config.kernel.empty();
            else if (options[i] == "Bootloader") isConfigured = !config.bootloader.empty();


            std::string marker = isConfigured ? "[x]" : "[ ]";
            mvwprintw(leftPanel, i + 2, 2, "%s %s", marker.c_str(), options[i].c_str());

            if (i == selected) {
                wattroff(leftPanel, A_REVERSE);
            }
        }


        std::string info = "Not configured yet";
        if (options[selected] == "Hostname" && !config.hostname.empty()) {
            info = "Hostname: " + config.hostname;
        }
        else if (options[selected] == "Network Configuration" && !config.networkBackend.empty()) {
        info = "Network: " + config.networkBackend;
        }
        else if (options[selected] == "Timezone" && !config.timezone.empty()) {
            info = "Timezone: " + config.timezone;
        }
        else if (options[selected] == "Keyboard Layout" && !config.keyboardLayout.empty()) {
            info = "Layout: " + config.keyboardLayout;
        }
        else if (options[selected] == "Locale" && !config.locale.empty()) {
            info = "Locale: " + config.locale;
        }
        else if (options[selected] == "Root Password" && !config.rootPassword.empty()) {
            info = "Root password is set";
        }
        else if (options[selected] == "Kernel") {
            info = "Kernel: " + config.kernel;
        }
        else if (options[selected] == "Bootloader") {
            info = "Bootloader: " + std::string(config.bootloader == "GRUB" ? "NaiadOS GRUB" : "keep existing");
        }
        else if (options[selected] == "User Account") {
        if (!config.users.empty()) {
            info = "";
            for (const auto &user : config.users) {
                info += user.username;
                info += user.sudoUser ? " [sudo]" : " [no sudo]";
                info += user.password.empty() ? " [no password]" : " [password set]";
                info += "  ";
            }
        }
        }
        else if (options[selected] == "Profile") {
            std::vector<std::string> parts;
            if (!config.uiChoice.empty()) parts.push_back(config.uiChoice);
            if (!config.audioSystem.empty()) parts.push_back(config.audioSystem);
            if (!parts.empty()) {
                info = "Profile: ";
                for (int j = 0; j < (int)parts.size(); j++) {
                    info += parts[j];
                    if (j < (int)parts.size() - 1) info += ", ";
                }
            }
        }
bool showingDiskDetails = (options[selected] == "Disk Configuration" && !config.diskDevice.empty());

    if (showingDiskDetails) {
        mvwprintw(rightPanel, 2, 2, "Disk: %s", config.diskDevice.c_str());

        std::string cmd = "lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINT --ascii 2>/dev/null";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            char buffer[256];
            int lineRow = 4;
            while (fgets(buffer, sizeof(buffer), pipe) != nullptr && lineRow < height - 2) {
                    std::string line(buffer);
                    if (!line.empty() && line.back() == '\n') {
                        line.pop_back();
                    }
                mvwprintw(rightPanel, lineRow, 2, "%s", line.c_str());
                lineRow++;
                }
                pclose(pipe);
            }
        }
    else {
        mvwprintw(rightPanel, 2, 2, "%s", info.c_str());
    }

        wrefresh(leftPanel);
        wrefresh(rightPanel);

        int key = getch();

        if (key == KEY_UP) {
            selected--;
            if (selected < 0) selected = (int)options.size() - 1;
        } 
        else if (key == KEY_DOWN) {
            selected++;
            if (selected >= (int)options.size()) selected = 0;
        } 
        else if (key == 10 || key == KEY_ENTER) {
            if (options[selected] == "Install") {
                showInstallSummary(config);
            }
            else if (options[selected] == "Network Configuration") {
                configureNetwork(config);
            }
            else if (options[selected] == "Timezone") {
                configureTimezone(config);
            }
            else if (options[selected] == "Disk Configuration") {
                configureDiskConfiguration(config);
            }
            else if (options[selected] == "Profile") {
                configureProfile(config);
            }
            else if (options[selected] == "Kernel") {
                configureKernel(config);
            }
            else if (options[selected] == "Bootloader") {
                configureBootloader(config);
            }
            else if (options[selected] == "User Account") {
                configureUserAccount(config);
            }
            else if (options[selected] == "Root Password") {
                configureRootPassword(config);
            }
            else if (options[selected] == "Locale") {
                configureLocales(config);
            }
            else if (options[selected] == "Keyboard Layout") {
                configureKeyboardLayout(config);
            }
            else if (options[selected] == "Hostname") {
                configureHostname(config);
            }
            else if (options[selected] == "Abort") {
                running = false;
            }
        }
    }

    delwin(leftPanel);
    delwin(rightPanel);
}

