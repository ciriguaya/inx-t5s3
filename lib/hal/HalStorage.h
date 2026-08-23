#pragma once

/**
 * @file HalStorage.h
 * @brief Minimal HalStorage-compatible facade over Inx's SDCardManager.
 *
 * Brought over so T5S3-fork code (highlight system, crash report dumper) can
 * use the same `Storage` API. Unlike the T5S3 fork's full HalStorage, this
 * shim does NOT alias SdFat's FsFile/HalFile types, so Inx's own code that
 * uses FsFile directly keeps working.
 */

#include <Print.h>
#include <SdFat.h>  // oflag_t, FsFile

#include <string>
#include <vector>

class HalStorage {
 public:
  HalStorage() = default;

  bool begin();
  bool ready() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  bool writeFile(const char* path, const String& content);
  bool ensureDirectoryExists(const char* path);

  FsFile open(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, FsFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForRead(const char* moduleName, const String& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, FsFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, FsFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

 private:
  static HalStorage instance;
};

#define Storage HalStorage::getInstance()
