#pragma once
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include "VansLog.h"


#if defined _WIN32
#include <windows.h>
#include <locale>
#include <codecvt>
#elif defined __linux

#endif

std::wstring stringToWString(const std::string& str) 
{
    // Determine the required buffer size
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    // Create a buffer to hold the wide string
    std::wstring wstrTo(size_needed, 0);
    // Perform the conversion
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::vector<std::string> GetFilesInFolder(const std::string& directory)
{
#if defined _WIN32
    std::vector<std::string> files;
    std::string searchPath = directory + "\\*.*";
    std::wstring wsearchPath = stringToWString(searchPath);
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(wsearchPath.c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE) 
    {
        VANS_LOG_ERROR("FindFirstFile failed (" << GetLastError() << ")");
        return files;
    }

    //用于从宽字符转换到string
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

    do {
        // Convert the wstring to a string
        const std::string fileOrDir = converter.to_bytes(findFileData.cFileName);

        //过滤掉dir只获取文件
        bool isDir = findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;

        if (!isDir && fileOrDir != "." && fileOrDir != "..")
        {
            files.push_back(fileOrDir);
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);
    return files;
#elif defined __linux
    std::vector<std::string> files;
    DIR* dir = opendir(directory.c_str());
    if (dir == nullptr) {
        VANS_LOG_ERROR("Failed to open directory: " << directory);
        return files;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fileOrDir = entry->d_name;
        if (fileOrDir != "." && fileOrDir != "..") {
            files.push_back(fileOrDir);
        }
    }

    closedir(dir);
    return files;
#endif
}


std::string GetFileExtension(const std::string& directory)
{
    //获取当地文件名的后缀
    size_t pos = directory.find_last_of('.');
    if (pos == std::string::npos)
    {
		return "";
	}
    return directory.substr(pos + 1);
}

std::string GetFileWithoutExtension(const std::string& filePath)
{
    // Find the position of the last dot
    size_t lastDot = filePath.find_last_of('.');
    // Find the position of the last slash or backslash
    size_t lastSlash = filePath.find_last_of("/\\");

    // If no dot is found or the dot is before the last slash, return the whole file name
    if (lastDot == std::string::npos || (lastSlash != std::string::npos && lastDot < lastSlash)) 
    {
        return filePath.substr(lastSlash + 1);
    }

    // Extract the file name without the extension
    return filePath.substr(lastSlash + 1, lastDot - lastSlash - 1);
}

void ReadFile(const std::string& file_path, std::vector<unsigned char>& result)
{
    // Open the file in binary mode
    std::ifstream file(file_path, std::ios::binary);
    if (!file) 
    {
        VANS_LOG_ERROR("file open failed" << file_path);
        return;
    }

    // Determine the file size
    file.seekg(0, std::ios::end);
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read the file contents into a vector
    result.resize(fileSize);
    if (!file.read(reinterpret_cast<char*>(result.data()), fileSize))
    {
        VANS_LOG_ERROR("read open failed" << file_path);
        return;
    }
}

bool CheckFolderExist(const std::string& check_string)
{
    DWORD attribs = GetFileAttributesA(check_string.c_str());
    if (attribs == INVALID_FILE_ATTRIBUTES)
    {
        return false;
    }
    return (attribs & FILE_ATTRIBUTE_DIRECTORY);
}

bool SwitchToDeferredShaderPath(std::string& string)
{
    auto temp_string = string + "/Deferred";
    //检查是否含有这个路径，如果没有就不转换，并输出
    if (!CheckFolderExist(temp_string))
    {
        return false;
    }
    string = temp_string;
    return true;
}
