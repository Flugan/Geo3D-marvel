// Shaders.cpp : Defines the entry point for the console application.
//

#include "dll_assembler.hpp"
#include <iostream>
#include <tchar.h>

CRITICAL_SECTION gl_CS;

bool gl_dumpBIN = false;
bool gl_dumpASM = false;
bool gl_Type = false;
float gl_separation = 0.1f;
float gl_screenSize = 55;
float gl_conv = 1.0;
bool gl_left = false;
bool gl_DepthZ = false;
std::filesystem::path dump_path;

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
	/*
	{
		auto ASM = readFile("ac2-vs.txt");
		auto ASM2 = patch(true, ASM, true, gl_conv, gl_screenSize, gl_separation);
		auto ASM3 = changeASM(true, ASM2, true, gl_conv, gl_screenSize, gl_screenSize);
		auto ASM4 = readFile("ac2-ps.txt");
		auto ASM5 = patch(true, ASM, true, gl_conv, gl_screenSize, gl_separation);
	}

	{
		auto ASM = readFile("bio-vs.txt");
		auto ASM2 = patch(false, ASM, true, gl_conv, gl_screenSize, gl_separation);
		auto ASM3 = changeASM(false, ASM2, true, gl_conv, gl_screenSize, gl_screenSize);
		auto ASM4 = readFile("bio-ps.txt");
		auto ASM5 = patch(false, ASM, true, gl_conv, gl_screenSize, gl_separation);
	}

	{
		auto ASM = readFile("SMR-vs.txt");
		auto ASM2 = patch(false, ASM, true, gl_conv, gl_screenSize, gl_separation);
		auto ASM3 = changeASM(false, ASM2, true, gl_conv, gl_screenSize, gl_screenSize);
		auto ASM4 = readFile("SMR-ps.txt");
		auto ASM5 = patch(false, ASM, true, gl_conv, gl_screenSize, gl_separation);
	}
	*/
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
		pathName.append("\\ShaderCache\\");
		//auto newFiles = enumerateFiles(pathName, "????????-??.bin");
		auto newFiles = enumerateFiles(pathName, "????????-??.txt");
		files.insert(files.end(), newFiles.begin(), newFiles.end());
	}

//#pragma omp parallel
//#pragma omp for
	for (int i = 0; i < files.size(); i++) {
		string fileName = files[i];
		//EnterCriticalSection(&gl_CS);
		//auto BIN = readFile(fileName);
		auto ASM = readFile(fileName);
		//LeaveCriticalSection(&gl_CS);
		//auto ASM = disassembler(BIN);
		/*/
		if (ASM.size() == 0) {
			string ASMfilename = fileName;
			ASMfilename.erase(fileName.size() - 3, 3);
			ASMfilename.append("error.txt");
			fopen_s(&f, ASMfilename.c_str(), "wb");
			fwrite(ASM.data(), 1, ASM.size(), f);
			fclose(f);
			continue;
		}*/
		if (ASM[0] == ';') {
			auto left = changeASM(false, ASM, true, 1.0f, 15.6f, 10.0f);
			auto right = changeASM(false, ASM, false, 1.0f, 15.6f, 10.0f);
			auto ASMLeft = assembler(false, left, left);
			auto ASMRight = assembler(false, right, right);
			auto ASM2 = disassembler(ASMLeft);
			/*
			string dxilFilename = fileName;
			auto dxil = readFile(dxilFilename);
			dxilFilename.erase(dxilFilename.size() - 3, 3);
			dxilFilename.append("DXIL");
			fopen_s(&f, dxilFilename.c_str(), "wb");
			fwrite(dxil.data(), 1, dxil.size() - 1, f);
			fclose(f);

			auto DXILm = toDXILm(dxil);
			dxilFilename.erase(dxilFilename.size() - 4, 4);
			dxilFilename.append("DXILm");
			fopen_s(&f, dxilFilename.c_str(), "wb");
			fwrite(DXILm.data(), 1, DXILm.size() - 1, f);
			fclose(f);

			auto DXIL = fromDXILm(DXILm);
			bool valid = true;
			if (DXIL.size() == dxil.size()) {
				for (size_t i = 0; i < dxil.size(); i++) {
					if (DXIL[i] != dxil[i]) {
						valid = false;
						break;
					}
				}
			}
			else {
				valid = false;
			}
			if (valid)
				continue;
			dxilFilename.erase(dxilFilename.size() - 5, 5);
			dxilFilename.append("DXIL_Restored");
			fopen_s(&f, dxilFilename.c_str(), "wb");
			fwrite(DXIL.data(), 1, DXIL.size() - 1, f);
			fclose(f);
			continue;
			*/
		}
		/*
		else {
			string ASMfilename = fileName;
			ASMfilename.erase(fileName.size() - 3, 3);
			ASMfilename.append("txt");
			EnterCriticalSection(&gl_CS);
			fopen_s(&f, ASMfilename.c_str(), "wb");
			fwrite(ASM.data(), 1, ASM.size(), f);
			fclose(f);
			LeaveCriticalSection(&gl_CS);
		}
		*/
		/*
		auto CBO = assembler(ASM, BIN);
		bool valid = true;
		if (CBO.size() == BIN.size()) {
			for (size_t i = 0; i < CBO.size(); i++) {
				if (CBO[i] != BIN[i]) {
					valid = false;
					break;
				}
			}
		}
		else {
			valid = false;
		}
		
		if (!valid) {
			fileName.erase(fileName.size() - 3, 3);
			fileName.append("cbo");
			FILE* f;
			fopen_s(&f, fileName.c_str(), "wb");
			fwrite(CBO.data(), 1, CBO.size(), f);
			fclose(f);

			fileName.erase(fileName.size() - 3, 3);
			fileName.append("fail.txt");
			auto ASM2 = disassembler(CBO);
			if (ASM2.size() > 0) {
				fopen_s(&f, fileName.c_str(), "wb");
				fwrite(ASM2.data(), 1, ASM2.size(), f);
				fclose(f);
			}
		}
		*/
	}
	writeLUT();
	cout << endl;
	return 0;
}
