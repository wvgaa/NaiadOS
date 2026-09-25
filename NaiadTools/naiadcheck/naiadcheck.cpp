#include "naiadcheck.h"

int main() {
    printBanner();
    std::cout << TEAL4 << "\n~ UPDATES\n" << RESET;
    std::cout << checkUpdates() << "\n";
    std::cout << checkAURUpdates() << "\n";
    
    std::cout << TEAL4 << "\n~ SYSTEM HEALTH\n" << RESET;
    std::cout << "  Internet        ~ " << checkInternet() << "\n";
    std::cout << "  Services        ~ " << checkFailedServices() << "\n";
    std::cout << "  Disk            ~ " << checkDiskHealth() << "\n";
    std::cout << "  Kernel          ~ " << checkKernel() << "\n";
    std::cout << "  GRUB Config     ~ " << checkGRUBconfig() << "\n";
    std::cout << "  GRUB Health     ~ " << checkGRUBhealth() << "\n";

    std::cout << TEAL4 << "\n~ BLOAT\n" << RESET;
    std::cout << "  Orphans         ~ " << checkOrphanedPackages() << "\n";
    std::cout << "  Cache           ~ " << checkCache() << "\n";
    std::cout << "  Space           ~ " << checkDiskSpace() << "\n";
    return 0;
}