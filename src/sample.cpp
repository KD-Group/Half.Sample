#include "commander/commander.hpp"
#include <clocale>


// 添加Windows头文件包含
#ifdef _WIN32

#include <windows.h>

#endif

int main() {
    // 设置本地化支持 UTF-8（Linux/Mac）
    std::setlocale(LC_ALL, "en_US.UTF-8");

    // Windows 特殊处理
#ifdef _WIN32
    // 使用SetConsoleOutputCP避免黑屏
    SetConsoleOutputCP(CP_UTF8);
#endif

    Commander::exec();
}
