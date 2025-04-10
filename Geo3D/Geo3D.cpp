#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#include <imgui.h>
#include <reshade.hpp>
#include "dll_assembler.hpp"
#include "StorePipeline.hpp"

bool gl_left = false;

float gl_conv = 1.0f;
float gl_screenSize = 27.0f;
float gl_separation = 15.0f;
bool gl_dumpBIN = false;
bool gl_dumpOnly = false;
bool gl_dumpASM = false;
bool gl_Type = false;
bool gl_2D = false;
bool gl_pipelines = false;
bool gl_quickLoad = false;
bool gl_DepthZ = false;

std::filesystem::path dump_path;
std::filesystem::path fix_path;
using namespace reshade::api;

void load_config();

set<wstring> fixes;
static void  enumerateFiles() {
	fixes.clear();
	if (filesystem::exists(fix_path)) {
		for (auto fix : filesystem::directory_iterator(fix_path))
			fixes.insert(fix.path());
	}
}

void updatePipeline(reshade::api::device* device, PSO* pso) {
	vector<UINT8> ASM, vsV, dsV, gsV, psV, csV;
	vector<UINT8> VS_L, VS_R, PS_L, PS_R, CS_L, CS_R, DS_L, DS_R, GS_L, GS_R;
	vector<UINT8> cVS_L, cVS_R, cPS_L, cPS_R, cCS_L, cCS_R, cDS_L, cDS_R, cGS_L, cGS_R;

	bool dx9 = device->get_api() == device_api::d3d9;

	if (pso->vsEdit.size() > 0) {
		ASM = pso->vsEdit;
		auto L = patch(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		auto test = changeASM(dx9, L, true, gl_conv, gl_screenSize, gl_separation);
		if (test.size() == 0) {
			VS_L = L;
			VS_R = patch(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
		}
		else {
			VS_L = test;
			VS_R = patch(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
			VS_R = changeASM(dx9, VS_R, false, gl_conv, gl_screenSize, gl_separation);
		}
	}
	else if (pso->vsCode.code_size > 0) {
		vsV = readV(pso->vsCode.code, pso->vsCode.code_size);
		ASM = disassembler(vsV);
		auto test = changeASM(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		if (test.size() > 0) {
			VS_L = test;
			VS_R = changeASM(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
		}
		else {
			pso->vs->code = pso->vsCode.code;
			pso->vs->code_size = pso->vsCode.code_size;
		}
	}
	
	if (pso->dsCode.code_size > 0) {
		dsV = readV(pso->dsCode.code, pso->dsCode.code_size);
		ASM = disassembler(dsV);
		auto test = changeASM(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		if (test.size() > 0) {
			DS_L = test;
			DS_R = changeASM(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
		}
		else {
			pso->ds->code = pso->dsCode.code;
			pso->ds->code_size = pso->dsCode.code_size;
		}
	}

	if (pso->gsCode.code_size > 0) {
		gsV = readV(pso->gsCode.code, pso->gsCode.code_size);
		ASM = disassembler(gsV);
		auto test = changeASM(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		if (test.size() > 0) {
			GS_L = test;
			GS_R = changeASM(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
		}
		else {
			pso->gs->code = pso->gsCode.code;
			pso->gs->code_size = pso->gsCode.code_size;
		}
	}
	
	if (pso->psEdit.size() > 0) {
		psV = readV(pso->psCode.code, pso->psCode.code_size);
		ASM = pso->psEdit;
		PS_L = patch(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		PS_R = patch(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
	}
	else if (pso->psCode.code_size > 0) {
		pso->ps->code = pso->psCode.code;
		pso->ps->code_size = pso->psCode.code_size;
	}

	if (pso->csEdit.size() > 0) {
		csV = readV(pso->csCode.code, pso->csCode.code_size);
		ASM = pso->csEdit;
		CS_L = patch(dx9, ASM, true, gl_conv, gl_screenSize, gl_separation);
		CS_R = patch(dx9, ASM, false, gl_conv, gl_screenSize, gl_separation);
	}
	else if (pso->csCode.code_size > 0) {
		pso->cs->code = pso->csCode.code;
		pso->cs->code_size = pso->csCode.code_size;
	}

	if (VS_L.size() > 0 && VS_R.size()) {
		cVS_L = assembler(dx9, VS_L, vsV);
		cVS_R = assembler(dx9, VS_R, vsV);
	}
	if (DS_L.size() > 0 && DS_R.size()) {
		cDS_L = assembler(dx9, DS_L, dsV);
		cDS_R = assembler(dx9, DS_R, dsV);
	}
	if (GS_L.size() > 0 && GS_R.size()) {
		cGS_L = assembler(dx9, GS_L, gsV);
		cGS_R = assembler(dx9, GS_R, gsV);
	}
	if (PS_L.size() > 0 && PS_R.size()) {
		cPS_L = assembler(dx9, PS_L, psV);
		cPS_R = assembler(dx9, PS_R, psV);
	}
	if (CS_L.size() > 0 && CS_R.size()) {
		cCS_L = assembler(dx9, CS_L, csV);
		cCS_R = assembler(dx9, CS_R, csV);
	}

	if (cVS_L.size() > 0) {
		pso->vs->code = cVS_L.data();
		pso->vs->code_size = cVS_L.size();
	}
	if (cDS_L.size() > 0) {
		pso->ds->code = cDS_L.data();
		pso->ds->code_size = cDS_L.size();
	}
	if (cGS_L.size() > 0) {
		pso->gs->code = cGS_L.data();
		pso->gs->code_size = cGS_L.size();
	}
	if (cPS_L.size() > 0) {
		pso->ps->code = cPS_L.data();
		pso->ps->code_size = cPS_L.size();
	}
	if (cCS_L.size() > 0) {
		pso->cs->code = cCS_L.data();
		pso->cs->code_size = cCS_L.size();
	}

	reshade::api::pipeline pipeL;
	if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeL)) {
		/*
		if (pso->Left.handle != 0) {
			auto temp = (IUnknown*)pso->Left.handle;
			temp->Release();
		}
		*/
		pso->Left = pipeL;
	}

	if (cVS_R.size() > 0) {
		pso->vs->code = cVS_R.data();
		pso->vs->code_size = cVS_R.size();
	}
	if (cDS_R.size() > 0) {
		pso->ds->code = cDS_R.data();
		pso->ds->code_size = cDS_R.size();
	}
	if (cGS_R.size() > 0) {
		pso->gs->code = cGS_R.data();
		pso->gs->code_size = cGS_R.size();
	}
	if (cPS_R.size() > 0) {
		pso->ps->code = cPS_R.data();
		pso->ps->code_size = cPS_R.size();
	}
	if (cCS_R.size() > 0) {
		pso->cs->code = cCS_R.data();
		pso->cs->code_size = cCS_R.size();
	}	

	reshade::api::pipeline pipeR;
	if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeR)) {
		/*
		if (pso->Right.handle != 0) {
			auto temp = (IUnknown*)pso->Right.handle;
			temp->Release();
		}
		*/
		pso->Right = pipeR;
	}
}

static void onInitPipeline(device* device, pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline pipeline)
{
	if (PSOmap.count(pipeline.handle) == 1) {
		return;
	}

	bool dx9 = device->get_api() == device_api::d3d9;
	
	bool pipelines = false;
	size_t numShaders = 0;
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
		case pipeline_subobject_type::pixel_shader:
		case pipeline_subobject_type::domain_shader:
		case pipeline_subobject_type::geometry_shader:
		case pipeline_subobject_type::hull_shader:
			numShaders++;
			break;
		}
	}
	if (numShaders > 1)
		pipelines = gl_pipelines;
	
	PSO* pso = new PSO;
	
	shader_desc* vs = nullptr;
	shader_desc* ps = nullptr;
	shader_desc* ds = nullptr;
	shader_desc* gs = nullptr;
	shader_desc* hs = nullptr;
	shader_desc* cs = nullptr;

	pso->crcVS = 0;
	pso->crcPS = 0;
	pso->crcDS = 0;
	pso->crcGS = 0;
	pso->crcHS = 0;
	pso->crcCS = 0;

	pso->cs = nullptr;
	pso->ds = nullptr;
	pso->gs = nullptr;
	pso->ps = nullptr;
	pso->cs = nullptr;

	pso->skip = false;
	pso->noDraw = false;

	pso->Left.handle = 0;
	pso->Right.handle = 0;
		
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
			vs = static_cast<shader_desc *>(subobjects[i].data);
			pso->crcVS = dumpShader(dx9, L"vs", vs->code, vs->code_size, pipelines, pipeline.handle);;
			break;
		case pipeline_subobject_type::pixel_shader:
			ps = static_cast<shader_desc*>(subobjects[i].data);
			pso->crcPS = dumpShader(dx9, L"ps", ps->code, ps->code_size, pipelines, pipeline.handle);
			break;
		case pipeline_subobject_type::compute_shader:
			cs = static_cast<shader_desc*>(subobjects[i].data);
			pso->crcCS = dumpShader(dx9, L"cs", cs->code, cs->code_size, pipelines, pipeline.handle);
			break;
		case pipeline_subobject_type::domain_shader:
			ds = static_cast<shader_desc*>(subobjects[i].data);
			pso->crcDS = dumpShader(dx9, L"ds", ds->code, ds->code_size, pipelines, pipeline.handle);
			break;
		case pipeline_subobject_type::geometry_shader:
			gs = static_cast<shader_desc*>(subobjects[i].data);
			pso->crcGS = dumpShader(dx9, L"gs", gs->code, gs->code_size, pipelines, pipeline.handle);
			break;
		case pipeline_subobject_type::hull_shader:
			hs = static_cast<shader_desc*>(subobjects[i].data);
			pso->crcHS = dumpShader(dx9, L"hs", hs->code, hs->code_size, pipelines, pipeline.handle);
			break;
		}
	}
	
	if (gl_dumpOnly)
		return;

	wchar_t sPath[MAX_PATH];
	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso->crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->skip = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso->crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->skip = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skip", pso->crcCS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->skip = true;

	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skipdraw", pso->crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->noDraw = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skipdraw", pso->crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->noDraw = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skipdraw", pso->crcCS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->noDraw = true;
	
	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.txt", pso->crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->vsEdit = readFile(fix_path / sPath);
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.txt", pso->crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->psEdit = readFile(fix_path / sPath);
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.txt", pso->crcCS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso->csEdit = readFile(fix_path / sPath);
	
	storePipelineStateCrosire(layout, subobject_count, subobjects, pso);
	pso->separation = gl_separation;
	if (gl_quickLoad) {
		pso->convergence = 0;
	}
	else {
		pso->convergence = gl_conv;
		updatePipeline(device, pso);
	}
	PSOmap[pipeline.handle] = pso;
}

struct __declspec(uuid("7C1F9990-4D3F-4674-96AB-49E1840C83FC")) CommandListSkip {
	bool skip;
	uint32_t PS;
	uint32_t VS;
	uint32_t CS;
};

bool edit = false;
uint16_t fade = 120;
map<uint32_t, uint16_t> vertexShaders;
map<uint32_t, uint16_t> pixelShaders;
map<uint32_t, uint16_t> computeShaders;
uint32_t currentVS = 0;
uint32_t currentPS = 0;
uint32_t currentCS = 0;
bool huntUsing2D = true;
PSO* pso2 = nullptr;

static void onBindPipeline(command_list* cmd_list, pipeline_stage stage, reshade::api::pipeline pipeline)
{
	CommandListSkip& commandListData = cmd_list->get_private_data<CommandListSkip>();
	commandListData.skip = false;

	if (PSOmap.count(pipeline.handle) == 1) {
		PSO* pso = PSOmap[pipeline.handle];

		if (!edit) {
			if (pso->crcPS != 0) pixelShaders[pso->crcPS] = fade;
			if (pso->crcVS != 0) vertexShaders[pso->crcVS] = fade;
			if (pso->crcCS != 0) computeShaders[pso->crcCS] = fade;

			if (currentPS != 0) pixelShaders[currentVS] = 1;
			if (currentVS != 0) vertexShaders[currentVS] = 1;
			if (currentCS != 0) computeShaders[currentCS] = 1;
		}

		if (gl_2D)
			return;

		if (pso->convergence != gl_conv || pso->separation != gl_separation) {
			pso->convergence = gl_conv;
			pso->separation = gl_separation;
			updatePipeline(cmd_list->get_device(), pso);
		}

		if (cmd_list->get_device()->get_api() == device_api::d3d12) {
			if (pso->skip)
				return;
			if (pso->noDraw)
				commandListData.skip = true;
		}
		else {
			if (pso->skip || pso->noDraw)
				pso2 = pso;
			else if (pso->crcVS)
				pso2 = nullptr;
			if (pso2 != nullptr) {
				if (pso2->skip)
					return;
				if (pso2->noDraw)
					commandListData.skip = true;
			}
		}
		
		if (cmd_list->get_device()->get_api() == device_api::d3d12) {
			commandListData.PS = pso->crcPS ? pso->crcPS : -1;
			commandListData.VS = pso->crcVS ? pso->crcVS : -1;
		}
		else {
			commandListData.PS = pso->crcPS ? pso->crcPS : commandListData.PS;
			commandListData.VS = pso->crcVS ? pso->crcVS : commandListData.VS;
		}
		commandListData.CS = pso->crcCS ? pso->crcCS : commandListData.CS;
		
		if (currentPS > 0 && currentPS == commandListData.PS || currentVS > 0 && currentVS == commandListData.VS || currentCS > 0 && currentCS == commandListData.CS) {
			if (huntUsing2D) {
				return;
			}
			else {
				commandListData.skip = true;
			}
		}
		
		if ((stage & pipeline_stage::vertex_shader) != 0 ||
			(stage & pipeline_stage::domain_shader) != 0 ||
			(stage & pipeline_stage::geometry_shader) != 0 ||
			(stage & pipeline_stage::pixel_shader) != 0 ||
			(stage & pipeline_stage::compute_shader) != 0) {
			if (pso->Left.handle != 0) {
				if (gl_left) {
					cmd_list->bind_pipeline(stage, pso->Left);
				}
				else {
					cmd_list->bind_pipeline(stage, pso->Right);
				}
			}
		}
	}
}

static void onPresent(command_queue* queue, swapchain* swapchain, const rect* source_rect, const rect* dest_rect, uint32_t dirty_rect_count, const rect* dirty_rects) {
	gl_left = !gl_left;
}

static void onReshadePresent(effect_runtime* runtime)
{
	/*
	auto var = runtime->find_uniform_variable("3DToElse.fx", "framecount");
	unsigned int framecountElse = 0;
	runtime->get_uniform_value_uint(var, &framecountElse, 1);
	if (framecountElse > 0)
		gl_left = (framecountElse % 2) == 0;
	*/

	bool dx9 = runtime->get_device()->get_api() == device_api::d3d9;

	if (runtime->is_key_pressed(VK_NUMPAD0)) {
		if (edit)
			huntUsing2D = true;

		edit = !edit;
		if (!edit) {
			currentVS = 0;
			currentPS = 0;
			currentCS = 0;
		}
	}
	if (runtime->is_key_pressed(VK_DECIMAL)) {
		huntUsing2D = !huntUsing2D;
	}
	FILE* f;
	wchar_t sPath[MAX_PATH];
	if (runtime->is_key_pressed(VK_F10)) {
		enumerateFiles();
		load_config();
		for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
			PSO* pso = it->second;
			pso->skip = false;
			pso->noDraw = false;

			wchar_t sPath[MAX_PATH];
			swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso->crcVS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->skip = true;
			swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso->crcPS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->skip = true;
			swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skip", pso->crcCS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->skip = true;

			swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skipdraw", pso->crcVS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->noDraw = true;
			swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skipdraw", pso->crcPS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->noDraw = true;
			swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skipdraw", pso->crcCS);
			if (fixes.find(fix_path / sPath) != fixes.end())
				pso->noDraw = true;

			bool update = false;
			swprintf_s(sPath, MAX_PATH, L"%08lX-vs.txt", pso->crcVS);
			if (fixes.find(fix_path / sPath) != fixes.end()) {
				pso->vsEdit = readFile(fix_path / sPath);
				update = true;
			}
			else if (pso->vsEdit.size() > 0) {
				pso->vsEdit.clear();
				update = true;
			}
			swprintf_s(sPath, MAX_PATH, L"%08lX-ps.txt", pso->crcPS);
			if (fixes.find(fix_path / sPath) != fixes.end()) {
				pso->psEdit = readFile(fix_path / sPath);
				update = true;
			}
			else if (pso->psEdit.size() > 0) {
				pso->psEdit.clear();
				update = true;
			}
			swprintf_s(sPath, MAX_PATH, L"%08lX-cs.txt", pso->crcPS);
			if (fixes.find(fix_path / sPath) != fixes.end()) {
				pso->csEdit = readFile(fix_path / sPath);
				update = true;
			}
			else if (pso->csEdit.size() > 0) {
				pso->csEdit.clear();
				update = true;
			}
			if (update)
				updatePipeline(runtime->get_device(), pso);
		}
	}

	if (!edit) {
		vector<uint32_t> toDeleteVS;
		for (auto it = vertexShaders.begin(); it != vertexShaders.end(); ++it) {
			it->second--;
			if (it->second == 0)
				toDeleteVS.push_back(it->first);
		}
		for (auto it = toDeleteVS.begin(); it != toDeleteVS.end(); ++it)
			vertexShaders.erase(*it);

		vector<uint32_t> toDeletePS;
		for (auto it = pixelShaders.begin(); it != pixelShaders.end(); ++it) {
			it->second--;
			if (it->second == 0)
				toDeletePS.push_back(it->first);
		}
		for (auto it = toDeletePS.begin(); it != toDeletePS.end(); ++it)
			pixelShaders.erase(*it);

		vector<uint32_t> toDeleteCS;
		for (auto it = computeShaders.begin(); it != computeShaders.end(); ++it) {
			it->second--;
			if (it->second == 0)
				toDeleteCS.push_back(it->first);
		}
		for (auto it = toDeleteCS.begin(); it != toDeleteCS.end(); ++it)
			computeShaders.erase(*it);
	}

	if (runtime->is_key_down(VK_CONTROL)) {
		if (runtime->is_key_pressed(VK_F11)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (pixelShaders.count(pso->crcPS) == 1) {
					filesystem::path fix_path_dump = fix_path / L"Dump";
					filesystem::create_directories(fix_path_dump);
					swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso->crcPS);
					filesystem::path file = fix_path_dump / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(dx9, pso->psCode.code, pso->psCode.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
	}
	else {
		if (runtime->is_key_pressed(VK_F11)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (vertexShaders.count(pso->crcVS) == 1) {
					filesystem::path fix_path_dump = fix_path / L"Dump";
					filesystem::create_directories(fix_path_dump);
					swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso->crcVS);
					filesystem::path file = fix_path_dump / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(dx9, pso->vsCode.code, pso->vsCode.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
	}

	if (edit && !gl_2D) {
		if (runtime->is_key_pressed(VK_NUMPAD1)) {
			if (currentPS > 0) {
				auto it = pixelShaders.find(currentPS);
				if (it == pixelShaders.begin())
					currentPS = 0;
				else
					currentPS = (--it)->first;
			}
			else if (pixelShaders.size() > 0) {
				currentPS = pixelShaders.rbegin()->first;
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD2)) {
			if (pixelShaders.size() > 0) {
				if (currentPS == 0)
					currentPS = pixelShaders.begin()->first;
				else {
					auto it = pixelShaders.find(currentPS);
					++it;
					if (it == pixelShaders.end())
						currentPS = 0;
					else
						currentPS = it->first;
				}
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD3)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (pso->crcPS == currentPS && currentPS != 0) {
					filesystem::path file;
					filesystem::create_directories(fix_path);
					if (huntUsing2D) {
						pso->skip = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso->crcPS);
					}
					else {
						pso->noDraw = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skipdraw", pso->crcPS);
					}
					file = fix_path / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(dx9, pso->psCode.code, pso->psCode.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD4)) {
			if (currentVS > 0) {
				auto it = vertexShaders.find(currentVS);
				if (it == vertexShaders.begin())
					currentVS = 0;
				else
					currentVS = (--it)->first;
			}
			else if (vertexShaders.size() > 0) {
				currentVS = vertexShaders.rbegin()->first;
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD5)) {
			if (vertexShaders.size() > 0) {
				if (currentVS == 0)
					currentVS = vertexShaders.begin()->first;
				else {
					auto it = vertexShaders.find(currentVS);
					++it;
					if (it == vertexShaders.end())
						currentVS = 0;
					else
						currentVS = it->first;
				}
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD6)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (pso->crcVS == currentVS && currentVS != 0) {
					filesystem::path file;
					filesystem::create_directories(fix_path);
					if (huntUsing2D) {
						pso->skip = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso->crcVS);
					}
					else {
						pso->noDraw = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skipdraw", pso->crcVS);
					}
					file = fix_path / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(dx9, pso->vsCode.code, pso->vsCode.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD7)) {
			if (currentCS > 0) {
				auto it = computeShaders.find(currentCS);
				if (it == computeShaders.begin())
					currentCS = 0;
				else
					currentCS = (--it)->first;
			}
			else if (computeShaders.size() > 0) {
				currentCS = computeShaders.rbegin()->first;
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD8)) {
			if (computeShaders.size() > 0) {
				if (currentCS == 0)
					currentCS = computeShaders.begin()->first;
				else {
					auto it = computeShaders.find(currentCS);
					++it;
					if (it == computeShaders.end())
						currentCS = 0;
					else
						currentCS = it->first;
				}
			}
		}
		if (runtime->is_key_pressed(VK_NUMPAD9)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (pso->crcCS == currentCS && currentCS != 0) {
					filesystem::path file;
					filesystem::create_directories(fix_path);
					if (huntUsing2D) {
						pso->skip = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skip", pso->crcCS);
					}
					else {
						pso->noDraw = true;
						swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skipdraw", pso->crcCS);
					}
					file = fix_path / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(dx9, pso->csCode.code, pso->csCode.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
	}

	if (runtime->is_key_down(VK_CONTROL)) {
		if (runtime->is_key_pressed(VK_F1)) {
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				if (pso->convergence == 0)
					updatePipeline(runtime->get_device(), pso);
			}
		}
		if (runtime->is_key_pressed(VK_F2)) {
			gl_2D = !gl_2D;
		}
		if (runtime->is_key_pressed(VK_F3)) {
			gl_separation -= 5;
			if (gl_separation < 0)
				gl_separation = 0;
			reshade::set_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);
		}
		if (runtime->is_key_pressed(VK_F4)) {
			gl_separation += 5;
			if (gl_separation > 100)
				gl_separation = 100;
			reshade::set_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);
		}
		if (runtime->is_key_pressed(VK_F5)) {
			gl_conv *= 0.9f;
			if (gl_conv < 0.01f)
				gl_conv = 0.01f;
			reshade::set_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
		}
		if (runtime->is_key_pressed(VK_F6)) {
			gl_conv *= 1.11f;
			reshade::set_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
		}
		if (runtime->is_key_pressed(VK_F7)) {
			gl_Type = !gl_Type;
			reshade::set_config_value(nullptr, "Geo3D", "Type", gl_Type);
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				pso->convergence = 0;
			}
		}
		if (runtime->is_key_pressed(VK_F8)) {
			gl_DepthZ = !gl_DepthZ;
			reshade::set_config_value(nullptr, "Geo3D", "DepthZ", gl_DepthZ);
			for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
				PSO* pso = it->second;
				pso->convergence = 0;
			}
		}
	}
}

static void load_config()
{
	reshade::get_config_value(nullptr, "Geo3D", "DumpOnly", gl_dumpOnly);
	reshade::get_config_value(nullptr, "Geo3D", "DumpBIN", gl_dumpBIN);
	reshade::get_config_value(nullptr, "Geo3D", "DumpASM", gl_dumpASM);
	reshade::get_config_value(nullptr, "Geo3D", "DumpPipelines", gl_pipelines);

	reshade::get_config_value(nullptr, "Geo3D", "QuickLoad", gl_quickLoad);
	reshade::get_config_value(nullptr, "Geo3D", "Type", gl_Type);
	reshade::get_config_value(nullptr, "Geo3D", "DepthZ", gl_DepthZ);
	
	reshade::get_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
	reshade::get_config_value(nullptr, "Geo3D", "StereoScreenSize", gl_screenSize);
	reshade::get_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);

	WCHAR file_prefix[MAX_PATH] = L"";
	GetModuleFileNameW(nullptr, file_prefix, ARRAYSIZE(file_prefix));

	std::filesystem::path game_file_path = file_prefix;
	dump_path = game_file_path.parent_path();
	fix_path = dump_path / L"ShaderFixesGeo3D";
	dump_path /= L"ShaderCacheGeo3D";
	enumerateFiles();

	bool debug = false;
	reshade::get_config_value(nullptr, "Geo3D", "Debug", debug);
	if (debug) {
		do {
			Sleep(250);
		} while (!IsDebuggerPresent());
	}
}

static void onReshadeOverlay(reshade::api::effect_runtime* runtime)
{
	if (edit) {
		ImGui::SetNextWindowPos(ImVec2(20, 20));
		if (!ImGui::Begin("Geo3D", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Geo3D: %s Type: %s %s", gl_2D ? "2D mode" : "3D mode", gl_Type ? "1" : "0", gl_DepthZ ? "Z" : "");
		ImGui::Text("Screensizen %.1f", gl_screenSize);
		ImGui::Text("Separation %.0f", gl_separation);
		ImGui::Text("Convergence %.3f", gl_conv);

		size_t maxPS = pixelShaders.size();
		size_t selectedPS = 0;
		if (currentPS) {
			for (auto it = pixelShaders.begin(); it != pixelShaders.end(); ++it) {
				++selectedPS;
				if (it->first == currentPS)
					break;
			}
		}

		size_t maxVS = vertexShaders.size();
		size_t selectedVS = 0;
		if (currentVS) {
			for (auto it = vertexShaders.begin(); it != vertexShaders.end(); ++it) {
				++selectedVS;
				if (it->first == currentVS)
					break;
			}
		}

		size_t maxCS = computeShaders.size();
		size_t selectedCS = 0;
		if (currentCS) {
			for (auto it = computeShaders.begin(); it != computeShaders.end(); ++it) {
				++selectedCS;
				if (it->first == currentCS)
					break;
			}
		}

		ImGui::Text("Hunt %s: PS: %d/%d VS: %d/%d CS: %d/%d", huntUsing2D ? "2D" : "skip", selectedPS, maxPS, selectedVS, maxVS, selectedCS, maxCS);
		ImGui::End();
	}
}

static void onInitCommandList(command_list* commandList)
{
	commandList->create_private_data<CommandListSkip>();
	CommandListSkip& commandListData = commandList->get_private_data<CommandListSkip>();
	commandListData.skip = false;
	commandListData.VS = 0;
	commandListData.PS = 0;
	commandListData.CS = 0;
}

static void onDestroyCommandList(command_list* commandList)
{
	commandList->destroy_private_data<CommandListSkip>();
}

static void onResetCommandList(command_list* commandList)
{
	CommandListSkip& commandListData = commandList->get_private_data<CommandListSkip>();
	commandListData.skip = false;
	commandListData.VS = 0;
	commandListData.PS = 0;
	commandListData.CS = 0;
}

bool blockDrawCallForCommandList(command_list* commandList)
{
	if (nullptr == commandList)
	{
		return false;
	}

	CommandListSkip& commandListData = commandList->get_private_data<CommandListSkip>();
	return commandListData.skip;
}

static bool onDraw(command_list* commandList, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
	// check if for this command list the active shader handles are part of the blocked set. If so, return true
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawIndexed(command_list* commandList, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	// same as onDraw
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawOrDispatchIndirect(command_list* commandList, indirect_command type, resource buffer, uint64_t offset, uint32_t draw_count, uint32_t stride)
{
	switch (type)
	{
	case indirect_command::unknown:
	case indirect_command::draw:
	case indirect_command::draw_indexed:
		// same as OnDraw
		return blockDrawCallForCommandList(commandList);
		// the rest aren't blocked
	}
	return false;
}

extern "C" __declspec(dllexport) const char* NAME = "Geo3D";
extern "C" __declspec(dllexport) const char* DESCRIPTION = "DirectX Stereoscopic 3D mainly intended for DirectX 12";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD fdwReason, LPVOID)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(hModule))
			return FALSE;
		load_config();
		
		reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::register_event<reshade::addon_event::reshade_present>(onReshadePresent);
		
		reshade::register_event<reshade::addon_event::draw>(onDraw);
		reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		
		reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
		reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
		reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}