// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "D3DCompiler.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <filesystem>
#include <d3d12.h>
#include <d3d11.h>
#include <d3d10.h>
#include <d3d9.h>
#include "D3DX9Shader.h"
#include <regex>

using namespace std;

struct shader_ins
{
	unsigned opcode : 11;
	unsigned _11_23 : 13;
	unsigned length : 7;
	unsigned extended : 1;
};
struct token_operand
{
	unsigned comps_enum : 2; /* sm4_operands_comps */
	unsigned mode : 2; /* sm4_operand_mode */
	unsigned sel : 8;
	unsigned file : 8; /* SM_FILE */
	unsigned num_indices : 2;
	unsigned index0_repr : 3; /* sm4_operand_index_repr */
	unsigned index1_repr : 3; /* sm4_operand_index_repr */
	unsigned index2_repr : 3; /* sm4_operand_index_repr */
	unsigned extended : 1;
};

vector<UINT8> changeASM(bool dx9, vector<UINT8> ASM, bool left, float conv, float screenSize, float gl_separation);
vector<UINT8> patch(bool dx9, vector<UINT8> shader, bool left, float conv, float screenSize, float separation);
uint32_t dumpShader(bool dx9, const wchar_t *type, const void *pData, size_t length, bool pipeline = false, uint64_t handle = 0);
vector<UINT8> asmShader(bool dx9, const void* pData, size_t length);
vector<UINT8> readV(const void *code, size_t length);
vector<UINT8> readFile(string fileName);
vector<UINT8> readFile(wstring fileName);
vector<string> stringToLines(const char *start, size_t size);
vector<UINT8> disassembler(vector<UINT8> buffer);
vector<UINT8> assembler(bool dx9, vector<UINT8> asmFile, vector<UINT8> buffer);
void writeLUT();
vector<UINT8> toDXILm(vector<UINT8> source);
vector<UINT8> fromDXILm(vector<UINT8> source);