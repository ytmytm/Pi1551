#include "ff.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <fnmatch.h>
#define DIR PosixDIR
#include <dirent.h>
#undef DIR

static std::string cardRoot, cardCwd = "/", failNext;
static std::string cwdPrefix;
void NativeFatFSCwdPrefix(const std::string& prefix) { cwdPrefix = prefix; }
static unsigned failureCountdown = 0;
void NativeFatFSRoot(const std::string& root) { cardRoot = root; cardCwd = "/"; }
void NativeFatFSFail(const std::string& operation) { failNext = operation; failureCountdown = 1; }
void NativeFatFSFailNth(const std::string& operation, unsigned n) { failNext = operation; failureCountdown = n; }
static bool Fail(const char* operation) {
    if (failNext != operation) return false;
    if (--failureCountdown) return false;
    failNext.clear(); return true;
}
static std::string Canonical(const char* path) {
    std::string p = path;
    for (char& c : p) if (c == '\\') c = '/';
    size_t colon = p.find(':'); if (colon != std::string::npos) p.erase(0,colon+1);
    if (p.empty() || p[0] != '/') p = cardCwd + "/" + p;
    std::vector<std::string> parts; size_t pos = 0;
    while (pos < p.size()) {
        size_t end = p.find('/',pos); if (end == std::string::npos) end = p.size();
        std::string part = p.substr(pos,end-pos);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!part.empty() && part != ".") parts.push_back(part);
        pos = end + 1;
    }
    std::string result;
    for (const auto& part : parts) result += "/" + part;
    return result.empty() ? "/" : result;
}
static std::string Resolve(const char* path) {
    return cardRoot.empty() ? std::string(path) : cardRoot + Canonical(path);
}

static std::map<FIL*, std::FILE*> files;

extern "C" FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
	if (Fail("open")) return FR_DISK_ERR;
	std::string resolved = Resolve(path);
	if ((mode & FA_CREATE_NEW) && access(resolved.c_str(), F_OK) == 0) return FR_EXIST;
	const char* openMode = (mode & (FA_CREATE_NEW | FA_CREATE_ALWAYS)) ? "w+b" : (mode & FA_WRITE) ? "r+b" : "rb";
	std::FILE* file = std::fopen(resolved.c_str(), openMode);
	if (!file)
		return FR_NO_FILE;
	std::memset(fp, 0, sizeof(*fp));
	std::fseek(file, 0, SEEK_END);
	fp->obj.objsize = static_cast<FSIZE_t>(std::ftell(file));
	std::fseek(file, 0, SEEK_SET);
	files[fp] = file;
	return FR_OK;
}

extern "C" FRESULT f_close(FIL* fp)
{
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	std::fclose(it->second);
	files.erase(it);
	return FR_OK;
}

extern "C" FRESULT f_read(FIL* fp, void* buffer, UINT requested, UINT* read)
{
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	*read = static_cast<UINT>(std::fread(buffer, 1, requested, it->second));
	fp->fptr += *read;
	return std::ferror(it->second) ? FR_DISK_ERR : FR_OK;
}

extern "C" FRESULT f_write(FIL* fp, const void* buffer, UINT requested, UINT* written)
{
	if (Fail("write")) { *written = 0; return FR_DISK_ERR; }
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	if (Fail("partial")) requested /= 2;
	*written = static_cast<UINT>(std::fwrite(buffer, 1, requested, it->second));
	fp->fptr += *written;
	if (fp->fptr > fp->obj.objsize)
		fp->obj.objsize = fp->fptr;
	return std::ferror(it->second) ? FR_DISK_ERR : FR_OK;
}

extern "C" FRESULT f_lseek(FIL* fp, FSIZE_t offset)
{
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	if (std::fseek(it->second, static_cast<long>(offset), SEEK_SET) != 0)
		return FR_DISK_ERR;
	fp->fptr = offset;
	return FR_OK;
}

extern "C" FRESULT f_sync(FIL* fp)
{
	if (Fail("sync")) return FR_DISK_ERR;
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	return std::fflush(it->second) == 0 ? FR_OK : FR_DISK_ERR;
}

extern "C" FRESULT f_stat(const TCHAR* path, FILINFO* info) {
    struct stat st; if (stat(Resolve(path).c_str(), &st)) return FR_NO_FILE;
    if (info) {
        memset(info, 0, sizeof(*info)); info->fsize = st.st_size;
        info->fattrib = S_ISDIR(st.st_mode) ? AM_DIR : 0;
        const char* base = strrchr(path, '/'); base = base ? base + 1 : path;
        strncpy(info->fname, base, sizeof(info->fname) - 1);
    }
    return FR_OK;
}
extern "C" FRESULT f_chdir(const TCHAR* path) {
    if (Fail("chdir")) return FR_DISK_ERR;
    FILINFO info;
    if (f_stat(path, &info) != FR_OK || !(info.fattrib & AM_DIR)) return FR_NO_PATH;
    cardCwd = Canonical(path); return FR_OK;
}
extern "C" FRESULT f_getcwd(TCHAR* buffer, UINT length) {
    std::string cwd = cwdPrefix + cardCwd;
    if (cwd.size() >= length) return FR_INVALID_PARAMETER;
    strcpy(buffer, cwd.c_str()); return FR_OK;
}
extern "C" FRESULT f_mkdir(const TCHAR* path) {
    if (mkdir(Resolve(path).c_str(), 0777) == 0) return FR_OK;
    return access(Resolve(path).c_str(), F_OK) == 0 ? FR_EXIST : FR_DISK_ERR;
}
struct NativeDirectory { PosixDIR* directory; std::string path; };
static std::map<DIR*, NativeDirectory> directories;
extern "C" FRESULT f_opendir(DIR* dir, const TCHAR* path) {
    PosixDIR* p = opendir(Resolve(path).c_str()); if (!p) return FR_NO_PATH;
    directories[dir] = NativeDirectory{p, path}; return FR_OK;
}
extern "C" FRESULT f_readdir(DIR* dir, FILINFO* info) {
    if (!directories.count(dir)) return FR_INVALID_OBJECT;
    NativeDirectory& d = directories[dir];
    if (!info) { rewinddir(d.directory); return FR_OK; }
    struct dirent* entry = readdir(d.directory);
    if (!entry) { memset(info,0,sizeof(*info)); return FR_OK; }
    return f_stat((d.path + "/" + entry->d_name).c_str(), info);
}
extern "C" FRESULT f_closedir(DIR* dir) {
    if (!directories.count(dir)) return FR_INVALID_OBJECT;
    closedir(directories[dir].directory); directories.erase(dir); return FR_OK;
}
extern "C" FRESULT f_unlink(const TCHAR* path) {
    std::string resolved = Resolve(path);
    struct stat st;
    if (stat(resolved.c_str(), &st)) return FR_NO_FILE;
    int result = S_ISDIR(st.st_mode) ? rmdir(resolved.c_str()) : unlink(resolved.c_str());
    return result == 0 ? FR_OK : FR_DISK_ERR;
}
extern "C" FRESULT f_rename(const TCHAR* from, const TCHAR* to) {
    if (Fail("rename")) return FR_DISK_ERR;
    if (access(Resolve(to).c_str(),F_OK) == 0) return FR_EXIST;
    return rename(Resolve(from).c_str(), Resolve(to).c_str()) == 0 ? FR_OK : FR_DISK_ERR;
}
extern "C" FRESULT f_findnext(DIR* dir, FILINFO* info) {
    FRESULT result;
    do { result = f_readdir(dir,info); }
    while (result == FR_OK && info->fname[0] && fnmatch(dir->pat,info->fname,0));
    return result;
}
extern "C" FRESULT f_findfirst(DIR* dir, FILINFO* info, const TCHAR* path, const TCHAR* pattern) {
    FRESULT result = f_opendir(dir,path);
    if (result != FR_OK) return result;
    dir->pat = pattern;
    return f_findnext(dir,info);
}
