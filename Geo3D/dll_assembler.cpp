#include "dll_assembler.hpp"
#include "crc32_hash.hpp"
#include "dxcapi.h"
#include "wrl.h"


using Microsoft::WRL::ComPtr;

FILE *failFile = NULL;

vector<DWORD> assembleIns(string s);
DWORD strToDWORD(string s);

extern bool gl_dumpBIN;
extern bool gl_dumpASM;
extern bool gl_Type;
extern bool gl_DepthZ;

HMODULE dxc_module = 0;
HMODULE dxil_module = 0;

extern std::filesystem::path dump_path;

vector<UINT8> readV(const void* code, size_t length) {
UINT8 *codeByte = (UINT8 *)code;
vector<UINT8> ret;
	for (size_t i = 0; i < length; i++) {
		ret.push_back(codeByte[i]);
	}
	return ret;
}

uint32_t dumpShader(bool dx9, const wchar_t *type, const void *pData, size_t length, bool pipeline, uint64_t handle) {
	uint32_t crc = compute_crc32((UINT8*)pData, length);
	FILE *f;
	wchar_t sPath[MAX_PATH];
	if (length > 0) {
		if (gl_dumpBIN) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-%s.txt", crc, type);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(pData, 1, length, f);
				fclose(f);
			}
		}
		if (gl_dumpASM) {
			auto v = readV(pData, length);
			vector<UINT8> ASM;
			if (dx9) {
				ASM = asmShader(dx9, pData, length);
			}
			else {
				ASM = disassembler(v);
			}
			if (ASM.size() > 0) {
				filesystem::path file;
				if (handle != 0 && pipeline) {
					swprintf_s(sPath, MAX_PATH, L"%16llX", handle);
					auto pipeline_path = dump_path / sPath;
					filesystem::create_directories(pipeline_path);
					swprintf_s(sPath, MAX_PATH, L"%08lX-%s.txt", crc, type);
					file = pipeline_path / sPath;
				}
				else {
					filesystem::create_directories(dump_path);
					swprintf_s(sPath, MAX_PATH, L"%08lX-%s.txt", crc, type);
					file = dump_path / sPath;
				}
				_wfopen_s(&f, file.c_str(), L"wb");
				if (f != 0) {
					fwrite(ASM.data(), 1, ASM.size(), f);
					fclose(f);
				}
			}
		}
	}
	return crc;
}

vector<DWORD> changeSM2(vector<DWORD> code, bool left, int tempReg, float conv, float screenSize, float separation) {
	vector<DWORD> newCode;
	bool define = false;
	for (size_t i = 0; i < code.size(); i++) {
		if (code[i] == 0xFFFF) {
			//add r1.x, r0.w, c250.x
			//mad r0.x, r1.x, c250.y, r0.x
			//mov oPos, r0
			newCode.push_back(0x03000002); // add
			newCode.push_back(0x80010000 + tempReg + 1);
			newCode.push_back(0x80FF0000 + tempReg + 0);
			newCode.push_back(0xA00000FA);


			newCode.push_back(0x04000004); // mad
			newCode.push_back(0x80010000 + tempReg + 0);
			newCode.push_back(0x80000000 + tempReg + 1);
			newCode.push_back(0xA05500FA);
			newCode.push_back(0x80000000 + tempReg + 0);

			newCode.push_back(0x02000001); // mov
			newCode.push_back(0xC00F0000);
			newCode.push_back(0x80E40000 + tempReg + 0);

			newCode.push_back(code[i]);
			break;
		}
		if (!define) {
			if (code[i] == 0x200001F) {
				float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
				// first declare
				newCode.push_back(0x5000051);
				newCode.push_back(0xA00F00FA);
				float fConv = -conv;
				float fSep = left ? -finalSep : finalSep;
				DWORD *conv = (DWORD *)&fConv;
				DWORD *sep = (DWORD *)&fSep;
				newCode.push_back(*conv);
				newCode.push_back(*sep);
				newCode.push_back(0);
				newCode.push_back(0);
				// complete declare
				newCode.push_back(code[i++]);
				newCode.push_back(code[i++]);
				newCode.push_back(code[i]);
				define = true;
			}
			else {
				newCode.push_back(code[i]);
			}
		}
		else {
			int numValues = (code[i] & 0xFF000000) >> 24;
			newCode.push_back(code[i++]);
			if ((code[i] & 0xF0000000) == 0xC0000000) {
				// oPos, replace oPos with r0
				DWORD oPosReplacement = 0x80000000 + tempReg + (code[i++] & 0xFFFFFFF);
				newCode.push_back(oPosReplacement);
			}
			else {
				newCode.push_back(code[i++]);
			}
			numValues--;
			while (numValues) {
				newCode.push_back(code[i++]);
				numValues--;
			}
			i--;
		}
	}
	return newCode;
}

vector<UINT8> convertSM2(vector<UINT8> asmFile) {
	string reg((char *)asmFile.data(), asmFile.size());
	map<size_t, string> outputs;
	map<string, string> declare;
	for (size_t i = 0; i < 10; i++) {
		string tex = "oT" + to_string(i);
		if (reg.find(tex) != string::npos) {
			outputs[outputs.size()] = tex;
			if (i > 0)
				declare[tex] = "dcl_texcoord" + to_string(i);
			else
				declare[tex] = "dcl_texcoord";
		}
	}
	if (reg.find("oPos") != string::npos) {
		outputs[outputs.size()] = "oPos";
		declare["oPos"] = "dcl_position";
	}
	for (size_t i = 0; i < 10; i++) {
		string color = "oD" + to_string(i);
		if (reg.find(color) != string::npos) {
			outputs[outputs.size()] = color;
			if (i > 0)
				declare[color] = "dcl_color" + to_string(i);
			else
				declare[color] = "dcl_color";
		}
	}
	if (reg.find("oFog") != string::npos) {
		outputs[outputs.size()] = "oFog";
		declare["oFog"] = "dcl_fog";
	}
	string dcl_outputs;
	for (size_t i = 0; i < outputs.size(); i++) {
		string orig = outputs[i];
		string dcl = declare[orig];
		dcl_outputs += "    " + dcl + " o" + to_string(i) + "\n";

		size_t pos = reg.find(orig);
		while (pos != string::npos) {
			reg = reg.substr(0, pos) + "o" + to_string(i) + reg.substr(pos + orig.size());
			pos = reg.find(orig);
		}
	}

	size_t pos = reg.find("preshader");
	if (pos != string::npos) {
		size_t endPos = reg.find("\n\n", pos);
		size_t next = reg.rfind('\n', pos) + 1;
		while (next < endPos) {
			reg = reg.substr(0, next) + "//PRESHADER" + reg.substr(next);
			next = reg.find('\n', next) + 1;
		}
	}

	size_t sincosNum = 0;
	pos = reg.find("sincos");
	while (pos != string::npos) {
		sincosNum++;
		size_t c1 = reg.find(',', pos);
		size_t c2 = reg.find(',', c1 + 1);
		size_t c3 = reg.find(',', c2 + 1);
		size_t eL = reg.find('\n', pos);
		string const1 = reg.substr(c2 + 2, c3 - c2 - 2);
		string const2 = reg.substr(c3 + 2, eL - c3 - 2);
		reg = reg.substr(0, c2) + reg.substr(eL);

		pos = reg.find("def " + const1);
		if (pos != string::npos) {
			eL = reg.find('\n', pos);
			reg = reg.substr(0, pos) + "// Discarded sincos constant " + const1 + reg.substr(eL);
			sincosNum++;
		}

		pos = reg.find("def " + const2);
		if (pos != string::npos) {
			eL = reg.find('\n', pos);
			reg = reg.substr(0, pos) + "// Discarded sincos constant " + const2 + reg.substr(eL);
			sincosNum++;
		}

		pos = reg.find("sincos");
		for (size_t i = 0; i < sincosNum; i++)
			pos = reg.find("sincos", pos + 1);
	}

	size_t vsPos = reg.find("vs_2");
	if (vsPos != string::npos)
		reg = reg.substr(0, vsPos) + "vs_3_0" + reg.substr(vsPos + 6);

	size_t psPos = reg.find("ps_2");
	if (psPos != string::npos)
		reg = reg.substr(0, psPos) + "ps_3_0" + reg.substr(psPos + 6);

	vsPos = reg.find("vs_1");
	if (vsPos != string::npos)
		reg = reg.substr(0, vsPos) + "vs_3_0" + reg.substr(vsPos + 6);

	psPos = reg.find("ps_1");
	if (psPos != string::npos)
		reg = reg.substr(0, psPos) + "ps_3_0" + reg.substr(psPos + 6);

	size_t dcl_pos = reg.rfind("dcl_");
	dcl_pos = reg.find("\n", dcl_pos) + 1;
	reg = reg.substr(0, dcl_pos) + dcl_outputs + reg.substr(dcl_pos);

	return readV(reg.data(), reg.size());
}

vector<UINT8> asmShader(bool dx9, const void* pData, size_t length) {
	auto v = readV(pData, length);
	if (dx9) {
		LPD3DXBUFFER pDisassembly = NULL;
		D3DXDisassembleShader((const DWORD*)v.data(), FALSE, NULL, &pDisassembly);
		auto ASM = readV(pDisassembly->GetBufferPointer(), pDisassembly->GetBufferSize());
		string reg((char*)ASM.data());
		if (reg.find("vs_2") != string::npos ||
			reg.find("ps_2") != string::npos ||
			reg.find("vs_1") != string::npos ||
			reg.find("ps_1") != string::npos) {
			return convertSM2(ASM);
		}
		else {
			return ASM;
		}
	}
	else {
		return disassembler(v);
	}
}

vector<UINT8> patch(bool dx9, vector<UINT8> shader, bool left, float conv, float screenSize, float separation) {
	if (dx9) {
		vector<UINT8> shaderOut;
		auto lines = stringToLines((char*)shader.data(), shader.size());
		string shader;
		bool dcl = false;
		for (size_t i = 0; i < lines.size(); i++) {
			string s = lines[i];
			if (s.find("dcl") != string::npos) {
				dcl = true;
				shader += s + "\n";
			}
			else {
				// before dcl
				shader += s + "\n";
				if (s.find("vs_") != string::npos) {
					for (size_t j = i + 1; j < lines.size(); j++) {
						string d = lines[j];
						if (d.find("def") == string::npos) {
							float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
							char buf[80];
							sprintf_s(buf, 80, "%.6f", left ? finalSep : -finalSep);
							string sepS(buf);
							sprintf_s(buf, 80, "%.3f", conv);
							string convS(buf);
							string signS = left ? "1" : "-1";
							shader += "    def c250, " + convS + ", " + sepS + ", " + signS + ", 0\n";
							i = j - 1;
							break;
						}
						shader += d + "\n";
					}
				}
			}
		}
		regex stereo("stereo");
		shader = regex_replace(shader, stereo, "c249");
		shaderOut = readV(shader.data(), shader.size());
		return shaderOut;
	}
	else if (shader.size() > 0 && shader[0] != ';') {
		vector<UINT8> shaderOut;
		auto lines = stringToLines((char*)shader.data(), shader.size());
		string shader;
		bool dcl = false;
		bool dcl_ICB = false;
		int temp = 0;
		bool stereoDone = false;
		string sReg;
		for (size_t i = 0; i < lines.size(); i++) {
			string s = lines[i];
			if (s.find("dcl") == 0) {
				dcl = true;
				dcl_ICB = false;
				if (s.find("dcl_temps") == 0) {
					string num = s.substr(10);
					temp = atoi(num.c_str()) + 1;
					shader += "dcl_temps " + to_string(temp) + "\n";
				}
				else if (s.find("dcl_immediateConstantBuffer") == 0) {
					dcl_ICB = true;
					shader += s + "\n";
				}
				else {
					shader += s + "\n";
				}
			}
			else if (dcl_ICB == true) {
				shader += s + "\n";
			}
			else if (dcl == true) {
				// after dcl
				if (temp == 0) {
					// add temps
					temp = 1;
					shader += "dcl_temps 1\n";
				}
				if (!stereoDone) {
					stereoDone = true;
					sReg = "r" + to_string(temp - 1) + ".xyzw";
					float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
					char buf[80];
					sprintf_s(buf, 80, "%.6f", left ? -finalSep : finalSep);
					string sep(buf);
					sprintf_s(buf, 80, "%.3f", -conv);
					string conv(buf);
					string side = left ? "1" : "-1";

					shader += "mov " + sReg + "l(" + sep + ", " + conv + ", " + side + ", 0)";
				}
				shader += s + "\n";
			}
			else {
				// before dcl
				shader += s + "\n";
			}
		}
		regex stereo("stereo");
		shader = regex_replace(shader, stereo, "sReg");
		shaderOut = readV(shader.data(), shader.size());
		return shaderOut;
	}
	else {
		float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
		double dConv = -conv;
		DWORD64* pConv = (DWORD64*)&dConv;
		double dSep = left ? finalSep : -finalSep;
		DWORD64* pSep = (DWORD64*)&dSep;
		char buf[80];
		sprintf_s(buf, 80, "0x%016llX", *pSep);
		string sepS(buf);
		sprintf_s(buf, 80, "0x%016llX", *pConv);
		string convS(buf);
		string sideS = left ? "1" : "-1";

		string reg((char*)shader.data(), shader.size());
		regex sepX("stereo.x");
		regex convX("stereo.y");
		regex sideX("stereo.z");

		reg = regex_replace(reg, sepX, sepS);
		reg = regex_replace(reg, convX, convS);
		reg = regex_replace(reg, sideX, sideS);

		return readV(reg.data(), reg.size());
	}
}

vector<UINT8> changeDXIL(vector<UINT8> ASM, bool left, float conv, float screenSize, float separation) {
	vector<UINT8> shaderOutput;
	auto lines = stringToLines((char*)ASM.data(), ASM.size());

	size_t oPos = 0;
	string oReg = "";
	// Find Position within Output signature declaration
	for (size_t i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (s == "; Output signature:") {
			for (size_t j = ++i; j < lines.size(); j++) {
				string pos = lines[j];
				if (pos.substr(0, 13) == "; SV_Position") {
					// position in second output signature list
					oReg = to_string(j - i - 3);
					oPos = j;
					break;
				}
			}
		}
	}
	// Position not found return empty string as normal
	if (oPos == 0)
		return shaderOutput;

	// Scan linearly  for better and better info
	vector<size_t> output;
	string outString = "  call void @dx.op.storeOutput.f32(i32 5, i32 " + oReg + ", i32 0, i8 0, float ";
	size_t outLenght = outString.length();
	for (size_t i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (s.substr(0, outLenght) == outString) {
			output.push_back(i);
		}
	}

	if (output.size() == 0) {
		return shaderOutput;
	}

	for (size_t k = 0; k < output.size(); k++) {
		size_t outPos = output[k];
		size_t filePos = outPos - 1;
		size_t lastValue = 0;

		string sOut = lines[outPos];
		string sX = lines[outPos].substr(outLenght);
		sX = sX.substr(0, sX.find(')'));
		string sZ = lines[outPos + 2].substr(outLenght);
		sZ = sZ.substr(0, sZ.find(')'));
		string sW = lines[outPos + 3].substr(outLenght);
		sW = sW.substr(0, sW.find(')'));
		string start = sOut.substr(0, sOut.find(sX));
		string end = sOut.substr(sOut.find(sX) + sX.length());

		for (size_t i = outPos; i > 0; i--) {
			string s = lines[i];
			string sNum;
			if (s.substr(0, 4) > "  %0" && s.substr(0, 4) <= "  %9") {
				sNum = s.substr(3);
				sNum = sNum.substr(0, sNum.find(" "));
				lastValue = atoi(sNum.c_str());
				break;
			}
			if (s.substr(0, 10) == "; <label>:") {
				sNum = s.substr(10);
				if (sNum.find(" ") < sNum.size())
					sNum = sNum.substr(0, sNum.find(" "));
				lastValue = atoi(sNum.c_str());
				break;
			}
		}

		// Go through the wilderness
		vector<string> shader;
		vector<string> shaderS;
		size_t sizeGap = 0;
		size_t rowGap = 0;

		float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
		double dConv = -conv;
		DWORD64* pConv = (DWORD64*)&dConv;
		double dSep = left ? -finalSep : finalSep;
		DWORD64* pSep = (DWORD64*)&dSep;
		char buf[80];
		sprintf_s(buf, 80, "0x%016llX", *pSep);
		string sepS(buf);
		sprintf_s(buf, 80, "0x%016llX", *pConv);
		string convS(buf);

		if (gl_Type) {
			if (gl_DepthZ)
				shaderS.push_back("  %" + to_string(lastValue + 1) + " = fadd fast float " + sZ + ", " + convS);
			else
				shaderS.push_back("  %" + to_string(lastValue + 1) + " = fadd fast float " + sW + ", " + convS);
			shaderS.push_back("  %" + to_string(lastValue + 2) + " = fmul fast float %" + to_string(lastValue + 1) + ", " + sepS);
			shaderS.push_back("  %" + to_string(lastValue + 3) + " = fadd fast float " + sX + ", %" + to_string(lastValue + 2));
			sizeGap = 3;
			rowGap = 3;
		}
		else {
			// find label or main
			string startNumber = "0";
			for (size_t j = filePos; j > 0; j--) {
				if (lines[j].find("main") != string::npos) {
					break;
				}
				if (lines[j].find("<label>:") != string::npos) {
					startNumber = lines[j].substr(10);
					if (startNumber.find(" ") < startNumber.size())
						startNumber = startNumber.substr(0, startNumber.find(" "));
					break;
				}
			}

			shaderS.push_back("  %" + to_string(lastValue + 1) + " = fadd fast float " + sX + ", 0.000000e+00");
			if (gl_DepthZ)
				shaderS.push_back("  %" + to_string(lastValue + 2) + " = fcmp fast une float " + sZ + ", 1.000000e+00");
			else
				shaderS.push_back("  %" + to_string(lastValue + 2) + " = fcmp fast une float " + sW + ", 1.000000e+00");
			shaderS.push_back("  br i1 %" + to_string(lastValue + 2) + ", label %" + to_string(lastValue + 3) + ", label %" + to_string(lastValue + 7));
			shaderS.push_back("");
			shaderS.push_back("; <label>:" + to_string(lastValue + 3));
			if (gl_DepthZ)
				shaderS.push_back("  %" + to_string(lastValue + 4) + " = fadd fast float " + sZ + ", " + convS);
			else
				shaderS.push_back("  %" + to_string(lastValue + 4) + " = fadd fast float " + sW + ", " + convS);
			shaderS.push_back("  %" + to_string(lastValue + 5) + " = fmul fast float %" + to_string(lastValue + 4) + ", " + sepS);
			shaderS.push_back("  %" + to_string(lastValue + 6) + " = fadd fast float " + sX + ", %" + to_string(lastValue + 5));
			shaderS.push_back("  br label %" + to_string(lastValue + 7));
			shaderS.push_back("");
			shaderS.push_back("; <label>:" + to_string(lastValue + 7));
			shaderS.push_back("  %" + to_string(lastValue + 8) + " = phi float [ %" +
				to_string(lastValue + 6) + ", %" + to_string(lastValue + 3) + " ], [ %" + to_string(lastValue + 1) + ", %" + startNumber + " ]");
			sizeGap = 8;
			rowGap = 12;
		}
		for (size_t i = 0; i < lines.size(); i++) {
			string s = lines[i];
			string s2;
			size_t prevOff = 0;
			size_t off = 0;
			while ((off = s.find("%", prevOff + 1)) < s.size()) {
				if (s.substr(0, 10) == "; <label>:") {
					string startNumber = s.substr(10);
					if (startNumber.find(" ") < startNumber.size())
						startNumber = startNumber.substr(0, startNumber.find(" "));
					size_t labelNum = atoi(startNumber.c_str());
					if (labelNum > lastValue)
						labelNum += sizeGap;
					string newLabel = "; <label>:" + to_string(labelNum);
					s = newLabel + s.substr(newLabel.size());
				}

				s2 += s.substr(prevOff, off - prevOff);
				prevOff = off;
				if (s[off + 1] > '0' && s[off + 1] <= '9') {
					size_t space = s.find(" ", off);
					size_t comma = s.find(",", off);
					size_t parentesis = s.find(")", off);
					size_t numEnd = min(space, min(comma, parentesis));
					string num = s.substr(off + 1, numEnd - off - 1);
					size_t iNum = atoi(num.c_str());
					if (iNum > lastValue)
						iNum += sizeGap;
					s2 += "%" + to_string(iNum);
					prevOff += 1 + num.size();
				}
			}
			s2 += s.substr(prevOff);

			if (filePos == i) {
				shader.push_back(s2);
				for (size_t j = 0; j < shaderS.size(); j++)
					shader.push_back(shaderS[j]);
			}
			else if (outPos == i) {
				shader.push_back(start + "%" + to_string(lastValue + sizeGap) + end);
				shader.push_back(lines[outPos + 1]);
				shader.push_back(lines[outPos + 2]);
				shader.push_back(lines[outPos + 3]);
				i += 3;
			}
			else {
				shader.push_back(s2);
			}
		}
		lines = shader;
		for (size_t i = 0; i < output.size(); i++) {
			output[i] += rowGap;
		}
	}
	for (size_t i = 0; i < lines.size(); i++) {
		for (size_t j = 0; j < lines[i].size(); j++) {
			shaderOutput.push_back(lines[i][j]);
		}
		shaderOutput.push_back('\n');
	}
	return shaderOutput;
}

vector<UINT8> changeASM9(vector<UINT8> ASM, bool left, float conv, float screenSize, float separation) {
	vector<UINT8> shaderOut;
	string reg((char*)ASM.data(), ASM.size());
	int tempReg = 0;
	for (int i = 0; i < 50; i++) {
		if (reg.find("r" + to_string(i)) == string::npos) {
			tempReg = i;
			break;
		}
	}
	size_t oPos = reg.find("dcl_position o");
	string oReg;
	if (oPos < reg.size()) {
		string regTest = reg.substr(oPos + 15, 1);
		if (regTest == "0" || regTest == "1")
			oReg = reg.substr(oPos + 13, 3);
		else
			oReg = reg.substr(oPos + 13, 2);
	}
	else {
		// no output
		return shaderOut;
	}
	auto lines = stringToLines((char*)ASM.data(), ASM.size());
	string shader;
	bool dcl = false;
	for (size_t i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (s.find("dcl") != string::npos) {
			dcl = true;
			shader += s + "\n";
		}
		else if (dcl == true) {
			// after dcl
			auto pos = s.find(oReg);
			if (pos != string::npos) {
				string sourceReg = "r" + to_string(tempReg);
				for (size_t i = 0; i < s.size(); i++) {
					if (i < pos) {
						shader += s[i];
					}
					else if (i == pos) {
						shader += sourceReg;
					}
					else if (i > pos + oReg.size() - 1) {
						shader += s[i];
					}
				}
				shader += "\n";
			}
			shader += s + "\n";
			if (i == (lines.size() - 3)) {
				string sourceReg = "r" + to_string(tempReg);
				string calcReg = "r" + to_string(tempReg + 1);

				if (gl_Type) {
						if (gl_DepthZ)
							shader += "      add " + calcReg + ".x, " + sourceReg + ".z, c250.x\n";
						else
							shader += "      add " + calcReg + ".x, " + sourceReg + ".w, c250.x\n";
						shader += "      mad " + oReg + ".x, " + calcReg + ".x, c250.y, " + sourceReg + ".x\n";
				}
				else {
					if (gl_DepthZ)
						shader += "    if_ne " + sourceReg + ".z, c250.z\n";
					else
						shader += "    if_ne " + sourceReg + ".w, c250.z\n";
					if (gl_DepthZ)
						shader += "      add " + calcReg + ".x, " + sourceReg + ".z, c250.x\n";
					else
						shader += "      add " + calcReg + ".x, " + sourceReg + ".w, c250.x\n";
					shader += "      mad " + oReg + ".x, " + calcReg + ".x, c250.y, " + sourceReg + ".x\n";
					shader += "    endif\n";
				}
			}
		}
		else {
			// before dcl
			shader += s + "\n";
			if (s.find("vs_") != string::npos) {
				for (size_t j = i + 1; j < lines.size(); j++) {
					string d = lines[j];
					if (d.find("def") == string::npos) {
						float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
						char buf[80];
						sprintf_s(buf, 80, "%.6f", left ? -finalSep : finalSep);
						string sepS(buf);
						sprintf_s(buf, 80, "%.3f", -conv);
						string convS(buf);
						shader += "    def c250, " + convS + ", " + sepS + ", 1.000000, 0\n";
						i = j - 1;
						break;
					}
					shader += d + "\n";
				}
			}
		}
	}
	shaderOut = readV(shader.data(), shader.size());
	return shaderOut;
}

vector<UINT8> changeASM(bool dx9, vector<UINT8> ASM, bool left, float conv, float screenSize, float separation) {
	if (ASM.size() > 0 && ASM[0] == ';')
		return changeDXIL(ASM, left, conv, screenSize, separation);
	if (dx9)
		return changeASM9(ASM, left, conv, screenSize, separation);

	vector<UINT8> shaderOut;
	auto lines = stringToLines((char*)ASM.data(), ASM.size());
	string shader;
	string oReg;
	bool dcl = false;
	bool dcl_ICB = false;
	int temp = 0;
	for (size_t i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (s.find("dcl") == 0) {
			dcl = true;
			dcl_ICB = false;
			if (s.find("dcl_output_siv") == 0 && s.find("position") != string::npos) {
				if (s.substr(17, 1) != ".") {
					oReg = s.substr(15, 3);
				}
				else {
					oReg = s.substr(15, 2);
				}
				shader += s + "\n";
			}
			else if (s.find("dcl_temps") == 0) {
				string num = s.substr(10);
				temp = atoi(num.c_str()) + 2;
				shader += "dcl_temps " + to_string(temp) + "\n";
			}
			else if (s.find("dcl_immediateConstantBuffer") == 0) {
				dcl_ICB = true;
				shader += s + "\n";
			}
			else {
				shader += s + "\n";
			}
		}
		else if (dcl_ICB == true) {
			shader += s + "\n";
		}
		else if (dcl == true) {
			// after dcl
			if (s.find("ret") < s.size()) {
				float finalSep = separation * 0.01f * 6.5f / (2.54f * screenSize * 16 / sqrtf(256 + 81));
				char buf[80];
				sprintf_s(buf, 80, "%.6f", left ? -finalSep : finalSep);
				string sep(buf);
				sprintf_s(buf, 80, "%.3f", -conv);
				string conv(buf);

				string sourceReg = "r" + to_string(temp - 1);
				string calcReg = "r" + to_string(temp - 2);

				if (gl_Type) {
					if (gl_DepthZ)
						shader += "add " + calcReg + ".x, " + sourceReg + ".z, l(" + conv + ")\n";
					else
						shader += "add " + calcReg + ".x, " + sourceReg + ".w, l(" + conv + ")\n";
					shader += "mad " + oReg + ".x, " + calcReg + ".x, l(" + sep + "), " + sourceReg + ".x\n";
				}
				else {
					if (gl_DepthZ)
						shader += "ne " + calcReg + ".x, " + sourceReg + ".z, l(1.000000)\n";
					else
						shader += "ne " + calcReg + ".x, " + sourceReg + ".w, l(1.000000)\n";
					shader += "if_nz " + calcReg + ".x\n";
					if (gl_DepthZ)
						shader += "  add " + calcReg + ".x, " + sourceReg + ".z, l(" + conv + ")\n";
					else
						shader += "  add " + calcReg + ".x, " + sourceReg + ".w, l(" + conv + ")\n";
					shader += "  mad " + oReg + ".x, " + calcReg + ".x, l(" + sep + "), " + sourceReg + ".x\n";
					shader += "endif\n";
				}
			}
			if (oReg.size() == 0) {
				// no output
				return shaderOut;
			}
			if (temp == 0) {
				// add temps
				temp = 2;
				shader += "dcl_temps 2\n";
			}
			shader += s + "\n";
			auto pos = s.find(oReg);
			if (pos != string::npos) {
				string reg = "r" + to_string(temp - 1);
				for (size_t j = 0; j < s.size(); j++) {
					if (j < pos) {
						shader += s[j];
					}
					else if (j == pos) {
						shader += reg;
					}
					else if (j > pos + oReg.size() - 1) {
						shader += s[j];
					}
				}
				shader += "\n";
			}
		}
		else {
			// before dcl
			shader += s + "\n";
		}
	}
	shaderOut = readV(shader.data(), shader.size());
	return shaderOut;
}

static map<string, vector<DWORD>> codeBin;

string convertF(DWORD original, const char* lit) {
	char buf[80];
	vector<DWORD> hex = { 0x7fc00000, 0xffc00000, 0xffff0000, 0x0000ffff, 0x7fffffff,  0xffecc800, 0x7fffff00, 0x7fff7fff, 0x7fffe000,
		0xffaa5501, 0xffaa5500, 0xffc10000, 0x7fc10000, 0xfffeffff, 0xffe699f1, 0xfffe4000, 0x120000, 0x20000, 0x7fffc000, 0xfffe7960,
		0xfffefffe, 0x7f83cdd0, 0xfffe0ce3, 0x00013210, 0x00040000, 0x00080000, 0x000fffff, 0x00010000, 0x00020000, 0x7f8901e6, 0x7fed18f2 };
	bool bHex = false;
	for (size_t i = 0; i < hex.size(); i++) {
		if (original == hex[i]) {
			bHex = true;
			break;
		}
	}
	float fOriginal = reinterpret_cast<float&>(original);
	if (original == 0xFF800000) {
		sprintf_s(buf, 80, "%s", "-1.#INF00");
	}
	else if (original == 0x7F800000) {
		sprintf_s(buf, 80, "%s", "1.#INF00");
	}
	else if (bHex) {
		sprintf_s(buf, 80, "0x%08x", original);
	}
	else if (original < 0xffff) {
		sprintf_s(buf, 80, "%d", original);
	}
	else if (original > 0xffff0000) {
		sprintf_s(buf, 80, "%d", original);
	}
	else {
		sprintf_s(buf, 80, "%.1f", fOriginal);
		string s1(buf);
		if (original == strToDWORD(s1))
			return s1;

		sprintf_s(buf, 80, "%.2f", fOriginal);
		string s2(buf);
		if (original == strToDWORD(s2))
			return s2;

		sprintf_s(buf, 80, "%.3f", fOriginal);
		string s3(buf);
		if (original == strToDWORD(s3))
			return s3;

		sprintf_s(buf, 80, "%.4f", fOriginal);
		string s4(buf);
		if (original == strToDWORD(s4))
			return s4;

		sprintf_s(buf, 80, "%.5f", fOriginal);
		string s5(buf);
		if (original == strToDWORD(s5))
			return s5;

		sprintf_s(buf, 80, "%.6f", fOriginal);
		string s6(buf);
		if (original == strToDWORD(s6))
			return s6;

		sprintf_s(buf, 80, "%.7f", fOriginal);
		string s7(buf);
		if (original == strToDWORD(s7))
			return s7;

		sprintf_s(buf, 80, "%.8f", fOriginal);
		string s8(buf);
		if (original == strToDWORD(s8))
			return s8;

		sprintf_s(buf, 80, "%.9f", fOriginal);
		string s9(buf);
		if (original == strToDWORD(s9))
			return s9;

		sprintf_s(buf, 80, "%.10f", fOriginal);
		string s10(buf);
		if (original == strToDWORD(s10))
			return s10;

		sprintf_s(buf, 80, "%.9E", fOriginal);
		string s9E(buf);
		if (original == strToDWORD(s9E))
			return s9E;
	}
	string sLiteral(buf);

	DWORD newDWORD = strToDWORD(sLiteral);
	if (newDWORD != original) {
		if (failFile == NULL)
			fopen_s(&failFile, "debug.txt", "wb");
		FILE* f = failFile;
		if (f != 0) {
			fprintf(f, "orig: %s <> %08X\n", lit, original);
			fprintf(f, "new:  %s <> %08X\n\n", sLiteral.c_str(), newDWORD);
		}
	}
	return sLiteral;
}

void writeLUT() {
	FILE* f = NULL;
	if (!codeBin.empty())
		fopen_s(&f, "lut.asm", "wb");
	if (f != 0) {
		for (auto it = codeBin.begin(); it != codeBin.end(); ++it) {
			string s = "{ \"" + it->first.substr(0, it->first.length() - 5) + "\", {";
			::fputs(s.c_str(), f);
			vector<DWORD> b = it->second;
			for (DWORD i = 0; i < b.size(); i++) {
				if (i == 0) {
					char hex[20];
					sprintf_s(hex, " 0x%08X", b[i]);
					::fputs(hex, f);
					/*
					char hex[40];
					shader_ins* ins = (shader_ins*)&b[0];
					if (ins->_11_23 > 0) {
						if (ins->extended)
							sprintf_s(hex, "0x%08X: %d,%d,%d<>%d->", b[0], ins->opcode, ins->_11_23, ins->length, ins->extended);
						else
							sprintf_s(hex, "0x%08X: %d,%d,%d->", b[0], ins->opcode, ins->_11_23, ins->length);
					}
					else {
						if (ins->extended)
							sprintf_s(hex, "0x%08X: %d,%d<>%d->", b[0], ins->opcode, ins->length, ins->extended);
						else
							sprintf_s(hex, "0x%08X: %d,%d->", b[0], ins->opcode, ins->length);
					}
					::fputs(hex, f);
					*/
				}
				else {
					char hex[20];
					sprintf_s(hex, ", 0x%08X", b[i]);
					::fputs(hex, f);
				}
			}
			::fputs(" } },\n", f);
			if (it->first.find("orig") != string::npos) {
				::fputs("\n", f);
			}
		}
		fclose(f);
	}
}

string assembleAndCompare(string s, vector<DWORD> v) {
	string s2;
	int numSpaces = 0;
	while (memcmp(s.c_str(), " ", 1) == 0) {
		s.erase(s.begin());
		numSpaces++;
	}
	size_t lastStart = 0;
	size_t lastLiteral = 0;
	size_t lastEnd = 0;
	vector<DWORD> v2 = assembleIns(s);
	string sNew = s;
	string s3;
	string sLiteral;
	string sBegin;
	bool valid = true;
	if (v2.size() > 0) {
		if (v2.size() == v.size()) {
			for (DWORD i = 0; i < v.size(); i++) {
				if (v[i] == 0x1835) {
					int size = v[++i];
					int loopSize = (size - 2) / 4;
					lastLiteral = sNew.find("{ { ");
					for (int j = 0; j < loopSize; j++) {
						i++;
						lastLiteral = sNew.find("{ ", lastLiteral + 1);
						lastEnd = sNew.find(",", lastLiteral);
						s3 = sNew.substr(lastLiteral + 2, lastEnd - lastLiteral - 2);
						sLiteral = convertF(v[i], s3.c_str());
						sBegin = sNew.substr(0, lastLiteral + 1);
						sBegin.append(" " + sLiteral + ",");

						i++;
						lastLiteral = lastEnd;
						lastEnd = sNew.find(",", lastEnd + 1);
						s3 = sNew.substr(lastLiteral + 1, lastEnd - lastLiteral - 1);
						sLiteral = convertF(v[i], s3.c_str());
						sBegin.append(" " + sLiteral + ",");

						i++;
						lastLiteral = lastEnd;
						lastEnd = sNew.find(",", lastEnd + 1);
						s3 = sNew.substr(lastLiteral + 1, lastEnd - lastLiteral - 1);
						sLiteral = convertF(v[i], s3.c_str());
						sBegin.append(" " + sLiteral + ",");

						i++;
						lastLiteral = sNew.find(",", lastLiteral + 1);
						lastEnd = sNew.find("}", lastLiteral + 1);
						s3 = sNew.substr(lastLiteral + 2, lastEnd - lastLiteral - 2);
						sLiteral = convertF(v[i], s3.c_str());
						sBegin.append(" " + sLiteral);
						sBegin.append(sNew.substr(lastEnd));
						sNew = sBegin;
					}
					i++;
				}
				else if (sNew.find("l(", lastStart + 1) != string::npos && v2[i] == 0x4001) {
					i++;
					lastStart = sNew.find("l(", lastStart + 1);
					lastLiteral = lastStart;
					lastEnd = sNew.find(")", lastLiteral);
					s3 = sNew.substr(lastLiteral + 2, lastEnd - lastLiteral - 2);
					sLiteral = convertF(v[i], s3.c_str());
					sBegin = sNew.substr(0, lastLiteral + 2);
					sBegin.append(sLiteral);
					sBegin.append(sNew.substr(lastEnd));
					sNew = sBegin;
				}
				else if (sNew.find("l(", lastStart + 1) != string::npos && v2[i] == 0x4002) {
					i++;
					lastStart = sNew.find("l(", lastStart + 1);
					lastLiteral = lastStart;
					lastEnd = sNew.find(",", lastLiteral);
					s3 = sNew.substr(lastLiteral + 2, lastEnd - lastLiteral - 2);
					sLiteral = convertF(v[i], s3.c_str());
					sBegin = sNew.substr(0, lastLiteral + 2);
					sBegin.append(sLiteral + ", ");

					i++;
					lastLiteral = lastEnd;
					lastEnd = sNew.find(",", lastEnd + 1);
					s3 = sNew.substr(lastLiteral + 1, lastEnd - lastLiteral - 1);
					sLiteral = convertF(v[i], s3.c_str());
					sBegin.append(sLiteral + ", ");

					i++;
					lastLiteral = lastEnd;
					lastEnd = sNew.find(",", lastEnd + 1);
					s3 = sNew.substr(lastLiteral + 1, lastEnd - lastLiteral - 1);
					sLiteral = convertF(v[i], s3.c_str());
					sBegin.append(sLiteral + ", ");

					i++;
					lastLiteral = lastEnd;
					lastEnd = sNew.find(")", lastEnd + 1);
					s3 = sNew.substr(lastLiteral + 1, lastEnd - lastLiteral - 1);
					sLiteral = convertF(v[i], s3.c_str());
					sBegin.append(sLiteral);
					sBegin.append(sNew.substr(lastEnd));
					sNew = sBegin;
				}
				else if (v[i] != v2[i])
					valid = false;
			}
		}
		else {
			valid = false;
		}
		if (valid) {
			s2 = "!success ";
			s2.append(s);
			//codeBin[s2] = v;
		} else {
			s2 = s;
			s2.append(" orig");
			codeBin[s2] = v;
			s2 = s;
			s2.append(" fail");
			codeBin[s2] = v2;
		}
	} else {
		s2 = "!missing ";
		s2.append(s);
		codeBin[s2] = v;
	}
	string ret = "";
	for (int i = 0; i < numSpaces; i++) {
		ret.append(" ");
	}
	ret.append(sNew);
	return ret;
}

vector<UINT8> disassembler(vector<UINT8> buffer) {
	vector<UINT8> ret;
	char* asmBuffer = nullptr;
	size_t asmSize = 0;
	ID3DBlob* pDissassembly;
	HRESULT hr = D3DDisassemble(buffer.data(), buffer.size(), 0, NULL, &pDissassembly);
	if (hr == S_OK) {
		asmBuffer = (char*)pDissassembly->GetBufferPointer();
		asmSize = pDissassembly->GetBufferSize();
	}
	else {
		if (dxc_module == 0)
			dxc_module = ::LoadLibrary(L"dxcompiler2.dll");
		if (dxc_module != 0) {
			DxcCreateInstanceProc dxc_create_func = (DxcCreateInstanceProc)GetProcAddress(dxc_module, "DxcCreateInstance");
			ComPtr<IDxcCompiler3> pCompiler;
			dxc_create_func(CLSID_DxcCompiler, IID_PPV_ARGS(pCompiler.GetAddressOf()));
			DxcBuffer buf = {};
			buf.Encoding = 0;
			buf.Ptr = buffer.data();
			buf.Size = buffer.size();
			ComPtr<IDxcResult> pRes;
			hr = pCompiler->Disassemble(&buf, IID_PPV_ARGS(pRes.GetAddressOf()));
			if (hr == S_OK) {
				ComPtr<IDxcBlob> pBlob;
				pRes->GetResult(pBlob.GetAddressOf());
				return readV(pBlob->GetBufferPointer(), pBlob->GetBufferSize());
			}
		}
		return ret;
	}

	UINT8 fourcc[4];
	DWORD fHash[4];
	DWORD one;
	DWORD fSize;
	DWORD numChunks;
	vector<DWORD> chunkOffsets;

	UINT8* pPosition = buffer.data();
	std::memcpy(fourcc, pPosition, 4);
	if (memcmp(fourcc, "DXBC", 4) != 0) {
		return ret;
	}
	pPosition += 4;
	std::memcpy(fHash, pPosition, 16);
	pPosition += 16;
	one = *(DWORD*)pPosition;
	pPosition += 4;
	fSize = *(DWORD*)pPosition;
	pPosition += 4;
	numChunks = *(DWORD*)pPosition;
	pPosition += 4;
	chunkOffsets.resize(numChunks);
	std::memcpy(chunkOffsets.data(), pPosition, 4 * numChunks);
	UINT8* codeByteStart = 0;
	int codeChunk = 0;

	for (DWORD i = 1; i <= numChunks; i++) {
		codeChunk = numChunks - i;
		codeByteStart = buffer.data() + chunkOffsets[codeChunk];
		if (memcmp(codeByteStart, "SHEX", 4) == 0 || memcmp(codeByteStart, "SHDR", 4) == 0)
			break;
	}

	vector<string> lines = stringToLines(asmBuffer, asmSize);
	DWORD* codeStart = (DWORD*)(codeByteStart + 8);
	bool codeStarted = false;
	bool multiLine = false;
	int multiLines = 0;
	string s2;
	vector<DWORD> o;
	for (DWORD i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (memcmp(s.c_str(), "//", 2) == 0 || memcmp(s.c_str(), "#line", 5) == 0 || s == "") {
			lines[i] = s;
			continue;
		}
		else {
			vector<DWORD> v;
			if (!codeStarted) {
				if (s.size() > 0 && s[0] != ' ') {
					codeStarted = true;
					v.push_back(*codeStart);
					codeStart += 2;
					string sNew = assembleAndCompare(s, v);
					lines[i] = sNew;
				}
				else {
					lines[i] = s;
				}
			}
			else if (s.find("{ {") < s.size()) {
				s2 = s;
				multiLine = true;
				multiLines = 1;
			}
			else if (s.find("} }") < s.size()) {
				s2.append("\n");
				s2.append(s);
				s = s2;
				multiLine = false;
				multiLines++;
				v.push_back(*codeStart);
				codeStart++;
				DWORD length = *codeStart;
				v.push_back(*codeStart);
				codeStart++;
				for (DWORD j = 2; j < length; j++) {
					v.push_back(*codeStart);
					codeStart++;
				}
				string sNew = assembleAndCompare(s, v);
				auto sLines = stringToLines(sNew.c_str(), sNew.size());
				size_t startLine = i - sLines.size() + 1;
				for (size_t j = 0; j < sLines.size(); j++) {
					lines[startLine + j] = sLines[j];
				}
			}
			else if (multiLine) {
				s2.append("\n");
				s2.append(s);
				multiLines++;
			}
			else if (s.size() > 0) {
				shader_ins* ins = (shader_ins*)codeStart;
				v.push_back(*codeStart);
				codeStart++;

				for (DWORD j = 1; j < ins->length; j++) {
					v.push_back(*codeStart);
					codeStart++;
				}
				string sNew = assembleAndCompare(s, v);
				lines[i] = sNew;
			}
		}
	}
	string a = "";
	for (size_t i = 0; i < lines.size(); i++) {
		a += lines[i] + "\n";
	}
	auto v = readV(a.data(), a.length());
	return v;
}

void handleSwizzle(string s, token_operand* tOp, bool special = false) {
	if (special) {
		tOp->sel = 0;
		tOp->mode = 0; // Mask
		if (s.size() > 0 && s[0] == 'x') {
			tOp->sel |= 0x1;
			s.erase(s.begin());
		}
		if (s.size() > 0 && s[0] == 'y') {
			tOp->sel |= 0x2;
			s.erase(s.begin());
		}
		if (s.size() > 0 && s[0] == 'z') {
			tOp->sel |= 0x4;
			s.erase(s.begin());
		}
		if (s.size() > 0 && s[0] == 'w') {
			tOp->sel |= 0x8;
			s.erase(s.begin());
		}
		return;
	}
	if (s.size() == 1) {
		tOp->mode = 2; // Scalar
		if (s[0] == 'x')
			tOp->sel = 0;
		if (s[0] == 'y')
			tOp->sel = 1;
		if (s[0] == 'z')
			tOp->sel = 2;
		if (s[0] == 'w')
			tOp->sel = 3;
		return;
	}
	// Swizzle
	tOp->sel = 0;
	tOp->mode = 1; // Swizzle
	for (size_t i = 0; i < s.size(); i++) {
		if (s[i] == 'x')
			tOp->sel |= 0 << (2 * i);
		if (s[i] == 'y')
			tOp->sel |= 1 << (2 * i);
		if (s[i] == 'z')
			tOp->sel |= 2 << (2 * i);
		if (s[i] == 'w')
			tOp->sel |= 3 << (2 * i);
	}
	return;
}

DWORD strToDWORD(string s) {
	if (s == "-1.#INF00") {
		return 0xFF800000;
	}
	if (s == "1.#INF00") {
		return 0x7F800000;
	}
	if (s.substr(0, 2) == "0x") {
		DWORD decimalValue;
		sscanf_s(s.c_str(), "0x%x", &decimalValue);
		return decimalValue;
	}
	if (s.find('.') < s.size()) {
		float f = (float)atof(s.c_str());
		DWORD* pF = (DWORD*)&f;
		return *pF;
	}
	return atoi(s.c_str());
}

vector<DWORD> assembleOp(string s, bool special = false) {
	vector<DWORD> v;
	DWORD op = 0;
	DWORD ext = 0;
	DWORD num = 0;
	token_operand* tOp = (token_operand*)&op;
	string bPoint;
	num = atoi(s.c_str());
	if (num != 0) {
		v.push_back(num);
		return v;
	}
	if (s[0] == '-') {
		s.erase(s.begin());
		tOp->extended = 1;
		ext |= 0x41;
	}
	if (s[0] == '|') {
		s.erase(s.begin());
		s.erase(s.end() - 1);
		tOp->extended = 1;
		ext |= 0x81;
	}
	size_t pos = s.find(" {min16f}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x4001;
	}
	pos = s.find(" {min16u}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x14001;
	}
	pos = s.find(" {min16i}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x10001;
	}
	pos = s.find(" {min16f as def32}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x4001;
	}
	pos = s.find(" {min16u as def32}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x14001;
	}
	pos = s.find(" {min16u as min16i}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x14001;
	}
	pos = s.find(" {def32 as min16f}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
	}
	pos = s.find(" {def32 as min16u}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
	}
	pos = s.find(" {def32 as min16i}");
	if (pos < s.size()) {
		s = s.substr(0, pos);
	}
	pos = s.find(" { nonuniform }");
	if (pos < s.size()) {
		s = s.substr(0, pos);
		tOp->extended = 1;
		ext |= 0x20001;
	}
	if (tOp->extended) {
		v.push_back(ext);
	}
	tOp->comps_enum = 2;
	if (s == "null") {
		op = 0xD000;
	}
	else if (s == "oDepth") {
		op = 0xC001;
	}
	else if (s == "oDepthLE") {
		op = 0x27001;
	}
	else if (s == "oDepthGE") {
		op = 0x26001;
	}
	else if (s == "vOutputControlPointID") {
		if (special)
			op = 0x16000;
		else
			op = 0x16001;
	}
	else if (s == "oMask") {
		op = 0xF001;
	}
	else if (s == "vPrim") {
		if (special)
			op = 0xB000;
		else
			op = 0xB001;
	}
	else if (s.find("vForkInstanceID") != string::npos) {
		op |= 0x17000;
		if (s == "vForkInstanceID") {
			if (special) {
				tOp->comps_enum = 0;
			}
			else {
				tOp->comps_enum = 1;
			}
		}
		else
			handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vCoverage") != string::npos) {
		op |= 0x23000;
		if (s == "vCoverage")
			if (special) {
				tOp->comps_enum = 1;
			}
			else {
				tOp->comps_enum = 0;
			}
		else
			handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vDomain") != string::npos) {
		op |= 0x1C000;
		handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("rasterizer") != string::npos) {
		op |= 0xE00;
		handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vThreadGroupID") != string::npos) {
		op |= 0x21000;
		handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vThreadIDInGroupFlattened") != string::npos) {
		op |= 0x24000;
		if (s == "vThreadIDInGroupFlattened")
			tOp->comps_enum = 0;
		else
			handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vThreadIDInGroup") != string::npos) {
		op |= 0x22000;
		handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else if (s.find("vThreadID") != string::npos) {
		op |= 0x20000;
		handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
	}
	else {
		if (s[0] == 'U') { s[0] = 'u'; }
		if (s[0] == 'S') { s[0] = 's'; }
		if (s[0] == 'C' && s[1] == 'B') { s[0] = 'c'; s[1] = 'b'; }

		string r(s);

		bool keep = false;

		if (s[0] == 'o') {
			tOp->file = 2;
		} else if (s[0] == 'x') {
			tOp->file = 3;
		} else if (s[0] == 'v') {
			tOp->file = 1;
			if (s.size() > 4 && s[1] == 'i' && s[2] == 'c' && s[3] == 'p') { // vicp
				tOp->file = 0x19;
				s.erase(s.begin());
				s.erase(s.begin());
				s.erase(s.begin());
			} else if (s.size() > 4 && s[1] == 'o' && s[2] == 'c' && s[3] == 'p') { // vocp
				tOp->file = 0x1A;
				s.erase(s.begin());
				s.erase(s.begin());
				s.erase(s.begin());
			} else if (s[1] == 'p' && s[2] == 'c') { // vpc
				tOp->file = 0x1B;
				s.erase(s.begin());
				s.erase(s.begin());
			}
		} else if (s[0] == 'r') {
			tOp->file = 0;
		} else if (s[0] == 's') {
			tOp->file = 6;
		} else if (s[0] == 'T') {
			tOp->file = 7;
			keep = true;
		} else if (s[0] == 't') {
			tOp->file = 7;
		} else if (s[0] == 'g') {
			tOp->file = 0x1F;
		} else if (s[0] == 'u') {
			tOp->file = 0x1E;
		} else if (s[0] == 'm') {
			tOp->file = 0x10;
		} else if (s[0] == 'l') {
			tOp->file = 4;
		} else if (s[0] == 'i') {
			tOp->file = 9;
			s.erase(s.begin());
			s.erase(s.begin());
		} else if (s[0] == 'c') {
			tOp->file = 8;
			s.erase(s.begin());
		}
		s.erase(s.begin());

		tOp->num_indices = 1;
		tOp->comps_enum = 2;

		if (s.find("[") < s.size()) {
			size_t pos = s.find("[");
			string sNum = s.substr(0, pos);
			if (sNum.size() > 0) {
				int num = atoi(sNum.c_str());
				v.push_back(num);
				tOp->num_indices++;
			}
			tOp->sel = 0xE4;
			tOp->mode = 1;
			size_t pos2 = s.find_first_of("]");
			string index = s.substr(pos + 1, pos2 - pos - 1);
			if (index.find(":") < index.size()) {
				tOp->num_indices++;
				size_t colon = index.find(":");
				string idx1 = index.substr(0, colon);
				string idx2 = index.substr(colon + 1);

				int iIdx = atoi(idx1.c_str());
				v.push_back(iIdx);

				if (idx2 == "*") {
					v.push_back(0xFFFFFFFF);
				}
				else {
					int iIdx2 = atoi(idx2.c_str());
					v.push_back(iIdx2);
				}
			}
			else if (index.find("+") < index.size()) {
				size_t plusPos = index.find("+");
				string sOp = index.substr(0, plusPos - 1);
				string sAdd = index.substr(plusPos + 2);
				int iAdd = atoi(sAdd.c_str());
				vector<DWORD> reg = assembleOp(sOp);
				if (sNum.size() == 0) {

					if (iAdd) {
						v.push_back(iAdd);
						tOp->index0_repr = 3;
					}
					else {
						tOp->index0_repr = 2;
					}
				}
				else {
					if (iAdd) {
						v.push_back(iAdd);
						tOp->index1_repr = 3;
					}
					else {
						tOp->index1_repr = 2;
					}
				}
				v.push_back(reg[0]);
				v.push_back(reg[1]);
			}
			else {
				int iIdx = atoi(index.c_str());
				v.push_back(iIdx);
			}
			if (s.find("][") < s.size()) {
				if (tOp->num_indices < 3)
					tOp->num_indices++;
				size_t pos4 = s.find("][");
				size_t pos5 = s.find_last_of("]");
				string index2 = s.substr(pos4 + 2, pos5 - pos4 - 2);
				if (index2.find("+") < index2.size()) {
					size_t plusPos = index2.find("+");
					string sOp = index2.substr(0, plusPos - 1);
					string sAdd = index2.substr(plusPos + 2);
					int iAdd = atoi(sAdd.c_str());
					vector<DWORD> reg = assembleOp(sOp);
					if (sNum.size() == 0) {
						if (iAdd) {
							tOp->num_indices = 2;
							v.push_back(iAdd);
							tOp->index1_repr = 3;
						}
						else {
							tOp->index1_repr = 2;
						}
					}
					else {
						tOp->num_indices = 3;
						if (iAdd) {
							v.push_back(iAdd);
							tOp->index2_repr = 3;
						}
						else {
							tOp->index2_repr = 2;
						}
					}
					v.push_back(reg[0]);
					v.push_back(reg[1]);
				}
				else {
					int iIdx = atoi(index2.c_str());
					v.push_back(iIdx);
				}
			}
			if (s.find("].") < s.size()) {
				size_t pos = s.find("].");
				string swizzle = s.substr(pos + 2);
				handleSwizzle(swizzle, tOp, special);
			}
			else {
				if (!keep) {
					tOp->comps_enum = 0;
					tOp->mode = 0;
					tOp->sel = 0;
				}
				if (special) {
					tOp->comps_enum = 2;
					handleSwizzle("xyzw", tOp, false);
				}
			}
		}
		else if (s[0] == '(') {
			tOp->num_indices = 0;
			tOp->mode = 0;
			tOp->comps_enum = 1;
			if (s.find(",") < s.size()) {
				tOp->comps_enum = 2;
				s.erase(s.begin());
				string s1 = s.substr(0, s.find(","));
				s = s.substr(s.find(",") + 1);
				if (s[0] == ' ')
					s.erase(s.begin());
				string s2 = s.substr(0, s.find(","));
				s = s.substr(s.find(",") + 1);
				if (s[0] == ' ')
					s.erase(s.begin());
				string s3 = s.substr(0, s.find(","));
				s = s.substr(s.find(",") + 1);
				if (s[0] == ' ')
					s.erase(s.begin());
				string s4 = s.substr(0, s.find(")"));
				v.push_back(strToDWORD(s1));
				v.push_back(strToDWORD(s2));
				v.push_back(strToDWORD(s3));
				v.push_back(strToDWORD(s4));
			}
			else {
				s.erase(s.begin());
				s.pop_back();
				v.push_back(strToDWORD(s));
			}
		}
		else if (s.find(".") < s.size()) {
			num = atoi(s.substr(0, s.find('.')).c_str());
			v.push_back(num);
			handleSwizzle(s.substr(s.find('.') + 1), tOp, special);
		}
		else {
			num = atoi(s.c_str());
			v.push_back(num);
			tOp->comps_enum = 0;
		}
	}

	v.insert(v.begin(), op);
	return v;
}

vector<string> strToWords(string s) {
	vector<string> words;
	string::size_type start = 0;
	while (s[start] == ' ') start++;
	string::size_type end = start;
	while (end < s.size() && s[end] != ' ' && s[end] != '(')
		end++;
	words.push_back(s.substr(start, end - start));
	while (s.size() > end) {
		if (s[end] == ' ') {
			start = ++end;
		} else {
			start = end;
		}
		while (s[start] == ' ') start++;
		if (start >= s.size())
			break;
		if (s[start] == '(' || s[start + 1] == '(') {
			end = s.find(')', start) + 1;
			if (end < s.size() && s[end] == ',')
				end++;
		} else {
			end = s.find(' ', start);
			if (s[end + 1] == '+') {
				end = s.find(' ', end + 3);
				if (s.size() > end && s[end + 1] == '+') {
					end = s.find(' ', end + 3);
				}
			}
		}
		if (end == string::npos) {
			words.push_back(s.substr(start));
		} else {
			string::size_type length = end - start;
			words.push_back(s.substr(start, length));
		}
	}
	for (size_t i = 0; i < words.size(); i++) {
		string s2 = words[i];
		if (s2[s2.size() - 1] == ',')
			s2.erase(--s2.end());
		words[i] = s2;
	}
	return words;
}

DWORD parseAoffimmi(DWORD start, string o) {
	string nums = o.substr(1, o.size() - 2);
	int n1 = atoi(nums.substr(0, nums.find(',')).c_str());
	nums = nums.substr(nums.find(',') + 1);
	int n2 = atoi(nums.substr(0, nums.find(',')).c_str());
	int n3 = atoi(nums.substr(nums.find(',') + 1).c_str());
	DWORD aoffimmi = start;
	aoffimmi |= (n1 & 0xF) << 9;
	aoffimmi |= (n2 & 0xF) << 13;
	aoffimmi |= (n3 & 0xF) << 17;
	return aoffimmi;
}

map<string, vector<DWORD>> hackMap = {
	{ "dcl_output oMask", { 0x02000065, 0x0000F000 } },
};

map<string, vector<int>> ldMap = {
	{ "gather4_c_aoffimmi", { 5, 126, 1 } },
	{ "gather4_c_aoffimmi_indexable", { 5, 126, 3 } },
	{ "gather4_c_indexable", { 5, 126, 2 } },
	{ "gather4_aoffimmi", { 4, 109, 1 } },
	{ "gather4_aoffimmi_indexable", { 4, 109, 3 } },
	{ "gather4_indexable", { 4, 109, 2 } },
	{ "gather4_po_c", { 6, 128, 0 } },
	{ "gather4_po_c_indexable", { 6, 128, 2 } },
	{ "gather4_po", { 5, 127, 0 } },
	{ "gather4_po_indexable", { 5, 127, 2 } },
	{ "ld_aoffimmi", { 3, 45, 1 } },
	{ "ld_aoffimmi_indexable", { 3, 45, 3 }},
	{ "ld_indexable", { 3, 45, 2 } },
	{ "ld_raw_indexable", { 3, 165, 2 } },
	{ "ldms_indexable", { 4, 46, 2 } },
	{ "ldms_aoffimmi", { 4, 46, 1 } },
	{ "ldms_aoffimmi_indexable", { 4, 46, 3 } },
	{ "sample_aoffimmi", { 4, 69, 1 } },
	{ "sample_d_indexable", { 6, 73, 2 } },
	{ "sample_b_indexable", { 5, 74, 2 } },
	{ "sample_c_indexable", { 5, 70, 2 } },
	{ "sample_c_aoffimmi", { 5, 70, 1 } },
	{ "sample_c_lz_indexable", { 5, 71, 2 } },
	{ "sample_c_lz_aoffimmi", { 5, 71, 1 } },
	{ "sample_c_lz_aoffimmi_indexable", { 5, 71, 3 } },
	{ "sample_indexable", { 4, 69, 2 } },
	{ "sample_aoffimmi_indexable", { 4, 69, 3 } },
	{ "sample_l_aoffimmi", { 5, 72, 1 } },
	{ "sample_l_aoffimmi_indexable", { 5, 72, 3 } },
	{ "sample_l_indexable", { 5, 72, 2 } },
	{ "resinfo_indexable", { 3, 61, 2 } },
	{ "ld_structured_indexable", { 4, 167, 2 } },
	{ "ld_uav_typed_indexable", { 3, 163, 2 } },
	{ "bufinfo_indexable", { 2, 121, 2 } },
};

map<string, vector<int>> insMap = {
	{ "gather4_c", { 5, 126 } },
	{ "sample_b", { 5, 74 } },
	{ "sample_c", { 5, 70 } },
	{ "sample_d", { 6, 73 } },
	{ "sample_c_lz", { 5, 71 } },
	{ "sample_l", { 5, 72 } },
	{ "ld_uav_typed", { 3, 163 } },
	{ "eval_sample_index", { 3, 204 } },
	{ "bfi", { 5, 140 } },
	{ "swapc", { 5, 142, 2 } },
	{ "imad", { 4, 35 } },
	{ "imul", { 4, 38, 2 } },
	{ "ldms", { 4, 46 } },
	{ "mad", { 4, 50 } },
	{ "movc", { 4, 55 } },
	{ "sample", { 4, 69 } },
	{ "sampled", { 6, 73 } },
	{ "gather4", { 4, 109 } },
	{ "udiv", { 4, 78, 2 } },
	{ "umul", { 4, 81 } },
	{ "umax", { 3, 83 } },
	{ "ubfe", { 4, 138 } },
	{ "store_structured", { 4, 168 } },
	{ "ld_structured", { 4, 167 } },
	{ "add", { 3, 0 } },
	{ "and", { 3, 1 } },
	{ "div", { 3, 14 } },
	{ "dp2", { 3, 15 } },
	{ "dp3", { 3, 16 } },
	{ "dp4", { 3, 17 } },
	{ "eq", { 3, 24 } },
	{ "ge", { 3, 29 } },
	{ "iadd", { 3, 30 } },
	{ "ieq", { 3, 32 } },
	{ "ige", { 3, 33 } },
	{ "ilt", { 3, 34 } },
	{ "imax", { 3, 36 } },
	{ "imin", { 3, 37 } },
	{ "ine", { 3, 39 } },
	{ "ishl", { 3, 41 } },
	{ "ishr", { 3, 42 } },
	{ "ld", { 3, 45 } },
	{ "lt", { 3, 49 } },
	{ "min", { 3, 51 } },
	{ "max", { 3, 52 } },
	{ "mul", { 3, 56 } },
	{ "ne", { 3, 57 } },
	{ "or", { 3, 60 } },
	{ "resinfo", { 3, 61 } },
	{ "sincos", { 3, 77, 2 } },
	{ "ult", { 3, 79 } },
	{ "uge", { 3, 80 } },
	{ "umin", { 3, 84 } },
	{ "ushr", { 3, 85 } },
	{ "xor", { 3, 87 } },
	{ "bfrev", { 2, 141 } },
	{ "countbits", { 2, 134 } },
	{ "deriv_rtx", { 2, 11 } },
	{ "deriv_rtx_coarse", { 2, 122 } },
	{ "deriv_rtx_fine", { 2, 123 } },
	{ "deriv_rty", { 2, 12 } },
	{ "deriv_rty_coarse", { 2, 124 } },
	{ "deriv_rty_fine", { 2, 125 } },
	{ "exp", { 2, 25 } },
	{ "frc", { 2, 26 } },
	{ "ftoi", { 2, 27 } },
	{ "ftou", { 2, 28 } },
	{ "ineg", { 2, 40 } },
	{ "itof", { 2, 43 } },
	{ "log", { 2, 47 } },
	{ "mov", { 2, 54 } },
	{ "not", { 2, 59 } },
	{ "round_ne", { 2, 64 } },
	{ "round_ni", { 2, 65 } },
	{ "round_pi", { 2, 66 } },
	{ "round_z", { 2, 67 } },
	{ "round_nz", { 2, 67 } },
	{ "rsq", { 2, 68 } },
	{ "sqrt", { 2, 75 } },
	{ "utof", { 2, 86 } },
	{ "rcp", { 2, 129 } },
	{ "sampleinfo", { 2, 111 } },
	{ "f16tof32", { 2, 131 } },
	{ "f32tof16", { 2, 130 } },
	{ "imm_atomic_alloc", { 2, 178 } },
	{ "breakc_z", { 1, 3, 0 } },
	{ "breakc_nz", { 1, 3, 0 } },
	{ "case", { 1, 6 } },
	{ "discard_z", { 1, 13, 0 } },
	{ "discard_nz", { 1, 13, 0 } },
	{ "if_z", { 1, 31, 0 } },
	{ "if_nz", { 1, 31, 0 } },
	{ "switch", { 1, 76, 0 } },
	{ "continuec_z", { 1, 8, 0 } },
	{ "continuec_nz", { 1, 8, 0 } },
	{ "retc_nz", { 1, 63, 0 } },
	{ "retc_z", { 1, 63 } },
	{ "imm_atomic_or", { 4, 182 } },
	{ "imm_atomic_and", { 4, 181 } },
	{ "imm_atomic_exch", { 4, 184 } },
	{ "imm_atomic_cmp_exch", { 5, 185 } },
	{ "imm_atomic_iadd", { 4, 180 } },
	{ "imm_atomic_consume", { 2, 179 } },
	{ "imm_atomic_umax", { 4, 188 } },
	{ "imm_atomic_umin", { 4, 189 } },
	{ "atomic_iadd", { 3, 173, 0 } },
	{ "ld_raw", { 3, 165 } },
	{ "store_raw", { 3, 166 } },
	{ "atomic_imax", { 3, 174, 0 } },
	{ "atomic_imin", { 3, 175, 0 } },
	{ "atomic_umax", { 3, 176, 0 } },
	{ "atomic_umin", { 3, 177, 0 } },
	{ "atomic_or", { 3, 170, 0 } },
	{ "atomic_and", { 3, 169, 0 } },
	{ "atomic_cmp_store", { 4, 172 } },
	{ "dcl_tgsm_raw", { 2, 159 } },
	{ "dcl_tgsm_structured", { 3, 160 } },
	{ "dcl_thread_group", { 3, 155 } },
	{ "firstbit_lo", { 2, 136 } },
	{ "firstbit_hi", { 2, 135 } },
	{ "firstbit_shi", { 2, 137 } },
	{ "ibfe", { 4, 139 } },
	{ "lod", { 4, 108 } },
	{ "samplepos", { 3, 110 } },
	{ "bufinfo", { 2, 121 } },
	{ "store_uav_typed", { 3, 164 } },
	{ "dcl_output", { 1, 101 } },
	{ "dcl_input", { 1, 95 } },
};

map<string, int> shaderMap = {
	{ "ps_", 0x00000 },
	{ "vs_", 0x10000 },
	{ "gs_", 0x20000 },
	{ "hs_", 0x30000 },
	{ "ds_", 0x40000 },
	{ "cs_", 0x50000 },
};

map<string, vector<int>> miniInsMap = {
	{ "sync_uglobal_g_t", { 190, 11 } },
	{ "sync_uglobal_t", { 190, 9 } },
	{ "sync_uglobal", { 190, 8 } },
	{ "sync_g_t", { 190, 3 } },
	{ "sync_g", { 190, 2 } },
	{ "sync_t", { 190, 1 } },
	{ "hs_decls", { 113 } },
	{ "hs_control_point_phase", { 114 } },
	{ "hs_fork_phase", { 115 } },
	{ "hs_join_phase", { 116 } },
	{ "break", { 2 } },
	{ "default", { 10 } },
	{ "else", { 18 } },
	{ "endif", { 21 } },
	{ "endloop", { 22 } },
	{ "endswitch", { 23 } },
	{ "loop", { 48 } },
	{ "ret", { 62 } },
	{ "nop",{ 58 } },
	{ "cut", { 9 } },
	{ "emit", { 19 } },
	{ "continue", { 7 } },
};

DWORD toLD(string s) {
	if (s == "(float,float,float,float)")
		return 0x5555;
	if (s == "(uint,uint,uint,uint)")
		return 0x4444;
	if (s == "(sint,sint,sint,sint)")
		return 0x3333;
	if (s == "(snorm,snorm,snorm,snorm)")
		return 0x2222;
	if (s == "(unorm,unorm,unorm,unorm)")
		return 0x1111;
	return 0;
}

DWORD ldFlag(string s) {
	if (s == "refactoringAllowed")
		return 0x1;
	if (s == "enableDoublePrecisionFloatOps")
		return 0x2;
	if (s == "forceEarlyDepthStencil")
		return 0x4;
	if (s == "enableRawAndStructuredBuffers")
		return 0x8;
	if (s == "skipOptimization")
		return 0x10;
	if (s == "enableMinimumPrecision")
		return 0x20;
	if (s == "enable11_1DoubleExtensions")
		return 0x40;
	if (s == "enable11_1ShaderExtensions")
		return 0x80;
	if (s == "allResourcesBound")
		return 0x100;
	return 0;
}

vector<DWORD> assembleIns(string s) {
	if (hackMap.find(s) != hackMap.end())
		return hackMap[s];
	DWORD op = 0;
	shader_ins* ins = (shader_ins*)&op;
	size_t pos = s.find("[precise");
	if (pos != string::npos) {
		size_t endPos = s.find("]", pos) + 1;
		string precise = s.substr(pos, endPos - pos);
		s.erase(pos, endPos - pos);
		int x = 0;
		int y = 0;
		int z = 0;
		int w = 0;
		if (precise == "[precise]") {
			x = 256;
			y = 512;
			z = 1024;
			w = 2048;
		}
		if (precise.find("x") != string::npos)
			x = 256;
		if (precise.find("y") != string::npos)
			y = 512;
		if (precise.find("z") != string::npos)
			z = 1024;
		if (precise.find("w") != string::npos)
			w = 2048;
		ins->_11_23 = x | y | z | w;
	}
	pos = s.find("_uint");
	if (pos != string::npos) {
		s.erase(pos, 5);
		ins->_11_23 = 2;
	}
	vector<DWORD> v;
	vector<string> w = strToWords(s);
	string o = w[0];
	if (o == "sampleinfo" && ins->_11_23 == 2)
		ins->_11_23 = 1;
	if (s.find("_opc") < s.size()) {
		o = o.substr(0, o.find("_opc"));
		ins->_11_23 = 4096;
	}
	bool bNZ = o.find("_nz") < o.size();
	bool bZ = o.find("_z") < o.size();
	bool bSat = o.find("_sat") < o.size();
	if (bSat) o = o.substr(0, o.find("_sat"));


	if (miniInsMap.find(o) != miniInsMap.end()) {
		vector<int> vIns = miniInsMap[o];
		ins->opcode = vIns[0];
		ins->_11_23 = vIns.size() > 1 ? vIns[1] : 0;
		ins->length = 1;
		v.push_back(op);
	} else if (shaderMap.find(o.substr(0, 3)) != shaderMap.end()) {
		int type = shaderMap[o.substr(0, 3)];
		op = type;
		op |= 16 * atoi(o.substr(3, 1).c_str());
		op |= atoi(o.substr(5, 1).c_str());
		v.push_back(op);
	} else if (insMap.find(o) != insMap.end()) {
		vector<int> vIns = insMap[o];
		int numOps = vIns[0];
		ins->opcode = vIns[1];
		vector<vector<DWORD>> Os;
		int numSpecial = 1;
		if (vIns.size() == 3) {
			numSpecial = vIns[2];
		}
		size_t offset = 0;
		for (int i = 0; i < numOps; i++) {
			size_t offsetSpace = i + 1 + offset;
			string sOp = w[offsetSpace];

			if (offsetSpace + 1 < w.size()) {
				if (w[offsetSpace + 1] == "{min16f}") {
					offset += 1;
					sOp += " {min16f}";
				}
				if (w[offsetSpace + 1] == "{min16f}|") {
					offset += 1;
					sOp += " {min16f}|";
				}
				if (w[offsetSpace + 1] == "{min16u}") {
					offset += 1;
					sOp += " {min16u}";
				}
				if (w[offsetSpace + 1] == "{min16i}") {
					offset += 1;
					sOp += " {min16i}";
				}
			}
			if (offsetSpace + 3 < w.size()) {
				if (w[offsetSpace + 1] == "{" &&
					w[offsetSpace + 2] == "nonuniform" &&
					w[offsetSpace + 3] == "}") {
					offset += 3;
					sOp += " { nonuniform }";
				}
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16f}") {
					offset += 3;
					sOp += " {def32 as min16f}";
				}
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16f}|") {
					offset += 3;
					sOp += " {def32 as min16f}|";
				}
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16u}") {
					offset += 3;
					sOp += " {def32 as min16u}";
				}
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16u}|") {
					offset += 3;
					sOp += " {def32 as min16u}|";
				}
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16i}") {
					offset += 3;
					sOp += " {def32 as min16i}";
				}
				if (w[offsetSpace + 1] == "{min16f" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "def32}") {
					offset += 3;
					sOp += " {min16f as def32}";
				}
				if (w[offsetSpace + 1] == "{min16f" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "def32}|") {
					offset += 3;
					sOp += " {min16f as def32}|";
				}
				if (w[offsetSpace + 1] == "{min16u" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "def32}") {
					offset += 3;
					sOp += " {min16u as def32}";
				}
				if (w[offsetSpace + 1] == "{min16u" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "def32}|") {
					offset += 3;
					sOp += " {min16u as def32}|";
				}
				if (w[offsetSpace + 1] == "{min16u" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16i}") {
					offset += 3;
					sOp += " {min16u as min16i}";
				}
			}
			Os.push_back(assembleOp(sOp, i < numSpecial));
		}
		if (bSat)
			ins->_11_23 |= 4;
		if (bNZ)
			ins->_11_23 |= 128;
		if (bZ)
			ins->_11_23 |= 0;
		ins->length = 1;
		for (size_t i = 0; i < Os.size(); i++)
			ins->length += (UINT)Os[i].size();
		if (o == "samplepos")
			ins->length++;
		v.push_back(op);
		for (size_t i = 0; i < Os.size(); i++)
			v.insert(v.end(), Os[i].begin(), Os[i].end());
		if (o == "samplepos")
			v.push_back(0);
	}
	else if (ldMap.find(o) != ldMap.end()) {
		vector<int> vIns = ldMap[o];
		int numOps = vIns[0];
		vector<vector<DWORD>> Os;
		int startPos = 1 + (vIns[2] & 3);
		int numSpecial = 1;
		int offset = 0;
		for (int i = 0; i < numOps; i++) {
			size_t offsetSpace = i + startPos + offset;
			string sOp = w[offsetSpace];
			if (offsetSpace + 1 < w.size()) {
				if (w[offsetSpace + 1] == "{min16f}") {
					offset += 1;
					sOp += " {min16f}";
				}
				if (w[offsetSpace + 1] == "{min16u}") {
					offset += 1;
					sOp += " {min16u}";
				}
				if (w[offsetSpace + 1] == "{min16i}") {
					offset += 1;
					sOp += " {min16i}";
				}
			}
			if (offsetSpace + 3 < w.size()) {
				if (w[offsetSpace + 1] == "{def32" &&
					w[offsetSpace + 2] == "as" &&
					w[offsetSpace + 3] == "min16u}") {
					offset += 3;
					sOp += " {def32 as min16u}";
				}
			}
			Os.push_back(assembleOp(sOp, i < numSpecial));
		}
		ins->opcode = vIns[1];
		ins->length = 1 + (vIns[2] & 3);
		if (vIns[2] != 0)
			ins->extended = 1;
		for (int i = 0; i < numOps; i++)
			ins->length += (UINT)Os[i].size();
		v.push_back(op);
		if (vIns[2] == 3)
			v.push_back(parseAoffimmi(0x80000001, w[1]));
		if (vIns[2] == 1)
			v.push_back(parseAoffimmi(1, w[1]));
		if (vIns[2] & 2) {
			int c = 1;
			if (vIns[2] == 3)
				c = 2;
			if (w[c] == "(texture1d)")
				v.push_back(0x80000082);
			if (w[c] == "(texture1darray)")
				v.push_back(0x800001C2);
			if (w[c] == "(texture2d)")
				v.push_back(0x800000C2);
			if (w[c] == "(texture2dms)")
				v.push_back(0x80000102);
			if (w[c] == "(texture2dmsarray)")
				v.push_back(0x80000242);
			if (w[c] == "(texture3d)")
				v.push_back(0x80000142);
			if (w[c] == "(texture2darray)")
				v.push_back(0x80000202);
			if (w[c] == "(texturecube)")
				v.push_back(0x80000182);
			if (w[c] == "(texturecubearray)")
				v.push_back(0x80000282);
			if (w[c] == "(buffer)")
				v.push_back(0x80000042);
			if (w[c] == "(raw_buffer)")
				v.push_back(0x800002C2);
			if (w[1].find("stride") != string::npos) {
				string stride = w[1].substr(27);
				stride = stride.substr(0, stride.size() - 1);
				DWORD d = 0x80000302;
				d += atoi(stride.c_str()) << 11;
				v.push_back(d);
			}
			if (w[startPos - 1] == "(float,float,float,float)")
				v.push_back(0x00155543);
			if (w[startPos - 1] == "(uint,uint,uint,uint)")
				v.push_back(0x00111103);
			if (w[startPos - 1] == "(sint,sint,sint,sint)")
				v.push_back(0x000CCCC3);
			if (w[startPos - 1] == "(mixed,mixed,mixed,mixed)")
				v.push_back(0x00199983);
			if (w[startPos - 1] == "(snorm,snorm,snorm,snorm)")
				v.push_back(0x00088883);
			if (w[startPos - 1] == "(unorm,unorm,unorm,unorm)")
				v.push_back(0x00044443);
		}
		for (int i = 0; i < numOps; i++)
			v.insert(v.end(), Os[i].begin(), Os[i].end());
	} else if (o == "dcl_uav_raw") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 157;
		v.insert(v.end(), os.begin(), os.end());
		if (w.size() > 2) {
			if (w[2].find("space=") == 0)
				v.push_back(atoi(w[2].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_raw_glc") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 157;
		v.insert(v.end(), os.begin(), os.end());
		ins->length = 1 + v.size();
		ins->_11_23 = 32;
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_structured") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 158;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(atoi(w[2].c_str()));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_structured_glc") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 158;
		ins->_11_23 = 32;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(atoi(w[2].c_str()));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_structured_rov") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 158;
		ins->_11_23 = 64;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(atoi(w[2].c_str()));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_raw") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 161;
		v.insert(v.end(), os.begin(), os.end());
		if (w.size() > 2) {
			if (w[2].find("space=") == 0)
				v.push_back(atoi(w[2].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_buffer") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 1;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture1d") {
		vector<DWORD> os = assembleOp(w[2]);
		ins->opcode = 88;
		ins->_11_23 = 2;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture1darray") {
		vector<DWORD> os = assembleOp(w[2]);
		ins->opcode = 88;
		ins->_11_23 = 7;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture1d") {
		vector<DWORD> os = assembleOp(w[2]);
		ins->opcode = 156;
		ins->_11_23 = 2;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture2d") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 3;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_buffer") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 1;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture3d") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 5;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture3d") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 5;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texturecube") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 6;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texturecubearray") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 10;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture2darray") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 88;
		ins->_11_23 = 8;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture2d") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 3;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture2d_glc") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 35;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture2d_rov") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 67;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture1darray") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 7;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture2darray") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 8;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_uav_typed_texture2darray_glc") {
		vector<DWORD> os = assembleOp(w[2], true);
		ins->opcode = 156;
		ins->_11_23 = 40;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[1]));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture2dms") {
		vector<DWORD> os = assembleOp(w[3], true);
		ins->opcode = 88;
		if (w[1] == "(0)")
			ins->_11_23 = 4;
		if (w[1] == "(2)")
			ins->_11_23 = 68;
		if (w[1] == "(4)")
			ins->_11_23 = 132;
		if (w[1] == "(6)")
			ins->_11_23 = 196;
		if (w[1] == "(8)")
			ins->_11_23 = 260;
		if (w[1] == "(16)")
			ins->_11_23 = 516;
		if (w[1] == "(32)")
			ins->_11_23 = 1028;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[2]));
		if (w.size() > 4) {
			if (w[4].find("space=") == 0)
				v.push_back(atoi(w[4].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_texture2dmsarray") {
		vector<DWORD> os = assembleOp(w[3], true);
		ins->opcode = 88;
		if (w[1] == "(0)")
			ins->_11_23 = 9;
		if (w[1] == "(2)")
			ins->_11_23 = 73;
		if (w[1] == "(4)")
			ins->_11_23 = 137;
		if (w[1] == "(8)")
			ins->_11_23 = 265;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(toLD(w[2]));
		if (w.size() > 4) {
			if (w[4].find("space=") == 0)
				v.push_back(atoi(w[4].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_indexrange") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 91;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(atoi(w[2].c_str()));
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_temps") {
		ins->opcode = 104;
		v.push_back(atoi(w[1].c_str()));
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_resource_structured") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 162;
		v.insert(v.end(), os.begin(), os.end());
		v.push_back(atoi(w[2].c_str()));
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				v.push_back(atoi(w[3].substr(6).c_str()));
		}
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_sampler") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 90;
		if (w.size() > 2) {
			if (w[2] == "mode_default") {
				ins->_11_23 = 0;
			} else if (w[2] == "mode_comparison") {
				ins->_11_23 = 1;
			}
		}
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				os.push_back(atoi(w[3].substr(6).c_str()));
		}
		v.insert(v.end(), os.begin(), os.end());
		ins->length = 1 + v.size();
		v.insert(v.begin(), op);
	} else if (o == "dcl_globalFlags") {
		ins->opcode = 106;
		ins->length = 1;
		ins->_11_23 = 0;
		if (w.size() > 1)
			ins->_11_23 |= ldFlag(w[1]);
		if (w.size() > 3)
			ins->_11_23 |= ldFlag(w[3]);
		if (w.size() > 5)
			ins->_11_23 |= ldFlag(w[5]);
		if (w.size() > 7)
			ins->_11_23 |= ldFlag(w[7]);
		if (w.size() > 9)
			ins->_11_23 |= ldFlag(w[9]);
		v.push_back(op);
	} else if (o == "dcl_constantbuffer") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 89;
		if (w.size() > 2) {
			if (w[2] == "dynamicIndexed")
				ins->_11_23 = 1;
			else if (w[2] == "immediateIndexed")
				ins->_11_23 = 0;
		}
		if (w.size() > 3) {
			if (w[3].find("space=") == 0)
				os.push_back(atoi(w[3].substr(6).c_str()));
		}

		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_output_siv") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 103;
		if (w[2] == "position")
			os.push_back(1);
		else if (w[2] == "clip_distance")
			os.push_back(2);
		else if (w[2] == "cull_distance")
			os.push_back(3);
		else if (w[2] == "rendertarget_array_index")
			os.push_back(4);
		else if (w[2] == "viewport_array_index")
			os.push_back(5);
		else if (w[2] == "finalQuadUeq0EdgeTessFactor")
			os.push_back(11);
		else if (w[2] == "finalQuadVeq0EdgeTessFactor")
			os.push_back(12);
		else if (w[2] == "finalQuadUeq1EdgeTessFactor")
			os.push_back(13);
		else if (w[2] == "finalQuadVeq1EdgeTessFactor")
			os.push_back(14);
		else if (w[2] == "finalQuadUInsideTessFactor")
			os.push_back(15);
		else if (w[2] == "finalQuadVInsideTessFactor")
			os.push_back(16);
		else if (w[2] == "finalTriUeq0EdgeTessFactor")
			os.push_back(17);
		else if (w[2] == "finalTriVeq0EdgeTessFactor")
			os.push_back(18);
		else if (w[2] == "finalTriWeq0EdgeTessFactor")
			os.push_back(19);
		else if (w[2] == "finalTriInsideTessFactor")
			os.push_back(20);
		else if (w[2] == "finalLineDetailTessFactor")
			os.push_back(21);
		else if (w[2] == "finalLineDensityTessFactor")
			os.push_back(22);
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_input_siv") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 97;
		if (w[2] == "position")
			os.push_back(1);
		else if (w[2] == "clip_distance")
			os.push_back(2);
		else if (w[2] == "cull_distance")
			os.push_back(3);
		else if (w[2] == "finalLineDetailTessFactor")
			os.push_back(0x15);
		else if (w[2] == "finalLineDensityTessFactor")
			os.push_back(0x16);
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_input_sgv") {
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 96;
		if (w[2] == "vertex_id")
			os.push_back(6);
		if (w[2] == "instance_id")
			os.push_back(8);
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_input_ps") {
		vector<DWORD> os;
		ins->opcode = 98;
		if (w[1] == "linear") {
			if (w[2] == "noperspective") {
				if (w[3] == "sample") {
					ins->_11_23 = 7;
					os = assembleOp(w[4], true);
				} else {
					ins->_11_23 = 4;
					os = assembleOp(w[3], true);
				}
			} else if (w[2] == "centroid") {
				ins->_11_23 = 3;
				os = assembleOp(w[3], true);
			} else if (w[2] == "sample") {
				ins->_11_23 = 6;
				os = assembleOp(w[3], true);
			} else {
				ins->_11_23 = 2;
				if (w.size() > 3) {
					os = assembleOp(w[2] + " " + w[3], true);
				} else {
					os = assembleOp(w[2], true);
				}
			}
		}
		if (w[1] == "constant") {
			ins->_11_23 = 1;
			os = assembleOp(w[2], true);
		}
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_input_ps_sgv") {
		if (w[1] == "constant") {
			w.erase(w.begin() + 1);
		}
		vector<DWORD> os = assembleOp(w[1], true);
		ins->opcode = 99;
		ins->_11_23 = 1;
		if (w.size() > 2) {
			if (w[2] == "sampleIndex") {
				os.push_back(0xA);
			} else if (w[2] == "is_front_face") {
				os.push_back(0x9);
			} else if (w[2] == "primitive_id") {
				os.push_back(0x7);
			}
		}
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_input_ps_siv") {
		vector<DWORD> os;
		ins->opcode = 100;
		if (w[1] == "linear") {
			if (w[2] == "noperspective") {
				if (w[3] == "sample") {
					ins->_11_23 = 7;
					os = assembleOp(w[4], true);
					if (w[5] == "position")
						os.push_back(1);
				} else if (w[3] == "centroid") {
					ins->_11_23 = 5;
					os = assembleOp(w[4], true);
					if (w[5] == "position")
						os.push_back(1);
				} else {
					ins->_11_23 = 4;
					os = assembleOp(w[3], true);
					if (w[4] == "position")
						os.push_back(1);
				}
			} else if (w[3] == "clip_distance") {
				os = assembleOp(w[2], true);
				ins->_11_23 = 2;
				os.push_back(2);
			}
		} else if (w[1] == "constant") {
			ins->_11_23 = 1;
			os = assembleOp(w[2], true);
			if (w[3] == "rendertarget_array_index")
				os.push_back(4);
		}
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_indexableTemp") {
		string s1 = w[1].erase(0, 1);
		string s2 = s1.substr(0, s1.find('['));
		string s3 = s1.substr(s1.find('[') + 1);
		s3.erase(s3.end() - 1, s3.end());
		ins->opcode = 105;
		ins->length = 4;
		v.push_back(op);
		v.push_back(atoi(s2.c_str()));
		v.push_back(atoi(s3.c_str()));
		v.push_back(atoi(w[2].c_str()));
	} else if (o == "dcl_immediateConstantBuffer") {
		vector<DWORD> os;
		ins->opcode = 53;
		ins->_11_23 = 3;
		ins->length = 0;
		DWORD length = 2;
		DWORD offset = 3;
		while (offset < w.size()) {
			string s1 = w[offset + 0];
			s1 = s1.substr(0, s1.find(','));
			string s2 = w[offset + 1];
			s2 = s2.substr(0, s2.find(','));
			string s3 = w[offset + 2];
			s3 = s3.substr(0, s3.find(','));
			string s4 = w[offset + 3];
			s4 = s4.substr(0, s4.find('}'));
			os.push_back(strToDWORD(s1));
			os.push_back(strToDWORD(s2));
			os.push_back(strToDWORD(s3));
			os.push_back(strToDWORD(s4));
			length += 4;
			offset += 5;
		}
		v.push_back(op);
		v.push_back(length);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_tessellator_partitioning") {
		ins->opcode = 150;
		ins->length = 1;
		if (w[1] == "partitioning_integer")
			ins->_11_23 = 1;
		else if (w[1] == "partitioning_fractional_odd")
			ins->_11_23 = 3;
		else if (w[1] == "partitioning_fractional_even")
			ins->_11_23 = 4;
		v.push_back(op);
	} else if (o == "dcl_tessellator_output_primitive") {
		ins->opcode = 151;
		ins->length = 1;
		if (w[1] == "output_line")
			ins->_11_23 = 2;
		else if (w[1] == "output_triangle_cw")
			ins->_11_23 = 3;
		else if (w[1] == "output_triangle_ccw")
			ins->_11_23 = 4;
		v.push_back(op);
	} else if (o == "dcl_tessellator_domain") {
		ins->opcode = 149;
		ins->length = 1;
		if (w[1] == "domain_isoline")
			ins->_11_23 = 1;
		else if (w[1] == "domain_tri")
			ins->_11_23 = 2;
		else if (w[1] == "domain_quad")
			ins->_11_23 = 3;
		v.push_back(op);
	} else if (o == "dcl_stream") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 143;
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "emit_stream") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 117;
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "cut_stream") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 118;
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_outputtopology") {
		ins->opcode = 92;
		ins->length = 1;
		if (w[1] == "trianglestrip")
			ins->_11_23 = 5;
		else if (w[1] == "linestrip")
			ins->_11_23 = 3;
		else if (w[1] == "pointlist")
			ins->_11_23 = 1;
		v.push_back(op);
	} else if (o == "dcl_output_control_point_count") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 148;
		ins->_11_23 = os[0];
		ins->length = 1;
		v.push_back(op);
	} else if (o == "dcl_input_control_point_count") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 147;
		ins->_11_23 = os[0];
		ins->length = 1;
		v.push_back(op);
	} else if (o == "dcl_maxout") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 94;
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	} else if (o == "dcl_inputprimitive") {
		ins->opcode = 93;
		ins->length = 1;
		if (w[1] == "point")
			ins->_11_23 = 1;
		else if (w[1] == "line")
			ins->_11_23 = 2;
		else if (w[1] == "triangle")
			ins->_11_23 = 3;
		else if (w[1] == "lineadj")
			ins->_11_23 = 6;
		else if (w[1] == "triangleadj")
			ins->_11_23 = 7;
		v.push_back(op);
	} else if (o == "dcl_hs_max_tessfactor") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 152;
		ins->length = 1 + os.size() - 1;
		v.push_back(op);
		v.insert(v.end(), os.begin() + 1, os.end());
	} else if (o == "dcl_hs_fork_phase_instance_count") {
		vector<DWORD> os = assembleOp(w[1]);
		ins->opcode = 153;
		ins->length = 1 + os.size();
		v.push_back(op);
		v.insert(v.end(), os.begin(), os.end());
	}

	/*
	if (o == "add") {
		ins->opcode = 0;
		auto o1 = assembleOp(w[1], true);
		auto o2 = assembleOp(w[2]);
		auto o3 = assembleOp(w[3]);
		ins->length = 1;
		ins->length += o1.size();
		ins->length += o2.size();
		ins->length += o3.size();
		v.push_back(op);
		v.insert(v.end(), o1.begin(), o1.end());
		v.insert(v.end(), o2.begin(), o2.end());
		v.insert(v.end(), o3.begin(), o3.end());
	}
	*/
	return v;
}

vector<UINT8> readFile(string fileName) {
	vector<UINT8> buffer;
	FILE* f;
	fopen_s(&f, fileName.c_str(), "rb");
	if (f != NULL) {
		fseek(f, 0L, SEEK_END);
		long fileSize = ftell(f);
		buffer.resize(fileSize);
		fseek(f, 0L, SEEK_SET);
		fread(buffer.data(), 1, buffer.size(), f);
		fclose(f);
	}
	return buffer;
}

vector<UINT8> readFile(wstring fileName) {
	vector<UINT8> buffer;
	FILE *f;
	_wfopen_s(&f, fileName.c_str(), L"rb");
	if (f != NULL) {
		fseek(f, 0L, SEEK_END);
		long fileSize = ftell(f);
		buffer.resize(fileSize);
		fseek(f, 0L, SEEK_SET);
		fread(buffer.data(), 1, buffer.size(), f);
		fclose(f);
	}
	return buffer;
}

vector<string> stringToLines(const char* start, size_t size) {
	vector<string> lines;
	if (size == 0)
		return lines;
	const char* pStart = start;
	const char* pEnd = pStart;
	const char* pRealEnd = pStart + size;
	while (true) {
		while (*pEnd != '\n' && pEnd < pRealEnd) {
			pEnd++;
		}
		if (*pStart == 0) {
			break;
		}
		string s(pStart, pEnd++);
		pStart = pEnd;
		lines.push_back(s);
		if (pStart >= pRealEnd) {
			break;
		}
	}
	for (unsigned int i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (s.size() > 1 && s[s.size() - 1] == '\r')
			s.erase(--s.end());
		lines[i] = s;
	}
	return lines;
}

vector<DWORD> ComputeHash(UINT8 const* input, DWORD size) {
	DWORD esi;
	DWORD ebx;
	DWORD i = 0;
	DWORD edi;
	DWORD edx;
	DWORD processedSize = 0;

	DWORD sizeHash = size & 0x3F;
	bool sizeHash56 = sizeHash >= 56;
	DWORD restSize = sizeHash56 ? 120 - 56 : 56 - sizeHash;
	DWORD loopSize = (size + 8 + restSize) >> 6;
	DWORD Dst[16];
	DWORD Data[] = { 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	DWORD loopSize2 = loopSize - (sizeHash56 ? 2 : 1);
	DWORD* pSrc = (DWORD*)input;
	DWORD h[] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476 };
	if (loopSize > 0) {
		while (i < loopSize) {
			if (i == loopSize2) {
				if (!sizeHash56) {
					Dst[0] = size << 3;
					DWORD remSize = size - processedSize;
					std::memcpy(&Dst[1], pSrc, remSize);
					std::memcpy(&Dst[1 + remSize / 4], Data, restSize);
					Dst[15] = (size * 2) | 1;
					pSrc = Dst;
				} else {
					DWORD remSize = size - processedSize;
					std::memcpy(&Dst[0], pSrc, remSize);
					std::memcpy(&Dst[remSize / 4], Data, 64 - remSize);
					pSrc = Dst;
				}
			} else if (i > loopSize2) {
				Dst[0] = size << 3;
				std::memcpy(&Dst[1], &Data[1], 56);
				Dst[15] = (size * 2) | 1;
				pSrc = Dst;
			}

			// initial values from memory
			edx = h[0];
			ebx = h[1];
			edi = h[2];
			esi = h[3];

			edx = _rotl((~ebx & esi | ebx & edi) + pSrc[0] + 0xD76AA478 + edx, 7) + ebx;
			esi = _rotl((~edx & edi | edx & ebx) + pSrc[1] + 0xE8C7B756 + esi, 12) + edx;
			edi = _rotr((~esi & ebx | esi & edx) + pSrc[2] + 0x242070DB + edi, 15) + esi;
			ebx = _rotr((~edi & edx | edi & esi) + pSrc[3] + 0xC1BDCEEE + ebx, 10) + edi;
			edx = _rotl((~ebx & esi | ebx & edi) + pSrc[4] + 0xF57C0FAF + edx, 7) + ebx;
			esi = _rotl((~edx & edi | ebx & edx) + pSrc[5] + 0x4787C62A + esi, 12) + edx;
			edi = _rotr((~esi & ebx | esi & edx) + pSrc[6] + 0xA8304613 + edi, 15) + esi;
			ebx = _rotr((~edi & edx | edi & esi) + pSrc[7] + 0xFD469501 + ebx, 10) + edi;
			edx = _rotl((~ebx & esi | ebx & edi) + pSrc[8] + 0x698098D8 + edx, 7) + ebx;
			esi = _rotl((~edx & edi | ebx & edx) + pSrc[9] + 0x8B44F7AF + esi, 12) + edx;
			edi = _rotr((~esi & ebx | esi & edx) + pSrc[10] + 0xFFFF5BB1 + edi, 15) + esi;
			ebx = _rotr((~edi & edx | edi & esi) + pSrc[11] + 0x895CD7BE + ebx, 10) + edi;
			edx = _rotl((~ebx & esi | ebx & edi) + pSrc[12] + 0x6B901122 + edx, 7) + ebx;
			esi = _rotl((~edx & edi | ebx & edx) + pSrc[13] + 0xFD987193 + esi, 12) + edx;
			edi = _rotr((~esi & ebx | esi & edx) + pSrc[14] + 0xA679438E + edi, 15) + esi;
			ebx = _rotr((~edi & edx | edi & esi) + pSrc[15] + 0x49B40821 + ebx, 10) + edi;

			edx = _rotl((~esi & edi | esi & ebx) + pSrc[1] + 0xF61E2562 + edx, 5) + ebx;
			esi = _rotl((~edi & ebx | edi & edx) + pSrc[6] + 0xC040B340 + esi, 9) + edx;
			edi = _rotl((~ebx & edx | ebx & esi) + pSrc[11] + 0x265E5A51 + edi, 14) + esi;
			ebx = _rotr((~edx & esi | edx & edi) + pSrc[0] + 0xE9B6C7AA + ebx, 12) + edi;
			edx = _rotl((~esi & edi | esi & ebx) + pSrc[5] + 0xD62F105D + edx, 5) + ebx;
			esi = _rotl((~edi & ebx | edi & edx) + pSrc[10] + 0x02441453 + esi, 9) + edx;
			edi = _rotl((~ebx & edx | ebx & esi) + pSrc[15] + 0xD8A1E681 + edi, 14) + esi;
			ebx = _rotr((~edx & esi | edx & edi) + pSrc[4] + 0xE7D3FBC8 + ebx, 12) + edi;
			edx = _rotl((~esi & edi | esi & ebx) + pSrc[9] + 0x21E1CDE6 + edx, 5) + ebx;
			esi = _rotl((~edi & ebx | edi & edx) + pSrc[14] + 0xC33707D6 + esi, 9) + edx;
			edi = _rotl((~ebx & edx | ebx & esi) + pSrc[3] + 0xF4D50D87 + edi, 14) + esi;
			ebx = _rotr((~edx & esi | edx & edi) + pSrc[8] + 0x455A14ED + ebx, 12) + edi;
			edx = _rotl((~esi & edi | esi & ebx) + pSrc[13] + 0xA9E3E905 + edx, 5) + ebx;
			esi = _rotl((~edi & ebx | edi & edx) + pSrc[2] + 0xFCEFA3F8 + esi, 9) + edx;
			edi = _rotl((~ebx & edx | ebx & esi) + pSrc[7] + 0x676F02D9 + edi, 14) + esi;
			ebx = _rotr((~edx & esi | edx & edi) + pSrc[12] + 0x8D2A4C8A + ebx, 12) + edi;

			edx = _rotl((esi ^ edi ^ ebx) + pSrc[5] + 0xFFFA3942 + edx, 4) + ebx;
			esi = _rotl((edi ^ ebx ^ edx) + pSrc[8] + 0x8771F681 + esi, 11) + edx;
			edi = _rotl((ebx ^ edx ^ esi) + pSrc[11] + 0x6D9D6122 + edi, 16) + esi;
			ebx = _rotr((edx ^ esi ^ edi) + pSrc[14] + 0xFDE5380C + ebx, 9) + edi;
			edx = _rotl((esi ^ edi ^ ebx) + pSrc[1] + 0xA4BEEA44 + edx, 4) + ebx;
			esi = _rotl((edi ^ ebx ^ edx) + pSrc[4] + 0x4BDECFA9 + esi, 11) + edx;
			edi = _rotl((ebx ^ edx ^ esi) + pSrc[7] + 0xF6BB4B60 + edi, 16) + esi;
			ebx = _rotr((edx ^ esi ^ edi) + pSrc[10] + 0xBEBFBC70 + ebx, 9) + edi;
			edx = _rotl((esi ^ edi ^ ebx) + pSrc[13] + 0x289B7EC6 + edx, 4) + ebx;
			esi = _rotl((edi ^ ebx ^ edx) + pSrc[0] + 0xEAA127FA + esi, 11) + edx;
			edi = _rotl((ebx ^ edx ^ esi) + pSrc[3] + 0xD4EF3085 + edi, 16) + esi;
			ebx = _rotr((edx ^ esi ^ edi) + pSrc[6] + 0x04881D05 + ebx, 9) + edi;
			edx = _rotl((esi ^ edi ^ ebx) + pSrc[9] + 0xD9D4D039 + edx, 4) + ebx;
			esi = _rotl((edi ^ ebx ^ edx) + pSrc[12] + 0xE6DB99E5 + esi, 11) + edx;
			edi = _rotl((ebx ^ edx ^ esi) + pSrc[15] + 0x1FA27CF8 + edi, 16) + esi;
			ebx = _rotr((edx ^ esi ^ edi) + pSrc[2] + 0xC4AC5665 + ebx, 9) + edi;

			edx = _rotl(((~esi | ebx) ^ edi) + pSrc[0] + 0xF4292244 + edx, 6) + ebx;
			esi = _rotl(((~edi | edx) ^ ebx) + pSrc[7] + 0x432AFF97 + esi, 10) + edx;
			edi = _rotl(((~ebx | esi) ^ edx) + pSrc[14] + 0xAB9423A7 + edi, 15) + esi;
			ebx = _rotr(((~edx | edi) ^ esi) + pSrc[5] + 0xFC93A039 + ebx, 11) + edi;
			edx = _rotl(((~esi | ebx) ^ edi) + pSrc[12] + 0x655B59C3 + edx, 6) + ebx;
			esi = _rotl(((~edi | edx) ^ ebx) + pSrc[3] + 0x8F0CCC92 + esi, 10) + edx;
			edi = _rotl(((~ebx | esi) ^ edx) + pSrc[10] + 0xFFEFF47D + edi, 15) + esi;
			ebx = _rotr(((~edx | edi) ^ esi) + pSrc[1] + 0x85845DD1 + ebx, 11) + edi;
			edx = _rotl(((~esi | ebx) ^ edi) + pSrc[8] + 0x6FA87E4F + edx, 6) + ebx;
			esi = _rotl(((~edi | edx) ^ ebx) + pSrc[15] + 0xFE2CE6E0 + esi, 10) + edx;
			edi = _rotl(((~ebx | esi) ^ edx) + pSrc[6] + 0xA3014314 + edi, 15) + esi;
			ebx = _rotr(((~edx | edi) ^ esi) + pSrc[13] + 0x4E0811A1 + ebx, 11) + edi;
			edx = _rotl(((~esi | ebx) ^ edi) + pSrc[4] + 0xF7537E82 + edx, 6) + ebx;
			h[0] += edx;
			esi = _rotl(((~edi | edx) ^ ebx) + pSrc[11] + 0xBD3AF235 + esi, 10) + edx;
			h[3] += esi;
			edi = _rotl(((~ebx | esi) ^ edx) + pSrc[2] + 0x2AD7D2BB + edi, 15) + esi;
			h[2] += edi;
			ebx = _rotr(((~edx | edi) ^ esi) + pSrc[9] + 0xEB86D391 + ebx, 11) + edi;
			h[1] += ebx;

			processedSize += 0x40;
			pSrc += 16;
			i++;
		}
	}
	vector<DWORD> hash(4);
	std::memcpy(hash.data(), h, 16);
	return hash;
}

string replaceString(string main, string search, string replace) {
	auto index = main.find(search);
	if (index != string::npos) {
		string start = main.substr(0, index);
		string combined = start.append(replace);
		string end = main.substr(index + search.size());
		combined = combined.append(end);
		return combined;
	}
	else {
		return main;
	}	
}

string RS(bool to, string line, string shortS, string longS, string comment = "") {
	if (to) {
		line = replaceString(line, longS, shortS);
		if (comment.size() > 0) {
			line = replaceString(line, comment, "");
		}
	}
	else {
		string changedLine = replaceString(line, shortS, longS);
		if (line != changedLine)
			line = changedLine.append(comment);
	}
	return line;
}

string handleCommon(bool b, string line) {
	line = RS(b, line, "extractvalue ResRet.i32", "extractvalue %dx.types.ResRet.i32");
	line = RS(b, line, "extractvalue ResRet.f32", "extractvalue %dx.types.ResRet.f32");
	line = RS(b, line, "extractvalue CBufRet.i32", "extractvalue %dx.types.CBufRet.i32");
	line = RS(b, line, "extractvalue CBufRet.f32", "extractvalue %dx.types.CBufRet.f32");

	line = RS(b, line, "CBufRet.i32 CBufferLoadLegacy(", "call %dx.types.CBufRet.i32 @dx.op.cbufferLoadLegacy.i32(i32 59, ", "  ; CBufferLoadLegacy(handle,regIndex)");
	line = RS(b, line, "CBufRet.f32 CBufferLoadLegacy(", "call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, ", "  ; CBufferLoadLegacy(handle,regIndex)");

	line = RS(b, line, "i32 LoadInput(", "call i32 @dx.op.loadInput.i32(i32 4, ", "  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)");
	line = RS(b, line, "f32 LoadInput(", "call float @dx.op.loadInput.f32(i32 4, ", "  ; LoadInput(inputSigId,rowIndex,colIndex,gsVertexAxis)");
	line = RS(b, line, "i32 StoreOutput(", "call void @dx.op.storeOutput.i32(i32 5, ", "  ; StoreOutput(outputSigId,rowIndex,colIndex,value)");
	line = RS(b, line, "f32 StoreOutput(", "call void @dx.op.storeOutput.f32(i32 5, ", "  ; StoreOutput(outputSigId,rowIndex,colIndex,value)");
	line = RS(b, line, "FAbs(", "call float @dx.op.unary.f32(i32 6, ", "  ; FAbs(value)");
	line = RS(b, line, "Saturate(", "call float @dx.op.unary.f32(i32 7, ", "  ; Saturate(value)");
	line = RS(b, line, "IsNaN(", "call i1 @dx.op.isSpecialFloat.f32(i32 8, ", "  ; IsNaN(value)");
	line = RS(b, line, "IsFinite(", "call i1 @dx.op.isSpecialFloat.f32(i32 10, ", "  ; IsFinite(value)");
	line = RS(b, line, "Cos(", "call float @dx.op.unary.f32(i32 12, ", "  ; Cos(value)");
	line = RS(b, line, "Sin(", "call float @dx.op.unary.f32(i32 13, ", "  ; Sin(value)");
	line = RS(b, line, "Tan(", "call float @dx.op.unary.f32(i32 14, ", "  ; Tan(value)");
	line = RS(b, line, "Exp(", "call float @dx.op.unary.f32(i32 21, ", "  ; Exp(value)");
	line = RS(b, line, "Frc(", "call float @dx.op.unary.f32(i32 22, ", "  ; Frc(value)");
	line = RS(b, line, "Log(", "call float @dx.op.unary.f32(i32 23, ", "  ; Log(value)");
	line = RS(b, line, "Sqrt(", "call float @dx.op.unary.f32(i32 24, ", "  ; Sqrt(value)");
	line = RS(b, line, "Round_ne(", "call float @dx.op.unary.f32(i32 26, ", "  ; Round_ne(value)"); line = RS(b, line, "Rsqrt(", "call float @dx.op.unary.f32(i32 25, ", "  ; Rsqrt(value)");
	line = RS(b, line, "Round_ni(", "call float @dx.op.unary.f32(i32 27, ", "  ; Round_ni(value)");
	line = RS(b, line, "FMax(", "call float @dx.op.binary.f32(i32 35, ", "  ; FMax(a,b)");
	line = RS(b, line, "FMin(", "call float @dx.op.binary.f32(i32 36, ", "  ; FMin(a,b)");
	line = RS(b, line, "FMad(", "call float @dx.op.tertiary.f32(i32 46, ", "  ; FMad(a,b,c)");
	line = RS(b, line, "Dot2(", "call float @dx.op.dot2.f32(i32 54, ", "  ; Dot2(ax,ay,bx,by)");
	line = RS(b, line, "Dot3(", "call float @dx.op.dot3.f32(i32 55, ", "  ; Dot3(ax,ay,az,bx,by,bz)");
	line = RS(b, line, "Dot4(", "call float @dx.op.dot4.f32(i32 56, ", "  ; Dot4(ax,ay,az,aw,bx,by,bz,bw)");

	line = RS(b, line, "ResRet.f32 Sample(", "call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, ", "  ; Sample(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,clamp)");
	line = RS(b, line, "ResRet.f32 SampleLevel(", "call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, ", "  ; SampleLevel(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,LOD)");
	line = RS(b, line, "SampleGrad(", "call %dx.types.ResRet.f32 @dx.op.sampleGrad.f32(i32 63, ", "  ; SampleGrad(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,ddx0,ddx1,ddx2,ddy0,ddy1,ddy2,clamp)");
	line = RS(b, line, "ResRet.f32 SampleCmp(", "call %dx.types.ResRet.f32 @dx.op.sampleCmp.f32(i32 64, ", "  ; SampleCmp(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,compareValue,clamp)");
	line = RS(b, line, "SampleCmpLevelZero(", "call %dx.types.ResRet.f32 @dx.op.sampleCmpLevelZero.f32(i32 65, ", "  ; SampleCmpLevelZero(srv,sampler,coord0,coord1,coord2,coord3,offset0,offset1,offset2,compareValue)");
	line = RS(b, line, "TextureLoad(", "call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, ", "  ; TextureLoad(srv,mipLevelOrSampleCount,coord0,coord1,coord2,offset0,offset1,offset2)");
	line = RS(b, line, "TextureStore(", "call void @dx.op.textureStore.f32(i32 67, ", "  ; TextureStore(srv,coord0,coord1,coord2,value0,value1,value2,value3,mask)");
	line = RS(b, line, "ResRet.i32 BufferLoad(", "call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(i32 68, ", "  ; BufferLoad(srv,index,wot)");
	line = RS(b, line, "BufferStore(","call void @dx.op.bufferStore.i32(i32 69, ", "  ; BufferStore(uav,coord0,coord1,value0,value1,value2,value3,mask)");
	line = RS(b, line, "GetDimensions(", "call %dx.types.Dimensions @dx.op.getDimensions(i32 72, ", "  ; GetDimensions(handle,mipLevel)");
	
	line = RS(b, line, "TextureGather(", "call %dx.types.ResRet.f32 @dx.op.textureGather.f32(i32 73,", "");
	line = RS(b, line, "Discard(", "call void @dx.op.discard(i32 82, ", "  ; Discard(condition)");
	line = RS(b, line, "DerivCoarseX(", "call float @dx.op.unary.f32(i32 83, ", "  ; DerivCoarseX(value)");
	line = RS(b, line, "DerivCoarseY(", "call float @dx.op.unary.f32(i32 84, ", "  ; DerivCoarseY(value)");
	line = RS(b, line, "i32 ThreadId(", "call i32 @dx.op.threadId.i32(i32 93, ", "  ; ThreadId(component)");
	line = RS(b, line, "GroupId(", "call i32 @dx.op.groupId.i32(i32 94, ", "  ; GroupId(component)");
	line = RS(b, line, "ThreadIdInGroup(", "call i32 @dx.op.threadIdInGroup.i32(i32 95, ", "  ; ThreadIdInGroup(component)");
	line = RS(b, line, "EmitStream(", "call void @dx.op.emitStream(i32 97, ", "  ; EmitStream(streamId)");
	line = RS(b, line, "CutStream(", "call void @dx.op.cutStream(i32 98, ", "  ; CutStream(streamId)");

	
	
	line = RS(b, line, "ResRet.i32 RawBufferLoad(", "call %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, ", "  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)");
	line = RS(b, line, "ResRet.f32 RawBufferLoad(", "call %dx.types.ResRet.f32 @dx.op.rawBufferLoad.f32(i32 139, ", "  ; RawBufferLoad(srv,index,elementOffset,mask,alignment)");

	
	return  line;
}

vector<UINT8> toDXILm(vector<UINT8> source) {
	vector<UINT8> ret = { 0 };
	if (source.size() == 0)
		return ret;
	if (source[0] == ';') {
		vector<string> lines = stringToLines((char*)source.data(), source.size());
		vector<string> newLines;
		for (size_t i = 0; i < lines.size(); i++) {
			string line = lines[i];
			if (line.find("declare") != string::npos) {
				newLines.push_back(line);
				continue;
			}

			bool b = true;
			line = RS(b, line, "Handle CreateHandle(", "call %dx.types.Handle @dx.op.createHandle(i32 57, ", "  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)");
			line = replaceString(line, "%dx.types.Handle", "Handle");
			line = replaceString(line, "%dx.types.Handle", "Handle");

			line = replaceString(line, " fast float", "");
			
			line = handleCommon(b, line);	
			newLines.push_back(line);
		}

		ret.clear();
		for (size_t i = 0; i < newLines.size(); i++) {
			for (size_t j = 0; j < newLines[i].size(); j++) {
				ret.push_back(newLines[i][j]);
			}
			ret.push_back('\n');
		}
		ret.push_back('\0');
		return ret;
	}
	else {
		return source;
	}
}

vector<UINT8> fromDXILm(vector<UINT8> source) {
	vector<UINT8> ret = { 0 };
	if (source.size() == 0)
		return ret;
	if (source[0] == ';') {
		vector<string> lines = stringToLines((char*)source.data(), source.size());
		vector<string> newLines;
		for (size_t i = 0; i < lines.size(); i++) {
			string line = lines[i];
			if (line.find("declare") != string::npos) {
				newLines.push_back(line);
				continue;
			}

			bool b = false;
			string createHandle = line;
			line = RS(b, line, "Handle CreateHandle(", "call %dx.types.Handle @dx.op.createHandle(i32 57, ", "  ; CreateHandle(resourceClass,rangeId,index,nonUniformIndex)");
			if (line == createHandle) {
				line = replaceString(line, "Handle", "%dx.types.H@ndle");
				line = replaceString(line, "Handle", "%dx.types.Handle");
				line = replaceString(line, "H@ndle", "Handle");
			}

			line = replaceString(line, "fadd", "fadd fast float");
			line = replaceString(line, "fmul", "fmul fast float");
			line = replaceString(line, "fsub", "fsub fast float");
			line = replaceString(line, "fdiv", "fdiv fast float");
			line = replaceString(line, "frem", "frem fast float");
			line = replaceString(line, "fast float float", "float");

			line = handleCommon(b, line);
			newLines.push_back(line);
		}

		ret.clear();
		for (size_t i = 0; i < newLines.size(); i++) {
			for (size_t j = 0; j < newLines[i].size(); j++) {
				ret.push_back(newLines[i][j]);
			}
			ret.push_back('\n');
		}
		ret.push_back('\0');
		return ret;
	}
	else {
		return source;
	}
}

vector<UINT8> assembler(bool dx9, vector<UINT8> asmFile, vector<UINT8> buffer) {
	if (dx9) {
		LPD3DXBUFFER pAssemblyL = NULL;
		D3DXAssembleShader((char*)asmFile.data(), (UINT)asmFile.size(), NULL, NULL, 0, &pAssemblyL, NULL);
		return readV(pAssemblyL->GetBufferPointer(), pAssemblyL->GetBufferSize());
	}

	vector<UINT8> ret;
	if (asmFile.size() == 0)
		return ret;
	if (buffer.size() == 0)
		return ret;
	if (asmFile[0] == ';') {
		if (dxc_module == 0)
			dxc_module = ::LoadLibrary(L"dxcompiler2.dll");
		if (dxc_module != 0) {
			DxcCreateInstanceProc dxc_create_func = (DxcCreateInstanceProc)GetProcAddress(dxc_module, "DxcCreateInstance");
			ComPtr<IDxcUtils> pUtils;
			dxc_create_func(CLSID_DxcUtils, IID_PPV_ARGS(pUtils.GetAddressOf()));
			ComPtr<IDxcAssembler> pAssembler;
			dxc_create_func(CLSID_DxcAssembler, IID_PPV_ARGS(pAssembler.GetAddressOf()));
			ComPtr<IDxcBlobEncoding> pSource;
			pUtils->CreateBlob(asmFile.data(), (DWORD)asmFile.size(), 0, pSource.GetAddressOf());
			ComPtr<IDxcOperationResult> pRes;
			HRESULT hr = pAssembler->AssembleToContainer(pSource.Get(), pRes.GetAddressOf());
			if (hr == S_OK) {
				ComPtr<IDxcBlob> pBlob;
				pRes->GetResult(pBlob.GetAddressOf());
				UINT8* pASM = (UINT8*)pBlob->GetBufferPointer();
				if (pASM == nullptr)
					return ret;
				for (size_t i = 0; i < pBlob->GetBufferSize(); i++) {
					ret.push_back(pASM[i]);
				}
				if (dxil_module == 0)
					dxil_module = ::LoadLibrary(L"dxil2.dll");
				if (dxil_module != 0) {
					DxcCreateInstanceProc dxil_create_func = (DxcCreateInstanceProc)GetProcAddress(dxil_module, "DxcCreateInstance");
					ComPtr<IDxcLibrary> library;
					dxc_create_func(CLSID_DxcLibrary, __uuidof(IDxcLibrary), (void**)&library);
					ComPtr<IDxcBlobEncoding> container_blob;
					library->CreateBlobWithEncodingFromPinned((BYTE*)ret.data(), (UINT32)ret.size(), 0 /* binary, no code page */, container_blob.GetAddressOf());
					ComPtr<IDxcValidator> validator;
					dxil_create_func(CLSID_DxcValidator, __uuidof(IDxcValidator), (void**)&validator);
					ComPtr<IDxcOperationResult> result;
					hr = validator->Validate(container_blob.Get(), DxcValidatorFlags_InPlaceEdit /* avoid extra copy owned by dxil.dll */, &result);
					if (hr != S_OK) {
						vector<UINT8> error;
						return error;
					}
				}
			}
			else {
				vector<UINT8> error;
				return error;
			}
		}
		return ret;
	}

	UINT8 fourcc[4];
	DWORD fHash[4];
	DWORD one;
	DWORD fSize;
	DWORD numChunks;
	vector<DWORD> chunkOffsets;

	UINT8* pPosition = buffer.data();
	std::memcpy(fourcc, pPosition, 4);
	pPosition += 4;
	std::memcpy(fHash, pPosition, 16);
	pPosition += 16;
	one = *(DWORD*)pPosition;
	pPosition += 4;
	fSize = *(DWORD*)pPosition;
	pPosition += 4;
	numChunks = *(DWORD*)pPosition;
	pPosition += 4;
	chunkOffsets.resize(numChunks);
	std::memcpy(chunkOffsets.data(), pPosition, 4 * numChunks);

	UINT8* codeByteStart = 0;
	int codeChunk = 0;
	for (DWORD i = 1; i <= numChunks; i++) {
		codeChunk = numChunks - i;
		codeByteStart = buffer.data() + chunkOffsets[numChunks - i];
		if (memcmp(codeByteStart, "SHEX", 4) == 0 || memcmp(codeByteStart, "SHDR", 4) == 0)
			break;
	}
	DWORD* codeStart = (DWORD*)(codeByteStart + 8);

	vector<string> lines = stringToLines((char*)asmFile.data(), asmFile.size());
	bool codeStarted = false;
	bool multiLine = false;
	string s2;
	vector<DWORD> o;
	for (DWORD i = 0; i < lines.size(); i++) {
		string s = lines[i];
		if (memcmp(s.c_str(), "//", 2) != 0 && memcmp(s.c_str(), "#line", 5) != 0) {
			vector<DWORD> v;
			if (!codeStarted) {
				if (s.size() > 0 && s[0] != ' ') {
					codeStarted = true;
					vector<DWORD> ins = assembleIns(s);
					o.insert(o.end(), ins.begin(), ins.end());
					o.push_back(0);
				}
			} else if (s.find("{ {") < s.size()) {
				s2 = s;
				multiLine = true;
			} else if (s.find("} }") < s.size()) {
				s2.append("\n");
				s2.append(s);
				s = s2;
				multiLine = false;
				vector<DWORD> ins = assembleIns(s);
				o.insert(o.end(), ins.begin(), ins.end());
			} else if (multiLine) {
				s2.append("\n");
				s2.append(s);
			} else if (s.size() > 0) {
				vector<DWORD> ins = assembleIns(s);
				o.insert(o.end(), ins.begin(), ins.end());
			}
		}
	}
	codeStart = (DWORD*)(codeByteStart);
	auto it = buffer.begin() + chunkOffsets[codeChunk] + 8;
	DWORD codeSize = codeStart[1];
	buffer.erase(it, it + codeSize);
	DWORD newCodeSize = 4 * (DWORD)o.size();
	codeStart[1] = newCodeSize;
	vector<UINT8> newCode(newCodeSize);
	o[1] = (DWORD)o.size();
	memcpy(newCode.data(), o.data(), newCodeSize);
	it = buffer.begin() + chunkOffsets[codeChunk] + 8;
	buffer.insert(it, newCode.begin(), newCode.end());
	DWORD* dwordBuffer = (DWORD*)buffer.data();
	for (DWORD i = codeChunk + 1; i < numChunks; i++) {
		dwordBuffer[8 + i] += newCodeSize - codeSize;
	}

	dwordBuffer[6] = (DWORD)buffer.size();
	vector<DWORD> hash = ComputeHash((UINT8 const*)buffer.data() + 20, (DWORD)buffer.size() - 20);
	dwordBuffer[1] = hash[0];
	dwordBuffer[2] = hash[1];
	dwordBuffer[3] = hash[2];
	dwordBuffer[4] = hash[3];

	return buffer;
}
