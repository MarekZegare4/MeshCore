#pragma once

// Native-disk-backed FILESYSTEM/File shim for the sim build's DataStore
// persistence (examples/companion_radio/DataStore.cpp / .h,
// src/helpers/IdentityStore.h/.cpp). Modelled on the ESP32 fs::FS/fs::File
// shape (DataStore.cpp's own "#else" fallback branch already calls
// fs->open(path, "r"/"w", create_bool) -- matching that signature here
// means SIM_PLATFORM falls straight into those existing branches with no
// DataStore.cpp/IdentityStore.cpp edits needed beyond the FILESYSTEM/File
// typedefs in IdentityStore.h), but backed by plain fopen/fread/fwrite
// under a local directory (./sim_data/ by default) so NodePrefs/contacts/
// identity genuinely round-trip across process restarts.

#include <Stream.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

class SimFile : public Stream {
  friend class SimFS;
  FILE* _fp = nullptr;
  DIR* _dir = nullptr;
  bool _is_dir = false;
  std::string _dirpath;   // full on-disk path, directory mode only
  std::string _name;      // basename
  size_t _size = 0;

  SimFile(FILE* fp, const std::string& name, size_t size) : _fp(fp), _name(name), _size(size) { }
  SimFile(DIR* dir, const std::string& dirpath, const std::string& name)
    : _dir(dir), _is_dir(true), _dirpath(dirpath), _name(name) { }

public:
  SimFile() { }

  operator bool() const { return _fp != nullptr || _dir != nullptr; }

  bool isDirectory() { return _is_dir; }
  const char* name() { return _name.c_str(); }
  size_t size() { return _size; }

  // directory iteration (MyMesh.cpp's CLI-rescue "ls" debug command)
  SimFile openNextFile() {
    if (!_dir) return SimFile();
    struct dirent* ent;
    while ((ent = readdir(_dir)) != nullptr) {
      std::string nm = ent->d_name;
      if (nm == "." || nm == "..") continue;
      std::string full = _dirpath + "/" + nm;
      struct stat st;
      if (stat(full.c_str(), &st) != 0) continue;
      SimFile f;
      f._name = nm;
      f._is_dir = S_ISDIR(st.st_mode);
      f._size = (size_t)st.st_size;
      return f;
    }
    return SimFile();
  }

  size_t read(uint8_t* buf, size_t len) {
    if (!_fp) return 0;
    return fread(buf, 1, len, _fp);
  }
  int read() override {
    if (!_fp) return -1;
    int c = fgetc(_fp);
    return c;
  }
  int peek() override {
    if (!_fp) return -1;
    int c = fgetc(_fp);
    if (c != EOF) ungetc(c, _fp);
    return c;
  }
  int available() override {
    if (!_fp) return 0;
    long cur = ftell(_fp);
    if (cur < 0) return 0;
    return (int)((long)_size - cur);
  }

  size_t write(uint8_t b) override { return _fp ? fwrite(&b, 1, 1, _fp) : 0; }
  size_t write(const uint8_t* buf, size_t len) override {
    return _fp ? fwrite(buf, 1, len, _fp) : 0;
  }

  bool seek(uint32_t pos) {
    if (!_fp) return false;
    return fseek(_fp, (long)pos, SEEK_SET) == 0;
  }

  void close() {
    if (_fp) { fclose(_fp); _fp = nullptr; }
    if (_dir) { closedir(_dir); _dir = nullptr; }
  }
};

typedef SimFile File;

class SimFS {
  std::string _root;

  // Creates every path component of `path`, whether it's absolute ("/a/b")
  // or -- as SimFS's own root ("./sim_data") and every path built from it
  // always are -- relative ("./a/b", "a/b"). A relative path must accumulate
  // starting from "." (not ""), otherwise the first component gets
  // mkdir()'d as if it were rooted at the real filesystem's "/" (a real bug
  // this had: "./sim_data" was being split into a leading "." component and
  // then prefixed with "/", trying -- and silently failing, permission
  // denied -- to mkdir "/./sim_data" instead of "./sim_data").
  static void mkdirsRecursive(const std::string& path) {
    if (path.empty() || path == "/" || path == ".") return;
    bool absolute = (path[0] == '/');
    std::string cur = absolute ? "" : ".";
    size_t pos = absolute ? 1 : 0;
    while (pos <= path.size()) {
      size_t slash = path.find('/', pos);
      std::string part = (slash == std::string::npos) ? path.substr(pos) : path.substr(pos, slash - pos);
      if (!part.empty() && part != ".") {
        cur += "/" + part;
        ::mkdir(cur.c_str(), 0755);
      }
      if (slash == std::string::npos) break;
      pos = slash + 1;
    }
  }

  std::string fullPath(const char* path) const {
    if (path && path[0] == '/') return _root + path;
    return _root + "/" + (path ? path : "");
  }

  static std::string basenameOf(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? std::string(slash + 1) : std::string(path);
  }

  void ensureParentDir(const std::string& fullFilePath) const {
    size_t slash = fullFilePath.find_last_of('/');
    if (slash == std::string::npos) return;
    std::string dir = fullFilePath.substr(0, slash);
    mkdirsRecursive(dir);
  }

  static bool rmrf(const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (!d) { return ::remove(path.c_str()) == 0; }
    struct dirent* ent;
    bool ok = true;
    while ((ent = readdir(d)) != nullptr) {
      std::string nm = ent->d_name;
      if (nm == "." || nm == "..") continue;
      ok = rmrf(path + "/" + nm) && ok;
    }
    closedir(d);
    ok = (::rmdir(path.c_str()) == 0) && ok;
    return ok;
  }

public:
  explicit SimFS(const std::string& root) : _root(root) { mkdirsRecursive(root); }

  bool mkdir(const char* path) {
    mkdirsRecursive(fullPath(path));
    return true;
  }

  bool exists(const char* path) {
    struct stat st;
    return stat(fullPath(path).c_str(), &st) == 0;
  }

  bool remove(const char* path) {
    return ::remove(fullPath(path).c_str()) == 0;
  }

  bool rename(const char* oldPath, const char* newPath) {
    return ::rename(fullPath(oldPath).c_str(), fullPath(newPath).c_str()) == 0;
  }

  bool format() {
    // Wipe the sim device's whole data directory and recreate it empty --
    // mirrors a real LittleFS/SPIFFS format().
    rmrf(_root);
    mkdirsRecursive(_root);
    return true;
  }

  // Matches the ESP32 fs::FS::open(path, mode, create) shape that
  // DataStore.cpp/IdentityStore.cpp's platform-agnostic "#else" fallback
  // branches already call.
  SimFile open(const char* path, const char* mode = "r", bool create = false) {
    std::string full = fullPath(path);
    struct stat st;
    bool file_exists = (stat(full.c_str(), &st) == 0);

    if (file_exists && S_ISDIR(st.st_mode)) {
      DIR* d = opendir(full.c_str());
      if (!d) return SimFile();
      return SimFile(d, full, basenameOf(path));
    }

    bool want_write = (mode && mode[0] == 'w');
    if (want_write) {
      ensureParentDir(full);
      FILE* fp = fopen(full.c_str(), "wb");
      if (!fp) return SimFile();
      return SimFile(fp, basenameOf(path), 0);
    }

    if (!file_exists) return SimFile();
    FILE* fp = fopen(full.c_str(), "rb");
    if (!fp) return SimFile();
    return SimFile(fp, basenameOf(path), (size_t)st.st_size);
  }
};

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Phase 2 (Emscripten) IDBFS backend.
//
// Important finding from actually reading SimFile/SimFS above before writing
// this: NEITHER class needs a single line changed for Emscripten. Every op
// here (fopen/fread/fwrite/fclose, mkdir, opendir/readdir, stat, rename,
// remove) is a plain libc call, and Emscripten's C runtime already
// implements all of those against its own virtual filesystem (MEMFS by
// default) -- that's the whole point of Emscripten's libc port, and it's
// why this shim was written directly against fopen()-shaped calls in Phase
// 1 rather than some native-only API. So SimFS's constructor (mkdirsRecursive
// on "./sim_data") and every DataStore/IdentityStore read/write already work
// unmodified under Emscripten, exactly as they do natively -- just against
// MEMFS (in-memory, gone on refresh) instead of the real disk.
//
// The only thing actually missing for the browser is durability: MEMFS
// alone doesn't survive a page reload. IDBFS is Emscripten's IndexedDB-
// backed filesystem type that mirrors a MEMFS directory to/from IndexedDB.
// Mounting it is the one piece of real Emscripten-specific code needed, so
// it lives here (colocated with the FS shim it backs) rather than in
// sim_main.cpp:
//
//   1. FS.mount(IDBFS, {autoPersist: true}, root) -- autoPersist is a
//      built-in IDBFS option (see this SDK's upstream/emscripten/src/lib/
//      libidbfs.js) that hooks every file close() following a write and
//      queues a debounced FS.syncfs(false, cb) push to IndexedDB
//      automatically (batched to one push per JS event-loop tick, so a
//      DataStore save that opens/writes/closes several files in a row still
//      costs one IndexedDB commit, not several). This was chosen over a
//      hand-rolled periodic timer in the main loop: it can't drift out of
//      sync with what was actually written (a timer firing between saves
//      could persist a half-written state; a close()-triggered persist
//      never can), and it does nothing at all when nothing changed instead
//      of a timer's fixed idle-polling cost.
//   2. FS.syncfs(true, cb) -- the reverse direction, a one-time pull *from*
//      IndexedDB into the MEMFS mirror. This must complete, and its callback
//      must fire, before the app ever reads a file -- i.e. before setup()
//      runs -- since IndexedDB has no synchronous API. sim_main.cpp's
///     Emscripten main() calls this and only calls setup() from the
//      callback (sim_idbfs_ready(), EMSCRIPTEN_KEEPALIVE'd below so the
//      generated JS glue can call it back by name).
//
// root must be an absolute path ("/sim_data") that already exists as a
// plain MEMFS directory by the time this runs -- true here because SimFS's
// own constructor (a file-scope global in examples/companion_radio/main.cpp,
// so it runs during static init, before this function's caller in
// sim_main.cpp's main()) already mkdir'd "./sim_data", which resolves to the
// same node as "/sim_data" since Emscripten's cwd defaults to "/". Mounting
// IDBFS onto an existing *empty* directory is the exact pattern Emscripten's
// own test suite uses (test/fs/test_idbfs_sync.c: `FS.mkdir(...);
// FS.mount(IDBFS, ..., ...)`) -- BUT unlike the real ::mkdir() syscall
// SimFS's own C++ constructor already called (which returns EEXIST quietly
// and is ignored, POSIX mkdir -p style), JS's own `FS.mkdir()` throws a hard
// exception if the directory is already there, which it always will be by
// this point. Confirmed by actually running this in a real browser before
// trusting the comment that used to be here: first load produced an
// uncaught `ErrnoError: File exists` right here, which silently aborted
// main() before sim_idbfs_ready() ever ran -- so this needs its own
// try/catch, unlike Emscripten's own test (which never pre-creates the dir
// through a second path first).
inline void sim_fs_mount_idbfs(const char* root) {
  EM_ASM({
    var root = UTF8ToString($0);
    try { FS.mkdir(root); } catch (e) { /* already exists -- see comment above */ }
    FS.mount(IDBFS, { autoPersist: true }, root);
    FS.syncfs(true, function(err) {
      if (err) console.error('[sim] IDBFS initial syncfs(true) failed:', err);
      else console.log('[sim] IDBFS pull from IndexedDB complete, booting app...');
      _sim_idbfs_ready();
    });
  }, root);
}
#endif // __EMSCRIPTEN__
