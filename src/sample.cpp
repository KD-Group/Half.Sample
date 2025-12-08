#include "commander/commander.hpp"

int main() {
    // 设置本地化支持 UTF-8（Linux/Mac）
    std::setlocale(LC_ALL, "en_US.UTF-8");

    // Windows 特殊处理
#ifdef _WIN32
    // 设置控制台代码页为 UTF-8
    system("chcp 65001 > nul");
#endif

    Commander::exec();
}
