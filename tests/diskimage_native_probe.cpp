#include "DiskImage.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

extern "C" void SetACTLed(int) {}

int main(int argc, char** argv)
{
	if (argc != 2 && argc != 3 && argc != 4)
		return 2;
	std::ifstream input(argv[1], std::ios::binary);
	std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
		std::istreambuf_iterator<char>());
	FILINFO info = {};
	std::snprintf(info.fname, sizeof(info.fname), "%s", argv[1]);
	DiskImage* image = new DiskImage();
	bool ok = image->OpenD64(&info, bytes.data(), bytes.size());
	if (argc == 3)
	{
		unsigned char expected[256];
		for (unsigned i = 0; i < sizeof(expected); ++i)
			expected[i] = static_cast<unsigned char>(i * 37 + 11);
		unsigned char decoded[256];
		ok = ok && image->SetDecodedSector(18, 0, expected)
			&& image->GetDecodedSector(18, 0, decoded)
			&& std::equal(decoded, decoded + 256, expected);
		image->SetReadOnly(true); // Keep this probe non-destructive.
		std::cout << "write-ok=" << ok << '\n';
		delete image;
		return ok ? 0 : 1;
	}
	if (argc == 2)
	{
		size_t offset = 0;
		unsigned mismatches = 0;
		for (unsigned track = 1; ok && track <= 35; ++track)
		{
			const unsigned sectors = track < 18 ? 21 : track < 25 ? 19 : track < 31 ? 18 : 17;
			for (unsigned sector = 0; sector < sectors; ++sector, offset += 256)
			{
				unsigned char decoded[256];
				if (!image->GetDecodedSector(track, sector, decoded)
					|| !std::equal(decoded, decoded + 256, bytes.begin() + offset))
					++mismatches;
			}
		}
		std::cout << "ok=" << (ok && mismatches == 0)
			<< " sectors=683 mismatches=" << mismatches << '\n';
		delete image;
		return ok && mismatches == 0 ? 0 : 1;
	}
	unsigned track = static_cast<unsigned>(std::strtoul(argv[2], nullptr, 0));
	unsigned sector = static_cast<unsigned>(std::strtoul(argv[3], nullptr, 0));
	unsigned char decoded[256];
	ok = ok && image->GetDecodedSector(track, sector, decoded);
	std::cout << "ok=" << ok;
	if (ok)
	{
		std::cout << " data=";
		for (unsigned i = 0; i < 16; ++i)
			std::cout << std::hex << unsigned(decoded[i] >> 4) << unsigned(decoded[i] & 15);
	}
	std::cout << '\n';
	delete image;
	return ok ? 0 : 1;
}
