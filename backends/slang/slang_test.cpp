// Simple test to check Slang integration and linking
#include <slang.h>
#include <slang-gfx.h>
#include <iostream>

int main() {
    slang::IGlobalSession* globalSession = nullptr;
    SlangResult result = slang::createGlobalSession(&globalSession);
    if (result == SLANG_OK && globalSession) {
        std::cout << "[SLANG TEST] Successfully created IGlobalSession. Compilation and linking are working!\n";
        globalSession->release();
    } else {
        std::cout << "[SLANG TEST] Failed to create IGlobalSession.\n";
        return 1;
    }
    std::cout << "Press Enter to exit...";
    std::cin.get();
    return 0;
}
