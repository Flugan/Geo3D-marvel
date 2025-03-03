#define IMGUI_DISABLE_INCLUDE_IMCONFIG_H
#define ImTextureID unsigned long long // Change ImGui texture ID type to that of a 'reshade::api::resource_view' handle

#include <imgui.h>
#include <reshade.hpp>
#include "dll_assembler.hpp"

bool gl_left = false;

float gl_conv = 1.0f;
float gl_minConv = 0.0f;
float gl_screenSize = 27.0f;
uint8_t gl_separation = 15;

bool gl_dumpBIN = false;
bool gl_dumpOnly = false;
bool gl_dumpASM = false;

bool gl_2D = false;
bool gl_quickLoad = false;
bool gl_Type = false;
bool gl_DepthZ = false;
bool gl_present = false;

std::filesystem::path dump_path;
std::filesystem::path fix_path;
using namespace reshade::api;

static void load_config();

struct PSO {
	uint8_t separation;
	float convergence;
	vector<pipeline_subobject> objects;
	pipeline_layout layout;

	shader_desc* vs;
	shader_desc vsS;
	uint32_t crcVS;
	vector<UINT8> vsEdit;

	shader_desc* ps;
	shader_desc psS;
	uint32_t crcPS;
	vector<UINT8> psEdit;

	shader_desc* cs;
	shader_desc csS;
	uint32_t crcCS;
	vector<UINT8> csEdit;

	shader_desc* ds;
	shader_desc dsS;
	uint32_t crcDS;

	shader_desc* gs;
	shader_desc gsS;
	uint32_t crcGS;

	bool skip;
	bool noDraw;

	reshade::api::pipeline Left;
	reshade::api::pipeline Right;
};
map<uint64_t, PSO> PSOmap;

static void storePipelineStateCrosire(pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, PSO* pso) {
	pso->layout = layout;
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		auto so = subobjects[i];
		switch (so.type)
		{
		case pipeline_subobject_type::vertex_shader:
		case pipeline_subobject_type::hull_shader:
		case pipeline_subobject_type::domain_shader:
		case pipeline_subobject_type::geometry_shader:
		case pipeline_subobject_type::pixel_shader:
		case pipeline_subobject_type::compute_shader:
		{
			const auto shader = static_cast<const shader_desc*>(so.data);

			if (shader == nullptr) {
				// nothing to see here
			}
			else {
				auto newShader = new shader_desc();
				newShader->code_size = shader->code_size;
				if (shader->code_size == 0) {
					newShader->code = nullptr;
				}
				else {
					auto code = new UINT8[shader->code_size];
					memcpy(code, shader->code, shader->code_size);
					newShader->code = code;
					if (shader->entry_point == nullptr)
					{
						newShader->entry_point = nullptr;
					}
					else
					{
						size_t EPlen = strlen(shader->entry_point) + 1;
						char* entry_point = new char[EPlen];
						strcpy_s(entry_point, EPlen, shader->entry_point);
						newShader->entry_point = entry_point;
					}

					newShader->spec_constants = shader->spec_constants;
					uint32_t* spec_constant_ids = nullptr;
					uint32_t* spec_constant_values = nullptr;
					if (shader->spec_constants > 0) {
						spec_constant_ids = new uint32_t[shader->spec_constants];
						spec_constant_values = new uint32_t[shader->spec_constants];
						for (uint32_t k = 0; k < shader->spec_constants; ++k)
						{
							spec_constant_ids[k] = shader->spec_constant_ids[k];
							spec_constant_values[k] = shader->spec_constant_values[k];
						}
					}
					newShader->spec_constant_ids = spec_constant_ids;
					newShader->spec_constant_values = spec_constant_values;
				}
				if (newShader->code_size > 0) {
					if (so.type == pipeline_subobject_type::vertex_shader) {
						pso->vs = newShader;
						pso->vsS = *newShader;
					}
					if (so.type == pipeline_subobject_type::pixel_shader) {
						pso->ps = newShader;
						pso->psS = *newShader;
					}
					if (so.type == pipeline_subobject_type::compute_shader) {
						pso->cs = newShader;
						pso->csS = *newShader;
					}
					if (so.type == pipeline_subobject_type::domain_shader) {
						pso->ds = newShader;
						pso->dsS = *newShader;
					}
					if (so.type == pipeline_subobject_type::geometry_shader) {
						pso->gs = newShader;
						pso->gsS = *newShader;
					}
				}
				so.data = newShader;
			}
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::input_layout:
		{
			const auto input_layout = static_cast<const input_element*>(so.data);

			input_element* input_elements = new input_element[so.count];
			for (uint32_t k = 0; k < so.count; ++k)
			{
				input_elements[k].buffer_binding = input_layout[k].buffer_binding;
				input_elements[k].format = input_layout[k].format;
				input_elements[k].instance_step_rate = input_layout[k].instance_step_rate;
				input_elements[k].location = input_layout[k].location;
				input_elements[k].offset = input_layout[k].offset;
				input_elements[k].semantic_index = input_layout[k].semantic_index;
				input_elements[k].stride = input_layout[k].stride;

				size_t SEMlen = strlen(input_layout[k].semantic) + 1;
				char* semantic = new char[SEMlen];
				strcpy_s(semantic, SEMlen, input_layout[k].semantic);
				input_elements[k].semantic = semantic;
			}

			so.data = input_elements;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::blend_state:
		{
			const auto blend_state = *static_cast<const blend_desc*>(so.data);
			auto newBlend = new blend_desc();

			for (size_t k = 0; k < 8; ++k)
			{
				newBlend->alpha_blend_op[k] = blend_state.alpha_blend_op[k];
				newBlend->color_blend_op[k] = blend_state.color_blend_op[k];
				newBlend->blend_enable[k] = blend_state.blend_enable[k];
				newBlend->dest_alpha_blend_factor[k] = blend_state.dest_alpha_blend_factor[k];
				newBlend->dest_color_blend_factor[k] = blend_state.dest_color_blend_factor[k];
				newBlend->logic_op[k] = blend_state.logic_op[k];
				newBlend->logic_op_enable[k] = blend_state.logic_op_enable[k];
				newBlend->render_target_write_mask[k] = blend_state.render_target_write_mask[k];
				newBlend->source_alpha_blend_factor[k] = blend_state.source_alpha_blend_factor[k];
				newBlend->source_color_blend_factor[k] = blend_state.source_color_blend_factor[k];
			}
			for (size_t k = 0; k < 4; ++k)
			{
				newBlend->blend_constant[k] = blend_state.blend_constant[k];
			}
			newBlend->alpha_to_coverage_enable = blend_state.alpha_to_coverage_enable;

			so.data = newBlend;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::rasterizer_state:
		{
			const auto rasterizer_state = *static_cast<const rasterizer_desc*>(so.data);
			auto newRasterizer = new rasterizer_desc();
			newRasterizer->antialiased_line_enable = rasterizer_state.antialiased_line_enable;
			newRasterizer->conservative_rasterization = rasterizer_state.conservative_rasterization;
			newRasterizer->cull_mode = rasterizer_state.cull_mode;
			newRasterizer->depth_bias = rasterizer_state.depth_bias;
			newRasterizer->depth_bias_clamp = rasterizer_state.depth_bias_clamp;
			newRasterizer->depth_clip_enable = rasterizer_state.depth_clip_enable;
			newRasterizer->fill_mode = rasterizer_state.fill_mode;
			newRasterizer->front_counter_clockwise = rasterizer_state.front_counter_clockwise;
			newRasterizer->multisample_enable = rasterizer_state.multisample_enable;
			newRasterizer->scissor_enable = rasterizer_state.scissor_enable;
			newRasterizer->slope_scaled_depth_bias = rasterizer_state.slope_scaled_depth_bias;

			so.data = newRasterizer;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::depth_stencil_state:
		{
			const auto depth_stencil_state = *static_cast<const depth_stencil_desc*>(so.data);
			auto newDepth = new depth_stencil_desc();
			newDepth->back_stencil_depth_fail_op = depth_stencil_state.back_stencil_depth_fail_op;
			newDepth->back_stencil_fail_op = depth_stencil_state.back_stencil_depth_fail_op;
			newDepth->back_stencil_func = depth_stencil_state.back_stencil_func;
			newDepth->back_stencil_pass_op = depth_stencil_state.back_stencil_pass_op;
			newDepth->depth_enable = depth_stencil_state.depth_enable;
			newDepth->depth_func = depth_stencil_state.depth_func;
			newDepth->depth_write_mask = depth_stencil_state.depth_write_mask;
			newDepth->front_stencil_depth_fail_op = depth_stencil_state.front_stencil_depth_fail_op;
			newDepth->front_stencil_fail_op = depth_stencil_state.front_stencil_fail_op;
			newDepth->front_stencil_func = depth_stencil_state.front_stencil_func;
			newDepth->front_stencil_pass_op = depth_stencil_state.front_stencil_pass_op;
			newDepth->stencil_enable = depth_stencil_state.stencil_enable;
			/*
			newDepth->stencil_read_mask = depth_stencil_state.stencil_read_mask;
			newDepth->stencil_reference_value = depth_stencil_state.stencil_reference_value;
			newDepth->stencil_write_mask = depth_stencil_state.stencil_write_mask;
			*/			
			newDepth->front_stencil_read_mask = depth_stencil_state.front_stencil_read_mask;
			newDepth->front_stencil_reference_value = depth_stencil_state.front_stencil_reference_value;
			newDepth->front_stencil_write_mask = depth_stencil_state.front_stencil_write_mask;			

			newDepth->back_stencil_read_mask = depth_stencil_state.back_stencil_read_mask;
			newDepth->back_stencil_reference_value = depth_stencil_state.back_stencil_reference_value;
			newDepth->back_stencil_write_mask = depth_stencil_state.back_stencil_write_mask;
			
			so.data = newDepth;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::stream_output_state:
		{
			const auto stream_output_state = *static_cast<const stream_output_desc*>(so.data);
			auto newStream = new stream_output_desc();
			newStream->rasterized_stream = stream_output_state.rasterized_stream;

			so.data = newStream;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::primitive_topology:
		{
			const auto topology = *static_cast<const primitive_topology*>(so.data);
			auto newTopology = new primitive_topology(topology);

			so.data = newTopology;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::depth_stencil_format:
		{
			const auto depth_stencil_format = *static_cast<const format*>(so.data);
			auto newDepthFormat = new format();
			*newDepthFormat = depth_stencil_format;

			so.data = newDepthFormat;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::render_target_formats:
		{
			const auto render_target_formats = static_cast<const format*>(so.data);

			format* rtf = new format[so.count];
			for (uint32_t k = 0; k < subobjects[i].count; ++k)
			{
				rtf[k] = render_target_formats[k];
			}

			so.data = rtf;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::sample_mask:
		{
			const auto sample_mask = *static_cast<const uint32_t*>(so.data);
			auto newSampleMask = new uint32_t();
			*newSampleMask = sample_mask;

			so.data = newSampleMask;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::sample_count:
		{
			const auto sample_count = *static_cast<const uint32_t*>(so.data);
			auto newSampleCount = new uint32_t();
			*newSampleCount = sample_count;

			so.data = newSampleCount;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::viewport_count:
		{
			const auto viewport_count = *static_cast<const uint32_t*>(so.data);
			auto newViewportCount = new uint32_t();
			*newViewportCount = viewport_count;

			so.data = newViewportCount;
			pso->objects.push_back(so);
			break;
		}
		case pipeline_subobject_type::dynamic_pipeline_states:
		{
			const auto dynamic_pipeline_states = static_cast<const dynamic_state*>(so.data);

			dynamic_state* dps = new dynamic_state[so.count];
			for (uint32_t k = 0; k < subobjects[i].count; ++k)
			{
				dps[k] = dynamic_pipeline_states[k];
			}

			so.data = dps;
			pso->objects.push_back(so);
			break;
		}
		}
	}
}

set<wstring> fixes;
static void  enumerateFiles() {
	fixes.clear();
	if (filesystem::exists(fix_path)) {
		for (auto fix : filesystem::directory_iterator(fix_path))
			fixes.insert(fix.path());
	}
}

vector<UINT8> changeOpenGL(bool left, vector<UINT8> ASM) {
	vector<UINT8> shaderOutput;
	auto lines = stringToLines((char*)ASM.data(), ASM.size());
	vector<string> output;
	for (size_t i = 0; i < lines.size(); i++) {
		auto s = lines[i];
		output.push_back(s);
	}
	for (size_t i = 0; i < output.size(); i++) {
		for (size_t j = 0; j < output[i].size(); j++) {
			shaderOutput.push_back(output[i][j]);
		}
		shaderOutput.push_back('\n');
	}
	return shaderOutput;
}

mutex m;
void updatePipeline(reshade::api::device* device, PSO* pso) {
	vector<UINT8> ASM;
	vector<UINT8> VS_L, VS_R, PS_L, PS_R, CS_L, CS_R;
	vector<UINT8> cVS_L, cVS_R, cPS_L, cPS_R, cCS_L, cCS_R;
	vector<UINT8> DS_L, DS_R, GS_L, GS_R, cDS_L, cDS_R, cGS_L, cGS_R;

	bool dx9 = device->get_api() == device_api::d3d9;

	if (device->get_api() == device_api::opengl) {
		if (pso->vsS.code_size == 0)
			return;

		auto ASM = readV(pso->vsS.code, pso->vsS.code_size);
		auto VS_L = changeOpenGL(true, ASM);
		auto VS_R = changeOpenGL(false, ASM);

		if (gl_dumpASM) {
			FILE* f;
			wchar_t sPath[MAX_PATH];

			if (VS_L.size() > 0) {
				filesystem::path file;
				filesystem::create_directories(dump_path);
				swprintf_s(sPath, MAX_PATH, L"%08lX-vs-left.txt", pso->crcVS);
				file = dump_path / sPath;
				_wfopen_s(&f, file.c_str(), L"wb");
				if (f != 0) {
					fwrite(VS_L.data(), 1, VS_L.size(), f);
					fclose(f);
				}
			}
			if (VS_R.size() > 0) {
				filesystem::path file;
				filesystem::create_directories(dump_path);
				swprintf_s(sPath, MAX_PATH, L"%08lX-vs-right.txt", pso->crcVS);
				file = dump_path / sPath;
				_wfopen_s(&f, file.c_str(), L"wb");
				if (f != 0) {
					fwrite(VS_R.data(), 1, VS_R.size(), f);
					fclose(f);
				}
			}
		}

		if (gl_dumpOnly)
			return;

		if (cVS_L.size() > 0) {
			pso->vs->code = cVS_L.data();
			pso->vs->code_size = cVS_L.size();
		}

		reshade::api::pipeline pipeL;
		if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeL)) {
			pso->Left = pipeL;
		}

		if (cVS_R.size() > 0) {
			pso->vs->code = cVS_R.data();
			pso->vs->code_size = cVS_R.size();
		}

		reshade::api::pipeline pipeR;
		if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeR)) {
			pso->Right = pipeR;
		}

		return;
	}

	if (pso->vsEdit.size() > 0) {
		ASM = pso->vsEdit;

		VS_L = patch(dx9, ASM, true);
		VS_R = patch(dx9, ASM, false);
		
		auto test = changeASM(dx9, VS_L, true);
		if (test.size() > 0) {
			VS_L = test;
			VS_R = changeASM(dx9, VS_R, false);
		}
	}
	else if (pso->vsS.code_size > 0) {
		auto ASM = asmShader(pso->vsS.code, pso->vsS.code_size);
		auto test = changeASM(dx9, ASM, true);
		if (test.size() > 0) {
			VS_L = test;
			VS_R = changeASM(dx9, ASM, false);
		}
		else {
			pso->vs->code = pso->vsS.code;
			pso->vs->code_size = pso->vsS.code_size;
		}
	}
	
	if (pso->dsS.code_size > 0) {
		auto ASM = asmShader(pso->dsS.code, pso->dsS.code_size);
		auto test = changeASM(dx9, ASM, true);
		if (test.size() > 0) {
			DS_L = test;
			DS_R = changeASM(dx9, ASM, false);
		}
		else {
			pso->ds->code = pso->dsS.code;
			pso->ds->code_size = pso->dsS.code_size;
		}
	}
	if (pso->gsS.code_size > 0) {
		auto ASM = asmShader(pso->gsS.code, pso->gsS.code_size);
		auto test = changeASM(dx9, ASM, true);
		if (test.size() > 0) {
			GS_L = test;
			GS_R = changeASM(dx9, ASM, false);
		}
		else {
			pso->gs->code = pso->gsS.code;
			pso->gs->code_size = pso->gsS.code_size;
		}
	}

	if (pso->psEdit.size() > 0) {
		ASM = pso->psEdit;
		PS_L = patch(dx9, ASM, true);
		PS_R = patch(dx9, ASM, false);
	}
	else if (pso->psS.code_size > 0) {
		pso->ps->code = pso->psS.code;
		pso->ps->code_size = pso->psS.code_size;
	}

	if (pso->csEdit.size() > 0) {
		ASM = pso->csEdit;
		CS_L = patch(dx9, ASM, true);
		CS_R = patch(dx9, ASM, false);
	}
	else if (pso->csS.code_size > 0) {
		pso->cs->code = pso->csS.code;
		pso->cs->code_size = pso->csS.code_size;
	}

	if (gl_dumpASM) {
		FILE* f;
		wchar_t sPath[MAX_PATH];

		if (VS_L.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-vs-left.txt", pso->crcVS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(VS_L.data(), 1, VS_L.size(), f);
				fclose(f);
			}
		}
		if (VS_R.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-vs-right.txt", pso->crcVS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(VS_R.data(), 1, VS_R.size(), f);
				fclose(f);
			}
		}
		if (PS_L.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-ps-left.txt", pso->crcPS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(PS_L.data(), 1, PS_L.size(), f);
				fclose(f);
			}
		}
		if (VS_R.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-ps-right.txt", pso->crcPS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(PS_R.data(), 1, PS_R.size(), f);
				fclose(f);
			}
		}
		if (CS_L.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-cs-left.txt", pso->crcCS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(CS_L.data(), 1, CS_L.size(), f);
				fclose(f);
			}
		}
		if (CS_R.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-cs-right.txt", pso->crcCS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(CS_R.data(), 1, CS_R.size(), f);
				fclose(f);
			}
		}
		if (DS_L.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-ds-left.txt", pso->crcDS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(DS_L.data(), 1, DS_L.size(), f);
				fclose(f);
			}
		}
		if (DS_R.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-ds-right.txt", pso->crcDS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(DS_R.data(), 1, DS_R.size(), f);
				fclose(f);
			}
		}
		if (GS_L.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-gs-left.txt", pso->crcGS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(GS_L.data(), 1, GS_L.size(), f);
				fclose(f);
			}
		}
		if (GS_R.size() > 0) {
			filesystem::path file;
			filesystem::create_directories(dump_path);
			swprintf_s(sPath, MAX_PATH, L"%08lX-gs-right.txt", pso->crcGS);
			file = dump_path / sPath;
			_wfopen_s(&f, file.c_str(), L"wb");
			if (f != 0) {
				fwrite(GS_R.data(), 1, GS_R.size(), f);
				fclose(f);
			}
		}
	}

	if (gl_dumpOnly)
		return;

	if (VS_L.size() > 0) {
		auto vsV = readV(pso->vsS.code, pso->vsS.code_size);
		cVS_L = assembler(dx9, VS_L, vsV);
		cVS_R = assembler(dx9, VS_R, vsV);
	}
	if (PS_L.size() > 0) {
		auto psV = readV(pso->psS.code, pso->psS.code_size);
		cPS_L = assembler(dx9, PS_L, psV);
		cPS_R = assembler(dx9, PS_R, psV);
	}
	if (CS_L.size() > 0) {
		auto csV = readV(pso->csS.code, pso->csS.code_size);
		cCS_L = assembler(dx9, CS_L, csV);
		cCS_R = assembler(dx9, CS_R, csV);
	}
	if (DS_L.size() > 0) {
		auto dsV = readV(pso->dsS.code, pso->dsS.code_size);
		cDS_L = assembler(dx9, DS_L, dsV);
		cDS_R = assembler(dx9, DS_R, dsV);
	}
	if (GS_L.size() > 0) {
		auto gsV = readV(pso->gsS.code, pso->gsS.code_size);
		cGS_L = assembler(dx9, GS_L, gsV);
		cGS_R = assembler(dx9, GS_R, gsV);
	}

	if (cVS_L.size() > 0) {
		pso->vs->code = cVS_L.data();
		pso->vs->code_size = cVS_L.size();
	}
	if (cPS_L.size() > 0) {
		pso->ps->code = cPS_L.data();
		pso->ps->code_size = cPS_L.size();
	}
	if (cCS_L.size() > 0) {
		pso->cs->code = cCS_L.data();
		pso->cs->code_size = cCS_L.size();
	}
	if (cDS_L.size() > 0) {
		pso->ds->code = cDS_L.data();
		pso->ds->code_size = cDS_L.size();
	}
	if (cGS_L.size() > 0) {
		pso->gs->code = cGS_L.data();
		pso->gs->code_size = cGS_L.size();
	}

	reshade::api::pipeline pipeL;
	if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeL)) {
		pso->Left = pipeL;
	}

	if (cVS_R.size() > 0) {
		pso->vs->code = cVS_R.data();
		pso->vs->code_size = cVS_R.size();
	}
	if (cPS_R.size() > 0) {
		pso->ps->code = cPS_R.data();
		pso->ps->code_size = cPS_R.size();
	}
	if (cCS_R.size() > 0) {
		pso->cs->code = cCS_R.data();
		pso->cs->code_size = cCS_R.size();
	}
	if (cDS_R.size() > 0) {
		pso->ds->code = cDS_R.data();
		pso->ds->code_size = cDS_R.size();
	}
	if (cGS_R.size() > 0) {
		pso->gs->code = cGS_R.data();
		pso->gs->code_size = cGS_R.size();
	}

	reshade::api::pipeline pipeR;
	if (device->create_pipeline(pso->layout, (UINT32)pso->objects.size(), pso->objects.data(), &pipeR)) {
		pso->Right = pipeR;
	}
}

static void onInitPipeline(device* device, pipeline_layout layout, uint32_t subobject_count, const pipeline_subobject* subobjects, pipeline pipeline)
{
	if (PSOmap.count(pipeline.handle) == 1)
		return;

	shader_desc* vs = nullptr;
	shader_desc* ps = nullptr;
	shader_desc* ds = nullptr;
	shader_desc* gs = nullptr;
	shader_desc* hs = nullptr;
	shader_desc* cs = nullptr;

	bool dx9 = device->get_api() == device_api::d3d9;
	if (device->get_api() == device_api::opengl) {
		if (gl_dumpASM)
			gl_dumpBIN = true;
		gl_dumpASM = false;
	}

	PSO pso = {};
	storePipelineStateCrosire(layout, subobject_count, subobjects, &pso);
	pso.separation = gl_separation;
	pso.convergence = gl_conv;
	for (uint32_t i = 0; i < subobject_count; ++i)
	{
		switch (subobjects[i].type)
		{
		case pipeline_subobject_type::vertex_shader:
			vs = static_cast<shader_desc*>(subobjects[i].data);
			pso.crcVS = dumpShader(L"vs", vs->code, vs->code_size);
			break;
		case pipeline_subobject_type::pixel_shader:
			ps = static_cast<shader_desc*>(subobjects[i].data);
			pso.crcPS = dumpShader(L"ps", ps->code, ps->code_size);
			break;
		case pipeline_subobject_type::compute_shader:
			cs = static_cast<shader_desc*>(subobjects[i].data);
			pso.crcCS = dumpShader(L"cs", cs->code, cs->code_size);
			break;
		case pipeline_subobject_type::domain_shader:
			ds = static_cast<shader_desc*>(subobjects[i].data);
			pso.crcDS = dumpShader(L"ds", ds->code, ds->code_size);
			break;
		case pipeline_subobject_type::geometry_shader:
			gs = static_cast<shader_desc*>(subobjects[i].data);
			pso.crcGS = dumpShader(L"gs", gs->code, gs->code_size);
			break;
		case pipeline_subobject_type::hull_shader:
			hs = static_cast<shader_desc*>(subobjects[i].data);
			dumpShader(L"hs", hs->code, hs->code_size);
			break;
		}
	}

	wchar_t sPath[MAX_PATH];
	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso.crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.skip = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso.crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.skip = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skip", pso.crcCS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.skip = true;

	if (pso.skip)
		return;

	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skipdraw", pso.crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.noDraw = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skipdraw", pso.crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.noDraw = true;
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.skipdraw", pso.crcCS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.noDraw = true;
	
	swprintf_s(sPath, MAX_PATH, L"%08lX-vs.txt", pso.crcVS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.vsEdit = readFile(fix_path / sPath);
	swprintf_s(sPath, MAX_PATH, L"%08lX-ps.txt", pso.crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.psEdit = readFile(fix_path / sPath);
	swprintf_s(sPath, MAX_PATH, L"%08lX-cs.txt", pso.crcPS);
	if (fixes.find(fix_path / sPath) != fixes.end())
		pso.csEdit = readFile(fix_path / sPath);
	
	if (gl_quickLoad) {
		pso.convergence = 0;
	}
	else {
		updatePipeline(device, &pso);
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

	if (gl_2D)
		return;
	
	if (PSOmap.count(pipeline.handle) == 1) {
		PSO* pso = &PSOmap[pipeline.handle];
		
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

		if (edit) {
			if (pso->crcPS != 0) pixelShaders[pso->crcPS] = 1;
			if (pso->crcVS != 0) vertexShaders[pso->crcVS] = 1;
			if (pso->crcCS != 0) computeShaders[pso->crcCS] = 1;
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
			(stage & pipeline_stage::pixel_shader) != 0 ||
			(stage & pipeline_stage::compute_shader) != 0 ||
			(stage & pipeline_stage::domain_shader) != 0 ||
			(stage & pipeline_stage::geometry_shader) != 0) {
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

bool gl_runtimeLeft = false;

static void onPresent(command_queue* queue, swapchain* swapchain, const rect* source_rect, const rect* dest_rect, uint32_t dirty_rect_count, const rect* dirty_rects) {
	if (gl_present) {
		gl_left = !gl_left;
	}
	else {
		gl_left = gl_runtimeLeft;
	}
}

static void onReshadeBeginEffects(effect_runtime* runtime, command_list* cmd_list, resource_view rtv, resource_view rtv_srgb)
{
	auto var = runtime->find_uniform_variable(nullptr, "framecount");
	unsigned int framecount = 0;
	runtime->get_uniform_value_uint(var, &framecount, 1);
	if (framecount > 0)
		gl_runtimeLeft = (framecount % 2) == 0;

	if (runtime->is_key_pressed(VK_F8)) {
		gl_left = !gl_left;
	}

	if (runtime->is_key_pressed(VK_NUMPAD0)) {
		edit = !edit;
		if (!edit) {
			vertexShaders.clear();
			pixelShaders.clear();
			computeShaders.clear();

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
		load_config();
		for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
			PSO* pso = &it->second;
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

	
	if (runtime->is_key_pressed(VK_F11)) {
		filesystem::path fix_path_dump = fix_path / L"Dump";
		filesystem::create_directories(fix_path_dump);
		for (auto it = PSOmap.begin(); it != PSOmap.end(); ++it) {
			PSO* pso = &it->second;
			if (runtime->is_key_down(VK_CONTROL)) {
				if (vertexShaders.count(pso->crcVS) == 1) {
					swprintf_s(sPath, MAX_PATH, L"%08lX-vs.skip", pso->crcVS);
					filesystem::path file = fix_path_dump / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(pso->vsS.code, pso->vsS.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
			else {
				if (pixelShaders.count(pso->crcPS) == 1) {
					swprintf_s(sPath, MAX_PATH, L"%08lX-ps.skip", pso->crcPS);
					filesystem::path file = fix_path_dump / sPath;
					_wfopen_s(&f, file.c_str(), L"wb");
					if (f != 0) {
						auto ASM = asmShader(pso->psS.code, pso->psS.code_size);
						fwrite(ASM.data(), 1, ASM.size(), f);
						fclose(f);
					}
				}
			}
		}
	}

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
			PSO* pso = &it->second;
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
					auto ASM = asmShader(pso->psS.code, pso->psS.code_size);
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
			PSO* pso = &it->second;
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
					auto ASM = asmShader(pso->vsS.code, pso->vsS.code_size);
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
			PSO* pso = &it->second;
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
					auto ASM = asmShader(pso->csS.code, pso->csS.code_size);
					fwrite(ASM.data(), 1, ASM.size(), f);
					fclose(f);
				}
			}
		}
	}

	if (runtime->is_key_down(VK_CONTROL)) {
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

		if (runtime->is_key_down(VK_MENU)) {
			if (runtime->is_key_pressed(VK_F5)) {
				gl_minConv *= 0.8f;
				if (gl_minConv < 0.1)
					gl_minConv = 0;
				reshade::set_config_value(nullptr, "Geo3D", "StereoMinConvergence", gl_minConv);
			}
			if (runtime->is_key_pressed(VK_F6)) {
				gl_minConv *= 1.25f;
				if (gl_minConv == 0)
					gl_minConv = 0.1f;
				reshade::set_config_value(nullptr, "Geo3D", "StereoMinConvergence", gl_minConv);
			}
		}
		else {
			if (runtime->is_key_pressed(VK_F5)) {
				gl_conv *= 0.8f;
				if (gl_conv < 0.1)
					gl_conv = 0.1f;
				reshade::set_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
			}
			if (runtime->is_key_pressed(VK_F6)) {
				gl_conv *= 1.25f;
				reshade::set_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
			}
			if (runtime->is_key_pressed(VK_F7)) {
				gl_Type = !gl_Type;
				gl_conv = 0;
				reshade::set_config_value(nullptr, "Geo3D", "Type", gl_Type);
			}
			if (runtime->is_key_pressed(VK_F8)) {
				gl_DepthZ = !gl_DepthZ;
				gl_conv = 0;
				reshade::set_config_value(nullptr, "Geo3D", "DepthZ", gl_DepthZ);
			}
		}
	}
}

static void load_config()
{
	reshade::get_config_value(nullptr, "Geo3D", "DumpOnly", gl_dumpOnly);
	reshade::get_config_value(nullptr, "Geo3D", "DumpBIN", gl_dumpBIN);
	reshade::get_config_value(nullptr, "Geo3D", "DumpASM", gl_dumpASM);

	reshade::get_config_value(nullptr, "Geo3D", "QuickLoad", gl_quickLoad);
	reshade::get_config_value(nullptr, "Geo3D", "Type", gl_Type);
	reshade::get_config_value(nullptr, "Geo3D", "DepthZ", gl_DepthZ);
	reshade::get_config_value(nullptr, "Geo3D", "Present", gl_present);
	
	reshade::get_config_value(nullptr, "Geo3D", "StereoConvergence", gl_conv);
	reshade::get_config_value(nullptr, "Geo3D", "StereoMinConvergence", gl_minConv);
	reshade::get_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);
	reshade::get_config_value(nullptr, "Geo3D", "StereoScreenSize", gl_screenSize);
	

	if (gl_separation > 100) {
		gl_separation = 100;
		reshade::set_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);
	}
	if (gl_separation < 0) {
		gl_separation = 0;
		reshade::set_config_value(nullptr, "Geo3D", "StereoSeparation", gl_separation);
	}
	bool debug = false;
	reshade::get_config_value(nullptr, "Geo3D", "Debug", debug);
	if (debug) {
		do {
			Sleep(250);
		} while (!IsDebuggerPresent());
	}

	enumerateFiles();
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

		auto api = runtime->get_device()->get_api();
		bool dx9 = api == device_api::d3d9;
		bool dx10 = api == device_api::d3d10;
		bool dx11 = api == device_api::d3d11;
		bool opengl = api == device_api::opengl;
		bool vulkan = api == device_api::vulkan;
		ImGui::Text("Geo3D: %s", gl_2D ? "2D Mode" : "3D Mode");
		if (opengl)
			ImGui::Text("OpenGL");
		else if (vulkan)
			ImGui::Text("Vulkan");
		else
			ImGui::Text("DirectX %s", dx9 ? "9" : dx10 ? "10" : dx11 ? "11" : "12");
		ImGui::Text("Separation %d", gl_separation);
		ImGui::Text("Convergence %.3f", gl_conv);
		ImGui::Text("Min Convergence %.3f", gl_minConv);

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

static void onDestroyPipeline(device* device, reshade::api::pipeline pipelineHandle)
{
	PSOmap.erase(pipelineHandle.handle);
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
	//commandList->draw(vertex_count, instance_count, first_vertex, first_instance)
	// check if for this command list the active shader handles are part of the blocked set. If so, return true
	return blockDrawCallForCommandList(commandList);
}


static bool onDrawIndexed(command_list* commandList, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
	//commandList->draw_indexed(index_count, instance_count, first_index, vertex_offset, first_instance)
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
	fix_path = L"ShaderFixesGeo3D";
	dump_path = L"ShaderCacheGeo3D";
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		if (!reshade::register_addon(hModule))
			return FALSE;
		load_config();

		reshade::register_event<reshade::addon_event::init_pipeline>(onInitPipeline);
		reshade::register_event<reshade::addon_event::bind_pipeline>(onBindPipeline);
		reshade::register_event<reshade::addon_event::reshade_overlay>(onReshadeOverlay);
		reshade::register_event<reshade::addon_event::reshade_begin_effects>(onReshadeBeginEffects);
		reshade::register_event<reshade::addon_event::present>(onPresent);
		
		reshade::register_event<reshade::addon_event::draw>(onDraw);
		reshade::register_event<reshade::addon_event::draw_indexed>(onDrawIndexed);
		reshade::register_event<reshade::addon_event::draw_or_dispatch_indirect>(onDrawOrDispatchIndirect);
		
		reshade::register_event<reshade::addon_event::init_command_list>(onInitCommandList);
		reshade::register_event<reshade::addon_event::destroy_command_list>(onDestroyCommandList);
		reshade::register_event<reshade::addon_event::reset_command_list>(onResetCommandList);
		reshade::register_event<reshade::addon_event::destroy_pipeline>(onDestroyPipeline);
		break;
	case DLL_PROCESS_DETACH:
		reshade::unregister_addon(hModule);
		break;
	}

	return TRUE;
}