#pragma once

#include "Logger.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <shlobj_core.h>
#include <string>
#include "Global.h"

void Logger::log(const char* format, ...) {
    std::string dir = Global::GetBRDRaomingPath();
    std::string path = dir + "\\logs.txt";

    std::filesystem::create_directories(dir);

    char buffer[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    std::cout << buffer << std::endl;

    std::ofstream file(path, std::ios::app);
    if (file.is_open()) {
        file << buffer << std::endl;
    }
}
