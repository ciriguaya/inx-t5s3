/**
 * @file HalStorage.cpp
 * @brief HalStorage facade over Inx's SDCardManager (see HalStorage.h).
 */

#include "HalStorage.h"

#include <SDCardManager.h>

#include <cstring>

HalStorage HalStorage::instance;

bool HalStorage::begin() { return SdMan.ready(); }

bool HalStorage::ready() const { return SdMan.ready(); }

std::vector<String> HalStorage::listFiles(const char* path, const int maxFiles) {
  return SdMan.listFiles(path, maxFiles);
}

String HalStorage::readFile(const char* path) { return SdMan.readFile(path); }

bool HalStorage::readFileToStream(const char* path, Print& out, const size_t chunkSize) {
  return SdMan.readFileToStream(path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, const size_t bufferSize, const size_t maxBytes) {
  return SdMan.readFileToBuffer(path, buffer, bufferSize, maxBytes);
}

bool HalStorage::writeFile(const char* path, const String& content) { return SdMan.writeFile(path, content); }

bool HalStorage::ensureDirectoryExists(const char* path) { return SdMan.ensureDirectoryExists(path); }

FsFile HalStorage::open(const char* path, const oflag_t oflag) { return SdMan.open(path, oflag); }

bool HalStorage::mkdir(const char* path, const bool pFlag) { return SdMan.mkdir(path, pFlag); }

bool HalStorage::exists(const char* path) { return SdMan.exists(path); }

bool HalStorage::remove(const char* path) { return SdMan.remove(path); }

bool HalStorage::rename(const char* oldPath, const char* newPath) { return SdMan.rename(oldPath, newPath); }

bool HalStorage::rmdir(const char* path) { return SdMan.rmdir(path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, FsFile& file) {
  return SdMan.openFileForRead(moduleName, path, file);
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, FsFile& file) {
  return SdMan.openFileForRead(moduleName, path, file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, FsFile& file) {
  return SdMan.openFileForRead(moduleName, path, file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, FsFile& file) {
  return SdMan.openFileForWrite(moduleName, path, file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, FsFile& file) {
  return SdMan.openFileForWrite(moduleName, path, file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, FsFile& file) {
  return SdMan.openFileForWrite(moduleName, path, file);
}

bool HalStorage::removeDir(const char* path) { return SdMan.removeDir(path); }
