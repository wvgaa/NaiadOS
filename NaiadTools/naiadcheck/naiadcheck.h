// INCLUDES
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <sstream>

// COLORS
inline const std::string TEAL1 = "\033[38;2;13;79;94m";
inline const std::string TEAL2 = "\033[38;2;14;107;122m";
inline const std::string TEAL3 = "\033[38;2;17;136;160m";
inline const std::string TEAL4 = "\033[38;2;21;168;200m";
inline const std::string TEAL5 = "\033[38;2;30;200;232m";
inline const std::string RESET  = "\033[0m";

// MAIN FUNCTION
inline std::string runCommand(const std::string& command) {
    FILE* pipe = popen(command.c_str(), "r");
    char buffer[256];
    std::string result = "";
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    return result;
}

//PRINTS
inline void printBanner() {
    std::cout << TEAL3 << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
    std::cout << TEAL5 << "███╗   ██╗ █████╗ ██╗ █████╗ ██████╗  ██████╗██╗  ██╗███████╗ ██████╗██╗  ██╗\n";
    std::cout << TEAL5 << "████╗  ██║██╔══██╗██║██╔══██╗██╔══██╗██╔════╝██║  ██║██╔════╝██╔════╝██║ ██╔╝\n";
    std::cout << TEAL4 << "██╔██╗ ██║███████║██║███████║██║  ██║██║     ███████║█████╗  ██║     █████╔╝ \n";
    std::cout << TEAL4 << "██║╚██╗██║██╔══██║██║██╔══██║██║  ██║██║     ██╔══██║██╔══╝  ██║     ██╔═██╗ \n";
    std::cout << TEAL3 << "██║ ╚████║██║  ██║██║██║  ██║██████╔╝╚██████╗██║  ██║███████╗╚██████╗██║  ██╗\n";
    std::cout << TEAL3 << "╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝╚═╝  ╚═╝╚═════╝  ╚═════╝╚═╝  ╚═╝╚══════╝ ╚═════╝╚═╝  ╚═╝\n";
    std::cout << TEAL3 << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
    std::cout << RESET;
}

//FUNCTIONS
inline std::string checkUpdates() {
    std::string output = runCommand("pacman -Qu");
    if (output.empty()) {
        return "✅ Up to date!";
    }
    else {
    int count = std::count(output.begin(), output.end(), '\n');
    std::string indented = "    " + output;
    size_t pos = 0;
    while ((pos = indented.find('\n', pos)) != std::string::npos) {
        indented.replace(pos, 1, "\n    ");
        pos += 5;
    }
    return "⚠️  " + std::to_string(count) + " pacman updates available:\n" + indented;
}
    }


inline std::string checkAURUpdates() {
    // Check for common AUR helpers
    if (system("command -v paru > /dev/null 2>&1") == 0) {
        std::string output = runCommand("paru -Qu --aur");
        if (output.empty()) return "✅ AUR up to date!";
        int count = std::count(output.begin(), output.end(), '\n');
        return "⚠️  " + std::to_string(count) + " AUR updates available";
    }
    else if (system("command -v yay > /dev/null 2>&1") == 0) {
        std::string output = runCommand("yay -Qu --aur");
        if (output.empty()) return "✅ AUR up to date!";
        int count = std::count(output.begin(), output.end(), '\n');
        return "⚠️  " + std::to_string(count) + " AUR updates available";
    }
    return "➖ No AUR helper found -- install paru or yay";
}

inline std::string checkInternet() {
    std::string output = runCommand("ping -c 1 8.8.8.8");
    if (output.empty()) {
        return "🚫 No internet connection!";
    }
    else {
        return "✅ Internet connected!";
    }
}

inline std::string checkFailedServices() {
    std::string output = runCommand("systemctl --failed --no-legend");
    if (output.empty()) {
        return "✅ All services healthy!";
    }
    else {
        int count = std::count(output.begin(), output.end(), '\n');
        return "⚠️  " + std::to_string(count) + " services failed!:\n" + output;
    }
}

inline std::string checkOrphanedPackages() {
    std::string output = runCommand("pacman -Qdtq");
    if (output.empty()) {
        return "✅ No Orphans found!";
    }
    else {
    int count = std::count(output.begin(), output.end(), '\n');
    std::string indented = "  " + output;
    size_t pos = 0;
    while ((pos = indented.find('\n', pos)) != std::string::npos) {
        indented.replace(pos, 1, "\n    ");
        pos += 5;
    }
    return "⚠️  " + std::to_string(count) + " orphans detected:\n" + indented;
    }
}

inline std::string smartAttrValue(const std::string &attrs, const std::string &attrName) {
    std::istringstream iss(attrs);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find(attrName) != std::string::npos) {
            std::istringstream ls(line);
            std::string tok;
            std::vector<std::string> toks;
            while (ls >> tok) toks.push_back(tok);
            if (toks.size() >= 10) return toks[9];
        }
    }
    return "";
}

inline std::string smartNvmeValue(const std::string &attrs, const std::string &label) {
    size_t pos = attrs.find(label);
    if (pos == std::string::npos) return "";
    std::istringstream ls(attrs.substr(pos + label.size()));
    std::string val;
    ls >> val;
    return val;
}

inline std::string diskFailureReasons(const std::string &attrs) {
    std::vector<std::pair<std::string, std::string>> criticalAttrs = {
        {"Reallocated_Sector_Ct", "reallocated sectors"},
        {"Current_Pending_Sector", "pending sectors"},
        {"Offline_Uncorrectable", "uncorrectable sectors"},
        {"Reported_Uncorrect", "uncorrectable errors reported"},
        {"UDMA_CRC_Error_Count", "interface CRC errors"}
    };

    std::vector<std::string> reasons;
    for (size_t i = 0; i < criticalAttrs.size(); i++) {
        std::string val = smartAttrValue(attrs, criticalAttrs[i].first);
        if (!val.empty() && val != "0") {
            reasons.push_back(criticalAttrs[i].second + " (" + val + ")");
        }
    }

    std::string critWarn = smartNvmeValue(attrs, "Critical Warning:");
    if (!critWarn.empty() && critWarn != "0x00") {
        reasons.push_back("NVMe critical warning flag " + critWarn);
    }
    std::string mediaErrors = smartNvmeValue(attrs, "Media and Data Integrity Errors:");
    if (!mediaErrors.empty() && mediaErrors != "0") {
        reasons.push_back("media/data integrity errors (" + mediaErrors + ")");
    }

    if (reasons.empty()) return "";
    std::string out = reasons[0];
    for (size_t i = 1; i < reasons.size(); i++) out += ", " + reasons[i];
    return out;
}

inline std::string checkDiskHealth() {
    if (system("command -v smartctl > /dev/null 2>&1") != 0) {
        return "➖ smartctl not found -- install smartmontools";
    }

    std::string disk = runCommand("lsblk -dno NAME,TYPE | grep disk | grep -v zram | head -1 | awk '{print \"/dev/\" $1}'");
    if (!disk.empty() && disk.back() == '\n') disk.pop_back();
    if (disk.empty()) {
        return "➖ No disk found to check";
    }

    std::string healthOut = runCommand("sudo smartctl -H " + disk + " 2>/dev/null");
    bool passed = healthOut.find("PASSED") != std::string::npos || healthOut.find("OK") != std::string::npos;

    std::string attrs = runCommand("sudo smartctl -A " + disk + " 2>/dev/null");

    std::string temp = smartAttrValue(attrs, "Temperature_Celsius");
    if (temp.empty()) temp = smartAttrValue(attrs, "Airflow_Temperature_Cel");
    if (temp.empty()) temp = smartNvmeValue(attrs, "Temperature:");
    if (!temp.empty()) temp += "°C";
    else temp = "n/a";

    std::string powerOnHours = smartAttrValue(attrs, "Power_On_Hours");
    if (powerOnHours.empty()) powerOnHours = smartNvmeValue(attrs, "Power On Hours:");
    if (!powerOnHours.empty() && powerOnHours != "n/a") powerOnHours += "h";
    else powerOnHours = "n/a";

    std::string reallocated = smartAttrValue(attrs, "Reallocated_Sector_Ct");

    std::string summary = passed ? "✅ Disk healthy" : "❌ Disk issue detected!";
    summary += " (" + disk + ") -- Temp: " + temp + ", Power-on: " + powerOnHours;

    if (!passed) {
        std::string reasons = diskFailureReasons(attrs);
        if (!reasons.empty()) summary += "\n    Reason: " + reasons;
    } else if (!reallocated.empty() && reallocated != "0") {
        summary += ", Reallocated sectors: " + reallocated + " ⚠️";
    }
    return summary;
}

inline std::string checkKernel() {
    std::string running = runCommand("uname -r");
    std::string installed = runCommand("ls /usr/lib/modules/ | grep -v lts | tail -1");
    if (running == installed) {
        return "✅ Kernel up to date";
    }
    else {
        return "⚠️  Reboot needed to use new kernel";
    }
}

inline std::string checkGRUBconfig() {
    std::string output = runCommand("test -f /boot/grub/grub.cfg && echo exists 2>/dev/null");
    if (output.find("exists") != std::string::npos) {
        return "✅ GRUB config found";
    } else {
        return "❌ GRUB config missing!";
    }
}

inline std::string checkGRUBhealth() {
    std::string output = runCommand("grub-script-check /boot/grub/grub.cfg 2>/dev/null");
    if (output.empty()) {
        return "✅ GRUB config healthy";
    }
    else {
        return "❌ GRUB errors found:\n" + output;
    }
}

inline std::string checkCache() {
    std::string output = runCommand("du -sh /var/cache/pacman/pkg/ 2>/dev/null");
return "⚠️  " + output.substr(0, output.find('\t')) + " cleanable with paru -Sc";
}

inline std::string checkDiskSpace() {
    std::string output = runCommand("df -h /");
    std::string secondLine = output.substr(output.find('\n') + 1);
    std::istringstream iss(secondLine);
    std::string token;
    int count = 0;
    std::string usage, avail, total;
    while (iss >> token) {
        count ++;
        if (count == 2) total = token;
        if (count == 4) avail = token;
        if (count == 5) usage = token;
    }
    int usageInt = std::stoi(usage.substr(0, usage.find('%')));
    if (usageInt > 85) {
        return "⚠️  Only " + avail + "/" + total + " free (" + usage + " used) -- clean up!";
    }
    else {
        return "✅ " + avail + "/" + total + " free (" + usage + " used)";
    }
}
