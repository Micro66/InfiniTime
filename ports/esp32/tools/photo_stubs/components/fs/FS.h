#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
constexpr int LFS_O_RDONLY = 1, LFS_O_WRONLY = 2, LFS_O_CREAT = 4, LFS_O_TRUNC = 8;
struct lfs_file_t { std::string path; size_t position = 0; };
struct lfs_info { size_t size = 0; };
namespace Pinetime::Controllers {
class FS {
public:
  std::map<std::string, std::vector<uint8_t>> files;
  bool failWrite = false, failClose = false, failRename = false;
  int FileDelete(const char* p) { return files.erase(p) ? 0 : -1; }
  int Stat(const char* p, lfs_info* info) { if (!files.count(p)) return -1; info->size = files[p].size(); return 0; }
  int FileOpen(lfs_file_t* f, const char* p, int flags) {
    if (flags == LFS_O_RDONLY && !files.count(p)) return -1;
    *f = {p, 0}; if (flags & LFS_O_TRUNC) files[p].clear(); return 0;
  }
  int FileRead(lfs_file_t* f, uint8_t* out, uint32_t count) {
    count = std::min<size_t>(count, files[f->path].size() - f->position);
    memcpy(out, files[f->path].data() + f->position, count); f->position += count; return count;
  }
  int FileWrite(lfs_file_t* f, const uint8_t* bytes, uint32_t count) {
    if (failWrite) return -1;
    auto& value = files[f->path]; value.insert(value.end(), bytes, bytes + count); return count;
  }
  int FileClose(lfs_file_t*) { return failClose ? -1 : 0; }
  int Rename(const char* from, const char* to) {
    if (failRename) return -1;
    files[to] = files.at(from); files.erase(from); return 0;
  }
};
}
