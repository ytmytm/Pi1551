#include "ff.h"

#include <cstdio>
#include <cstring>
#include <map>

static std::map<FIL*, std::FILE*> files;

extern "C" FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
	const char* openMode = (mode & FA_WRITE) ? "r+b" : "rb";
	std::FILE* file = std::fopen(path, openMode);
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
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
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
	std::map<FIL*, std::FILE*>::iterator it = files.find(fp);
	if (it == files.end())
		return FR_INVALID_OBJECT;
	return std::fflush(it->second) == 0 ? FR_OK : FR_DISK_ERR;
}
