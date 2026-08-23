#include "HighlightPersistence.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

namespace {

// Replace characters that are invalid in FAT32 filenames.
char sanitizeFilenameChar(char c) {
  // FAT32 forbids: \ / : * ? " < > | and control chars
  if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
      static_cast<unsigned char>(c) < 32) {
    return '_';
  }
  return c;
}

// Extracts the string value of a `"key": "value"` pair inside one JSON object, honoring backslash
// escapes in the value. Returns "" when the key or a well-formed value is absent.
std::string extractQuotedValue(const char* objStart, const char* objEnd, const char* keyLiteral) {
  const char* kp = strstr(objStart, keyLiteral);
  if (!kp || kp >= objEnd) return "";
  const char* colon = strchr(kp, ':');
  if (!colon || colon >= objEnd) return "";
  const char* q = strchr(colon + 1, '"');
  if (!q || q >= objEnd) return "";
  const char* v = q + 1;
  const char* e = strchr(v, '"');
  while (e && e < objEnd && e > v && *(e - 1) == '\\') e = strchr(e + 1, '"');
  if (!e || e > objEnd) return "";
  return HighlightPersistence::unescapeJsonValue(std::string(v, e - v));
}

// Extracts the integer value of a `"key": N` pair inside one JSON object. Returns "" when absent.
std::string extractIntValue(const char* objStart, const char* objEnd, const char* keyLiteral) {
  const char* kp = strstr(objStart, keyLiteral);
  if (!kp || kp >= objEnd) return "";
  const char* colon = strchr(kp, ':');
  if (!colon || colon >= objEnd) return "";
  return std::to_string(atoi(colon + 1));
}

// Parses one CrossPoint-fork "*_pages.json" master file (the per-page highlight store the old reader
// wrote; each object has spine/page/indices/text/paragraph) into HighlightEntry rows. The entries
// carry no timestamp/sequence - callers assign those.
std::vector<HighlightEntry> loadPagesMasterFile(const std::string& filePath, const std::string& bookTitle) {
  std::vector<HighlightEntry> out;
  HalStorage& storage = HalStorage::getInstance();
  String content = storage.readFile(filePath.c_str());
  if (content.isEmpty()) {
    return out;
  }
  const char* p = content.c_str();
  while (*p) {
    const char* os = strchr(p, '{');
    if (!os) break;
    const char* oe = HighlightPersistence::findJsonObjectEnd(os);
    if (!oe) break;

    HighlightEntry e;
    e.bookTitle = bookTitle;
    e.chapter = extractIntValue(os, oe, "\"spine\"");
    e.selectedText = extractQuotedValue(os, oe, "\"text\"");
    e.paragraphText = extractQuotedValue(os, oe, "\"paragraph\"");
    // The fork's master files have no path; this reader patches it in on import (ensureQuoteBookPaths).
    e.bookPath = extractQuotedValue(os, oe, "\"path\"");
    if (!e.selectedText.empty()) {
      out.push_back(std::move(e));
    }
    p = oe + 1;
  }
  return out;
}

/** Case-insensitive, whitespace-collapsed copy used to compare stored highlight texts. */
std::string normalizeQuoteText(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool pendingSpace = false;
  for (const char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      pendingSpace = !out.empty();
      continue;
    }
    if (pendingSpace) {
      out += ' ';
      pendingSpace = false;
    }
    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

/**
 * Rewrites a JSON array of objects, dropping every object span for which `shouldDrop` returns true.
 * Non-matching object bytes are preserved verbatim (so the fork's indices/page/paragraph survive); the
 * array separators are normalized. Returns true if any object was dropped.
 */
bool rewriteJsonArrayDroppingMatching(const String& content,
                                      const std::function<bool(const char* objStart, const char* objEnd)>& shouldDrop,
                                      std::string& out) {
  out.clear();
  if (content.isEmpty() || content.indexOf('{') < 0) {
    return false;
  }
  std::vector<std::pair<const char*, size_t>> kept;
  const char* cur = content.c_str();
  int dropped = 0;
  while (const char* os = strchr(cur, '{')) {
    const char* oe = HighlightPersistence::findJsonObjectEnd(os);
    if (!oe) break;
    if (shouldDrop(os, oe)) {
      ++dropped;
    } else {
      kept.emplace_back(os, static_cast<size_t>(oe - os + 1));
    }
    cur = oe + 1;
  }
  if (dropped == 0) {
    return false;
  }
  out = "[";
  for (size_t i = 0; i < kept.size(); ++i) {
    out += i ? ",\n" : "\n";
    out.append(kept[i].first, kept[i].second);
  }
  out += "\n]\n";
  return true;
}

}  // namespace

std::string HighlightPersistence::sanitizeFilename(const std::string& title) {
  std::string result = title;
  for (auto& c : result) {
    c = sanitizeFilenameChar(c);
  }
  // Trim leading/trailing whitespace and dots
  while (!result.empty() && (result.front() == ' ' || result.front() == '.')) {
    result.erase(result.begin());
  }
  while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
    result.pop_back();
  }
  if (result.empty()) {
    result = "unknown";
  }
  // Limit filename length
  if (result.size() > 60) {
    result.resize(60);
  }
  return result;
}

std::string HighlightPersistence::getHighlightsDir() { return "/highlights"; }

std::string HighlightPersistence::getFilePath(const std::string& bookTitle) {
  return getHighlightsDir() + "/" + sanitizeFilename(bookTitle) + ".json";
}

std::string HighlightPersistence::escapeJsonString(const std::string& str) {
  std::string result;
  result.reserve(str.size() + 4);
  for (char c : str) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          result += buf;
        } else {
          result += c;
        }
        break;
    }
  }
  return result;
}

std::string HighlightPersistence::buildHighlightJson(const HighlightEntry& entry) {
  std::string json;
  json += "{\n";
  json += "  \"book\": \"";
  json += escapeJsonString(entry.bookTitle);
  json += "\",\n";
  json += "  \"path\": \"";
  json += escapeJsonString(entry.bookPath);
  json += "\",\n";
  json += "  \"chapter\": \"";
  json += escapeJsonString(entry.chapter);
  json += "\",\n";
  json += "  \"text\": \"";
  json += escapeJsonString(entry.selectedText);
  json += "\",\n";
  json += "  \"paragraph\": \"";
  json += escapeJsonString(entry.paragraphText);
  json += "\",\n";
  json += "  \"timestamp\": ";
  json += std::to_string(entry.timestamp);
  json += ",\n";
  json += "  \"sequence\": ";
  json += std::to_string(entry.sequence);
  json += "\n}";
  return json;
}

unsigned long HighlightPersistence::getNextSequence() {
  unsigned long seq = readSequence();
  seq++;
  setSequence(seq);
  return seq;
}

unsigned long HighlightPersistence::readSequence() {
  HalStorage& storage = HalStorage::getInstance();
  storage.ensureDirectoryExists(getHighlightsDir().c_str());

  std::string seqPath = getHighlightsDir() + "/.sequence";
  unsigned long seq = 0;

  String existing = storage.readFile(seqPath.c_str());
  if (!existing.isEmpty()) {
    const char* p = existing.c_str();
    while (*p == ' ' || *p == '\n' || *p == '\r') p++;
    seq = strtoul(p, nullptr, 10);
  }
  return seq;
}

void HighlightPersistence::setSequence(unsigned long value) {
  HalStorage& storage = HalStorage::getInstance();
  storage.ensureDirectoryExists(getHighlightsDir().c_str());
  String newSeq(value);
  storage.writeFile((getHighlightsDir() + "/.sequence").c_str(), newSeq);
}

const char* HighlightPersistence::findJsonObjectEnd(const char* objStart) {
  if (!objStart || *objStart != '{') return nullptr;

  bool inString = false;
  bool escaped = false;
  for (const char* p = objStart + 1; *p; p++) {
    if (inString) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (*p == '\\') {
        escaped = true;
        continue;
      }
      if (*p == '"') inString = false;
      continue;
    }
    if (*p == '"') {
      inString = true;
      continue;
    }
    if (*p == '}') return p;
  }
  return nullptr;
}

std::string HighlightPersistence::unescapeJsonValue(const std::string& value) {
  if (value.find('\\') == std::string::npos) return value;
  std::string result;
  result.reserve(value.size());
  for (size_t i = 0; i < value.size(); i++) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const char next = value[i + 1];
      switch (next) {
        case '"':
          result += '"';
          i++;
          continue;
        case '\\':
          result += '\\';
          i++;
          continue;
        case 'n':
          result += '\n';
          i++;
          continue;
        case 'r':
          result += '\r';
          i++;
          continue;
        case 't':
          result += '\t';
          i++;
          continue;
        case 'u': {
          // \uXXXX -> single byte (only emitted for control chars < 0x20).
          bool ok = i + 5 < value.size();
          unsigned int code = 0;
          for (int k = 1; ok && k <= 4; k++) {
            const char c = value[i + 1 + k];
            code <<= 4;
            if (c >= '0' && c <= '9')
              code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
              code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
              code |= static_cast<unsigned>(c - 'A' + 10);
            else
              ok = false;
          }
          if (ok && code < 0x100) {
            result += static_cast<char>(code);
            i += 5;
            continue;
          }
          break;
        }
        default:
          break;
      }
    }
    result += value[i];
  }
  return result;
}

bool HighlightPersistence::saveHighlight(const std::string& bookTitle, const std::string& bookPath,
                                          const std::string& chapter, const std::string& selectedText,
                                          const std::string& paragraphText) {
  if (bookTitle.empty() || selectedText.empty()) {
    LOG_ERR("HLP", "Cannot save highlight: empty title or text");
    return false;
  }

  HalStorage& storage = HalStorage::getInstance();

  // Ensure /highlights/ directory exists
  if (!storage.ensureDirectoryExists(getHighlightsDir().c_str())) {
    LOG_ERR("HLP", "Failed to create highlights directory");
    return false;
  }

  HighlightEntry entry;
  entry.bookTitle = bookTitle;
  entry.bookPath = bookPath;
  entry.chapter = chapter;
  entry.selectedText = selectedText;
  entry.paragraphText = paragraphText;
  entry.timestamp = millis();
  entry.sequence = getNextSequence();

  // Build the JSON entry
  std::string jsonLine = buildHighlightJson(entry);
  std::string filePath = getFilePath(bookTitle);
  LOG_DBG("HLP", "Saving highlight to: %s", filePath.c_str());

  // Read existing content via the higher-level readFile API
  String existingContent = storage.readFile(filePath.c_str());

  // Build the new file content as a JSON array
  String newContent;
  if (existingContent.isEmpty() || existingContent.indexOf('[') < 0) {
    // Start a new JSON array
    newContent = "[\n" + String(jsonLine.c_str()) + "\n]\n";
  } else {
    // Append to existing array
    int closePos = existingContent.lastIndexOf(']');
    if (closePos >= 0) {
      String before = existingContent.substring(0, closePos);
      // Add comma if there are existing entries
      if (closePos > 0 && existingContent[closePos - 1] != '[' && existingContent[closePos - 1] != '\n') {
        before += ",\n";
      } else if (closePos > 0 && existingContent[closePos - 1] == '\n') {
        before += ",";
      }
      newContent = before + String(jsonLine.c_str()) + "\n]\n";
    } else {
      // Fallback: just append
      newContent = existingContent + "\n" + String(jsonLine.c_str()) + "\n";
    }
  }

  bool result = storage.writeFile(filePath.c_str(), newContent);
  if (!result) {
    LOG_ERR("HLP", "Failed to write highlight file: %s", filePath.c_str());
  }
  return result;
}

std::vector<HighlightEntry> HighlightPersistence::loadAllHighlights() {
  std::vector<HighlightEntry> derived;   // current per-book .json (has timestamp + sequence)
  std::vector<HighlightEntry> master;    // CrossPoint fork's *_pages.json store (no sequence)

  HalStorage& storage = HalStorage::getInstance();
  if (!storage.ensureDirectoryExists(getHighlightsDir().c_str())) {
    return {};
  }

  std::vector<String> files = storage.listFiles(getHighlightsDir().c_str(), 1000);
  for (const String& name : files) {
    if (!name.endsWith(".json")) {
      continue;
    }
    const std::string fname = name.c_str();
    if (fname.size() >= 11 && fname.compare(fname.size() - 11, 11, "_pages.json") == 0) {
      const std::string stem = fname.substr(0, fname.size() - 11);
      std::vector<HighlightEntry> entries =
          loadPagesMasterFile(getHighlightsDir() + "/" + fname, stem);
      master.insert(master.end(), entries.begin(), entries.end());
      continue;
    }
    std::string stem = fname;
    stem.resize(stem.size() - 5);  // strip ".json"
    std::vector<HighlightEntry> entries = loadHighlights(stem);
    derived.insert(derived.end(), entries.begin(), entries.end());
  }

  // Merge with dedup: the derived .json files are rebuilt from *_pages.json, so the same quote
  // appears in both. Keep the derived copy (it has the real timestamp/sequence) and use the master
  // as the fallback so quotes survive even when the derived files are stale or missing.
  std::vector<HighlightEntry> all;
  std::vector<std::string> seenKeys;
  auto dedupKey = [](const HighlightEntry& e) {
    std::string key = sanitizeFilename(e.bookTitle);
    key += '|';
    key += e.chapter;
    key += '|';
    key += e.selectedText;
    return key;
  };
  for (const HighlightEntry& e : derived) {
    const std::string key = dedupKey(e);
    if (std::find(seenKeys.begin(), seenKeys.end(), key) == seenKeys.end()) {
      seenKeys.push_back(key);
      all.push_back(e);
    }
  }
  for (const HighlightEntry& e : master) {
    const std::string key = dedupKey(e);
    if (std::find(seenKeys.begin(), seenKeys.end(), key) == seenKeys.end()) {
      seenKeys.push_back(key);
      all.push_back(e);
    }
  }

  // Drop entries that can't be displayed or opened (artifacts of an empty book title - e.g. a stray
  // "_pages.json" written when the fork saved a highlight for a title-less book with no fallback).
  all.erase(std::remove_if(all.begin(), all.end(),
                           [](const HighlightEntry& e) { return e.bookTitle.empty() || e.selectedText.empty(); }),
            all.end());

  // Keep the persisted sequence counter ahead of every stored entry so newly saved highlights sort
  // after the imported old ones instead of landing before them (the fork's counter file was not
  // carried over, so a fresh card would otherwise start new quotes at sequence 1 behind 400+).
  unsigned long stored = readSequence();
  unsigned long maxSeq = stored;
  for (const HighlightEntry& e : all) {
    if (e.sequence > maxSeq) maxSeq = e.sequence;
  }
  if (maxSeq > stored) {
    setSequence(maxSeq);
  }
  return all;
}

bool HighlightPersistence::loadLatestHighlight(HighlightEntry& out) {
  std::vector<HighlightEntry> all = loadAllHighlights();
  if (all.empty()) {
    return false;
  }
  const HighlightEntry* latest = &all[0];
  for (const HighlightEntry& e : all) {
    if (e.sequence > latest->sequence) {
      latest = &e;
    }
  }
  out = *latest;
  return true;
}

std::string HighlightPersistence::defaultTitleForPath(const std::string& path) {
  std::string stem;
  const size_t slash = path.find_last_of('/');
  stem = (slash == std::string::npos) ? path : path.substr(slash + 1);
  const size_t dot = stem.find_last_of('.');
  if (dot != std::string::npos) {
    stem.resize(dot);
  }
  // "Popol Vuh(1)" -> "Popol Vuh" so title-less books share the same /highlights identity the
  // fork's *_pages.json master uses for them.
  const size_t open = stem.find_last_of('(');
  if (open != std::string::npos && !stem.empty() && stem.back() == ')') {
    stem.resize(open);
    while (!stem.empty() && (stem.back() == ' ' || stem.back() == '-')) {
      stem.pop_back();
    }
  }
  const std::string s = sanitizeFilename(stem);
  return (s.empty() || s == "unknown") ? std::string() : s;
}

std::vector<std::string> HighlightPersistence::bookTitleCandidates(const std::string& title, const std::string& path) {
  std::vector<std::string> candidates;
  auto addCandidate = [&](const std::string& raw) {
    std::string s = sanitizeFilename(raw);
    if (s.empty() || s == "unknown") {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), s) == candidates.end()) {
      candidates.push_back(std::move(s));
    }
  };
  addCandidate(title);
  addCandidate(defaultTitleForPath(path));
  {
    std::string stripped = defaultTitleForPath(path);
    const size_t open = stripped.find_last_of('(');
    if (open != std::string::npos && stripped.back() == ')') {
      stripped.resize(open);
      while (!stripped.empty() && (stripped.back() == ' ' || stripped.back() == '-')) {
        stripped.pop_back();
      }
      addCandidate(stripped);
    }
  }
  return candidates;
}

bool HighlightPersistence::deleteHighlight(const std::vector<std::string>& titleCandidates,
                                           const std::string& chapterTitle, const std::string& spineIndex,
                                           const std::string& text) {
  if (titleCandidates.empty() || text.empty()) {
    return false;
  }
  HalStorage& storage = HalStorage::getInstance();
  const std::string target = normalizeQuoteText(text);
  bool changed = false;

  for (const std::string& cand : titleCandidates) {
    // Derived /highlights/<title>.json: match by text + chapter (the chapter title string).
    {
      const std::string filePath = getHighlightsDir() + "/" + cand + ".json";
      if (storage.exists(filePath.c_str())) {
        String content = storage.readFile(filePath.c_str());
        std::string out;
        const bool dropped = rewriteJsonArrayDroppingMatching(
            content,
            [&](const char* os, const char* oe) {
              if (normalizeQuoteText(extractQuotedValue(os, oe, "\"text\"")) != target) {
                return false;
              }
              if (!chapterTitle.empty() && extractQuotedValue(os, oe, "\"chapter\"") != chapterTitle) {
                return false;
              }
              return true;
            },
            out);
        if (dropped) {
          changed = true;
          storage.writeFile(filePath.c_str(), String(out.c_str()));
        }
      }
    }
    // Fork master <title>_pages.json: match by text + spine (the spine number).
    {
      const std::string masterPath = getHighlightsDir() + "/" + cand + "_pages.json";
      if (storage.exists(masterPath.c_str())) {
        String content = storage.readFile(masterPath.c_str());
        std::string out;
        const bool dropped = rewriteJsonArrayDroppingMatching(
            content,
            [&](const char* os, const char* oe) {
              if (normalizeQuoteText(extractQuotedValue(os, oe, "\"text\"")) != target) {
                return false;
              }
              if (!spineIndex.empty() && extractIntValue(os, oe, "\"spine\"") != spineIndex) {
                return false;
              }
              return true;
            },
            out);
        if (dropped) {
          changed = true;
          storage.writeFile(masterPath.c_str(), String(out.c_str()));
        }
      }
    }
  }
  return changed;
}

void HighlightPersistence::deleteFromPagesMaster(const std::string& stem, const std::string& text) {
  if (stem.empty() || text.empty()) {
    return;
  }
  HalStorage& storage = HalStorage::getInstance();
  const std::string masterPath = getHighlightsDir() + "/" + sanitizeFilename(stem) + "_pages.json";
  if (!storage.exists(masterPath.c_str())) {
    return;
  }
  const std::string target = normalizeQuoteText(text);
  String content = storage.readFile(masterPath.c_str());
  std::string out;
  const bool dropped = rewriteJsonArrayDroppingMatching(
      content,
      [&](const char* os, const char* oe) {
        return normalizeQuoteText(extractQuotedValue(os, oe, "\"text\"")) == target;
      },
      out);
  if (dropped) {
    storage.writeFile(masterPath.c_str(), String(out.c_str()));
  }
}

void HighlightPersistence::ensureQuoteBookPaths(const std::vector<std::string>& titleCandidates,
                                                const std::string& path) {
  if (titleCandidates.empty() || path.empty()) {
    return;
  }
  HalStorage& storage = HalStorage::getInstance();
  const std::string escapedPath = escapeJsonString(path);

  auto patchFile = [&](const std::string& filePath) {
    if (!storage.exists(filePath.c_str())) {
      return;
    }
    String content = storage.readFile(filePath.c_str());
    if (content.isEmpty()) {
      return;
    }
    std::string out;
    bool changed = false;
    const char* p = content.c_str();
    while (const char* os = strchr(p, '{')) {
      const char* oe = findJsonObjectEnd(os);
      if (!oe) {
        break;
      }
      out.append(p, static_cast<size_t>(os - p));

      // Locate the "path" key's value span inside this object, if present.
      const char* keyp = strstr(os, "\"path\"");
      const char* colon = keyp && keyp < oe ? strchr(keyp, ':') : nullptr;
      const char* valStart = colon && colon < oe ? strchr(colon + 1, '"') : nullptr;
      const char* valEnd = valStart && valStart < oe ? strchr(valStart + 1, '"') : nullptr;
      while (valEnd && valEnd < oe && valEnd > valStart && *(valEnd - 1) == '\\') {
        valEnd = strchr(valEnd + 1, '"');
      }
      const bool hasPath = valEnd && valEnd < oe && valStart && valEnd > valStart;
      if (hasPath) {
        const std::string existing(valStart + 1, static_cast<size_t>(valEnd - valStart - 1));
        if (existing == escapedPath) {
          out.append(os, static_cast<size_t>(oe - os + 1));  // unchanged
        } else {
          out.append(os, static_cast<size_t>(valStart + 1 - os));
          out += escapedPath;
          out.append(valEnd, static_cast<size_t>(oe - valEnd + 1));
          changed = true;
        }
      } else {
        // The fork's master objects carry no path - insert one before the closing brace.
        out.append(os, static_cast<size_t>(oe - os));
        out += ",\"path\":\"";
        out += escapedPath;
        out += "\"}";
        changed = true;
      }
      p = oe + 1;
    }
    out.append(p);
    if (changed) {
      storage.writeFile(filePath.c_str(), String(out.c_str()));
    }
  };

  for (const std::string& cand : titleCandidates) {
    patchFile(getHighlightsDir() + "/" + cand + ".json");
    patchFile(getHighlightsDir() + "/" + cand + "_pages.json");
  }
}

std::vector<HighlightEntry> HighlightPersistence::loadHighlights(const std::string& bookTitle) {
  std::vector<HighlightEntry> entries;

  HalStorage& storage = HalStorage::getInstance();
  std::string filePath = getFilePath(bookTitle);

  String content = storage.readFile(filePath.c_str());
  if (content.isEmpty()) {
    return entries;
  }

  // Simple JSON array parser for our specific format.
  const char* p = content.c_str();
  while (*p) {
    // Skip to opening brace of an object
    p = strchr(p, '{');
    if (!p) break;

    HighlightEntry entry;
    const char* end = strchr(p, '}');
    if (!end) break;

    // Parse key-value pairs using simple string scanning
    auto extractString = [&](const char* key) -> std::string {
      const char* kp = strstr(p, key);
      if (!kp || kp > end) return "";
      kp = strchr(kp, '"');
      if (!kp || kp > end) return "";
      kp++;  // skip opening quote of key
      kp = strchr(kp, '"');
      if (!kp || kp > end) return "";
      kp++;  // skip closing quote of key
      kp = strchr(kp, '"');
      if (!kp || kp > end) return "";
      const char* valStart = kp + 1;  // skip opening quote of value
      const char* valEnd = strchr(valStart, '"');
      if (!valEnd || valEnd > end) return "";
      return std::string(valStart, valEnd - valStart);
    };

    auto extractNumber = [&](const char* key) -> unsigned long {
      const char* kp = strstr(p, key);
      if (!kp || kp > end) return 0;
      kp = strchr(kp, ':');
      if (!kp || kp > end) return 0;
      kp++;  // skip colon
      while (*kp == ' ' && kp < end) kp++;
      return strtoul(kp, nullptr, 10);
    };

    entry.bookTitle = extractString("\"book\"");
    entry.bookPath = extractString("\"path\"");
    entry.chapter = extractString("\"chapter\"");
    entry.selectedText = extractString("\"text\"");
    entry.paragraphText = extractString("\"paragraph\"");
    entry.timestamp = extractNumber("\"timestamp\"");
    entry.sequence = extractNumber("\"sequence\"");

    if (!entry.selectedText.empty()) {
      entries.push_back(std::move(entry));
    }

    p = end + 1;
  }

  return entries;
}
