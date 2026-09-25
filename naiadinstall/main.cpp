#include "naiadinstall.h"

int main() {
    initTUI();

    InstallConfig config;
    showMainMenu(config);

    endTUI();
    return 0;
}