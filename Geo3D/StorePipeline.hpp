#include <reshade.hpp>
#include <vector>
#include <map>

using namespace std;

using namespace reshade::api;
struct PSO {
	float separation;
	float convergence;
	vector<pipeline_subobject> objects;
	pipeline_layout layout;

	shader_desc* vs;
	shader_desc vsCode;
	uint32_t crcVS;
	vector<UINT8> vsEdit;

	shader_desc* ps;
	shader_desc psCode;
	uint32_t crcPS;
	vector<UINT8> psEdit;

	shader_desc* cs;
	shader_desc csCode;
	uint32_t crcCS;
	vector<UINT8> csEdit;

	shader_desc* ds;
	shader_desc dsCode;
	uint32_t crcDS;

	shader_desc* gs;
	shader_desc gsCode;
	uint32_t crcGS;

	uint32_t crcHS;

	bool skip;
	bool noDraw;

	reshade::api::pipeline Left;
	reshade::api::pipeline Right;
};
map<uint64_t, PSO*> PSOmap;

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
				pso->objects.push_back(so);
				break;
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
				if (newShader && so.type == pipeline_subobject_type::vertex_shader) {
					pso->vs = newShader;
					pso->vsCode = *newShader;
				}
				if (newShader && so.type == pipeline_subobject_type::domain_shader) {
					pso->ds = newShader;
					pso->dsCode = *newShader;
				}
				if (newShader && so.type == pipeline_subobject_type::geometry_shader) {
					pso->gs = newShader;
					pso->gsCode = *newShader;
				}
				if (newShader && so.type == pipeline_subobject_type::pixel_shader) {
					pso->ps = newShader;
					pso->psCode = *newShader;
				}
				if (newShader && so.type == pipeline_subobject_type::compute_shader) {
					pso->cs = newShader;
					pso->csCode = *newShader;
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
