#pragma once

#include <SDCardManager.h>

namespace EpubNotesIndex {

constexpr const char* kPath = "/.metadata/epub/notes_index.json";
// v5: index now also merges the custom /highlights quotes (and carries a "sig"
// of the /highlights dir so it rebuilds when fork-added quotes change).
constexpr int kVersion = 5;

inline void invalidate() {
  if (SdMan.exists(kPath)) {
    SdMan.remove(kPath);
  }
}

}  // namespace EpubNotesIndex
