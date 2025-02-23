// Shaders.cpp : Defines the entry point for the console application.
//

#include "dll_assembler.hpp"
#include <iostream>
#include <tchar.h>

CRITICAL_SECTION gl_CS;

bool gl_dumpBIN = false;
bool gl_dumpASM = false;
uint8_t gl_separation = 14;
float gl_screenSize = 27;
float gl_conv = 1.0;
float gl_minConv = 0.0f;
bool gl_Type = false;
bool gl_DepthZ = false;
bool gl_left = false;
std::filesystem::path dump_path;

// Primary hash calculation for all shader file names, all textures.
// 64 bit magic FNV-0 and FNV-1 prime
#define FNV_64_PRIME ((UINT64)0x100000001b3ULL)
static UINT64 fnv_64_buf(const void* buf, size_t len)
{
	UINT64 hval = 0;
	unsigned const char* bp = (unsigned const char*)buf;	/* start of buffer */
	unsigned const char* be = bp + len;		/* beyond end of buffer */

	// FNV-1 hash each octet of the buffer
	while (bp < be)
	{
		// multiply by the 64 bit FNV magic prime mod 2^64 */
		hval *= FNV_64_PRIME;
		// xor the bottom with the current octet
		hval ^= (UINT64)*bp++;
	}
	return hval;
}

vector<string> enumerateFiles(string pathName, string filter = "") {
	vector<string> files;
	WIN32_FIND_DATAA FindFileData;
	HANDLE hFind;
	string sName = pathName;
	sName.append(filter);
	hFind = FindFirstFileA(sName.c_str(), &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE)	{
		string fName = pathName;
		fName.append(FindFileData.cFileName);
		files.push_back(fName);
		while (FindNextFileA(hFind, &FindFileData)) {
			fName = pathName;
			fName.append(FindFileData.cFileName);
			files.push_back(fName);
		}
		FindClose(hFind);
	}
	return files;
}

int _tmain(int argc, _TCHAR* argv[])
{
	vector<string> gameNames;
	string pathName;
	vector<string> files;
	FILE* f;

	char gamebuffer[100000];	
	InitializeCriticalSection(&gl_CS);
	vector<string> lines;
	fopen_s(&f, "gamelist.txt", "rb");
	if (f) {
		size_t fr = ::fread(gamebuffer, 1, 100000, f);
		fclose(f);
		lines = stringToLines(gamebuffer, fr);
	}
	if (lines.size() > 0) {
		for (auto i = lines.begin(); i != lines.end(); i++) {
			gameNames.push_back(*i);
		}
	}
	for (DWORD i = 0; i < gameNames.size(); i++) {
		string gameName = gameNames[i];
		if (gameName[0] == ';')
			continue;
		cout << gameName << endl;

		pathName = gameName;
		pathName.append("\\ShaderCacheGeo3D\\");
		auto newFiles = enumerateFiles(pathName, "????????-??.bin");
		//auto newFiles = enumerateFiles(pathName, "????????-??.txt");
		files.insert(files.end(), newFiles.begin(), newFiles.end());
	}

//#pragma omp parallel
//#pragma omp for
	for (int i = 0; i < files.size(); i++) {
		string fileName = files[i];
		auto BIN = readFile(fileName);
		auto ASM = asmShader(BIN.data(), BIN.size());
		auto newBin = assembler(false, ASM, BIN);
		//auto crc2 = fnv_64_buf(BIN.data(), BIN.size());
		//printf_s("%s-%016llX", fileName.c_str(), crc2);
		//cout << endl;
		//s_stromgf
		/*
		ID3DBlob* pDissassembly;
		LPCSTR error = nullptr;
		HRESULT hr = D3DDisassemble(BIN.data(), BIN.size(), 0, error, &pDissassembly);
		if (hr == S_OK) {
			auto ASM = readV(pDissassembly->GetBufferPointer(), pDissassembly->GetBufferSize());
			if (ASM.size() > 0) {
				string write = fileName + ".ASM";
				fopen_s(&f, write.c_str(), "wb");
				fwrite(ASM.data(), 1, ASM.size(), f);
				fclose(f);
			}
		}
		else {
			DWORD* dw = (DWORD*)BIN.data();
			int dwSize = BIN.size() / 4;
			vector<DWORD> temp;
			for (int i = 0; i < dwSize; i++) {
				temp.push_back(*(dw + i));
			}
			temp = changeSM2(temp, true, gl_conv, gl_screenSize, gl_separation);
			UINT8* db = (UINT8*)temp.data();
			int dbSize = temp.size() * 4;
			vector<UINT8> BIN2;
			for (int i = 0; i < dbSize; i++) {
				BIN2.push_back(*(db + i));
			}
			if (BIN2.size() > 0) {
				string write = fileName + ".BIN2";
				fopen_s(&f, write.c_str(), "wb");
				fwrite(BIN2.data(), 1, BIN2.size(), f);
				fclose(f);
			}
		}
		*/
	}
	cout << endl;
	return 0;
}
