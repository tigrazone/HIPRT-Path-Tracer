/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#include "renderer.h"

void Renderer::render(const OpenImageDenoiser& denoiser)
{
	int tile_size_x = 8;
	int tile_size_y = 8;

	hiprtInt2 nb_groups;
	nb_groups.x = std::ceil(m_render_width / (float)tile_size_x);
	nb_groups.y = std::ceil(m_render_height / (float)tile_size_y);

	hiprtInt2 resolution = make_hiprtInt2(m_render_width, m_render_height);

	HIPRTCamera hiprt_cam = m_camera.to_hiprt();
	HIPRTRenderData render_data = get_render_data(denoiser);
	void* launch_args[] = { &m_hiprt_scene.geometry, &render_data, &resolution, &hiprt_cam};
	launch_kernel(8, 8, resolution.x, resolution.y, launch_args);
}

void Renderer::change_render_resolution(int new_width, int new_height)
{
	m_render_width = new_width;
	m_render_height = new_height;

	m_pixels_buffer.resize(new_width * new_height * 3);
	m_normals_buffer.resize(new_width * new_height);
	m_albedo_buffer.resize(new_width * new_height);

	// Recomputing the perspective projection matrix since the aspect ratio
	// may have changed
	float new_aspect = (float)new_width / new_height;
	m_camera.projection_matrix = glm::transpose(glm::perspective(m_camera.vertical_fov, new_aspect, m_camera.near_plane, m_camera.far_plane));
}

OrochiBuffer<Color>& Renderer::get_color_framebuffer()
{
	return m_pixels_buffer;
}

OrochiBuffer<Color>& Renderer::get_denoiser_albedo_buffer()
{
	return m_albedo_buffer;
}

OrochiBuffer<float3>& Renderer::get_denoiser_normals_buffer()
{
	return m_normals_buffer;
}

RenderSettings& Renderer::get_render_settings()
{
	return m_render_settings;
}

WorldSettings& Renderer::get_world_settings()
{
	return m_world_settings;
}

int Renderer::get_sample_number()
{
	return m_render_settings.sample_number;
}

void Renderer::set_sample_number(int sample_number)
{
	m_render_settings.sample_number = sample_number;
}

HIPRTRenderData Renderer::get_render_data(const OpenImageDenoiser& denoiser)
{
	HIPRTRenderData render_data;

	render_data.geom = m_hiprt_scene.geometry;

	render_data.buffers.pixels = m_pixels_buffer.get_pointer();
	render_data.buffers.denoiser_normals = m_normals_buffer.get_pointer();
	render_data.buffers.denoiser_albedo = m_albedo_buffer.get_pointer();
	render_data.buffers.triangles_indices = reinterpret_cast<int*>(m_hiprt_scene.mesh.triangleIndices);
	render_data.buffers.triangles_vertices = reinterpret_cast<float3*>(m_hiprt_scene.mesh.vertices);
	render_data.buffers.normals_present = reinterpret_cast<unsigned char*>(m_hiprt_scene.normals_present);
	render_data.buffers.vertex_normals = reinterpret_cast<float3*>(m_hiprt_scene.vertex_normals);
	render_data.buffers.material_indices = reinterpret_cast<int*>(m_hiprt_scene.material_indices);
	render_data.buffers.materials_buffer = reinterpret_cast<RendererMaterial*>(m_hiprt_scene.materials_buffer);
	render_data.buffers.emissive_triangles_count = m_hiprt_scene.emissive_triangles_count;
	render_data.buffers.emissive_triangles_indices = reinterpret_cast<int*>(m_hiprt_scene.emissive_triangles_indices);

	render_data.world_settings = m_world_settings;
	//render_data.render_settings = m_render_settings;

	render_data.render_settings.frame_number = m_render_settings.frame_number;
	render_data.render_settings.sample_number = m_render_settings.sample_number;
	render_data.render_settings.samples_per_frame = m_render_settings.samples_per_frame;
	render_data.render_settings.nb_bounces = m_render_settings.nb_bounces;
	render_data.render_settings.render_low_resolution = m_render_settings.render_low_resolution;

	return render_data;
}

void Renderer::init_ctx(int device_index)
{
	m_hiprt_orochi_ctx = std::make_shared<HIPRTOrochiCtx>();
	m_hiprt_orochi_ctx.get()->init(device_index);
}









































bool customReadSourceCode(const std::filesystem::path& srcPath, std::string& sourceCode, std::optional<std::vector<std::filesystem::path>> includes = std::nullopt);

bool customReadSourceCode(
	const std::filesystem::path& srcPath, std::string& sourceCode, std::optional<std::vector<std::filesystem::path>> includes)
{
	std::fstream f(srcPath);
	if (f.is_open())
	{
		size_t sizeFile;
		f.seekg(0, std::fstream::end);
		size_t size = sizeFile = static_cast<size_t>(f.tellg());
		f.seekg(0, std::fstream::beg);
		if (includes)
		{
			sourceCode.clear();
			std::string line;
			while (std::getline(f, line))
			{
				if (line.find("#include") != std::string::npos)
				{
					size_t		pa = line.find("<");
					size_t		pb = line.find(">");
					std::string buf = line.substr(pa + 1, pb - pa - 1);
					includes.value().push_back(buf);
					sourceCode += line + '\n';
				}
				sourceCode += line + '\n';
			}
		}
		else
		{
			sourceCode.resize(size, ' ');
			f.read(&sourceCode[0], size);
		}
		f.close();
	}
	else
		return false;
	return true;
}

hiprtError customBuildTraceKernels(
	hiprtContext								 ctxt,
	const std::filesystem::path& srcPath,
	std::vector<const char*>					 functionNames,
	std::vector<hiprtApiFunction>& functionsOut,
	std::optional<std::vector<const char*>>		 opts,
	std::optional<std::vector<hiprtFuncNameSet>> funcNameSets,
	uint32_t									 numGeomTypes,
	uint32_t									 numRayTypes)
{
	std::vector<std::filesystem::path> includeNamesData;
	std::string						   sourceCode;
	customReadSourceCode(srcPath, sourceCode, includeNamesData);

	std::vector<std::string> headersData(includeNamesData.size());
	std::vector<const char*> headers;
	std::vector<const char*> includeNames;
	for (size_t i = 0; i < includeNamesData.size(); i++)
	{
		customReadSourceCode(std::filesystem::path("../") / includeNamesData[i], headersData[i]);
		includeNames.push_back(includeNamesData[i].string().c_str());
		headers.push_back(headersData[i].c_str());
	}

	functionsOut.resize(functionNames.size());
	return hiprtBuildTraceKernels(
		ctxt,
		static_cast<uint32_t>(functionNames.size()),
		functionNames.data(),
		sourceCode.c_str(),
		srcPath.string().c_str(),
		static_cast<uint32_t>(headers.size()),
		headers.data(),
		includeNames.data(),
		opts ? static_cast<uint32_t>(opts.value().size()) : 0,
		opts ? opts.value().data() : nullptr,
		numGeomTypes,
		numRayTypes,
		funcNameSets ? funcNameSets.value().data() : nullptr,
		functionsOut.data(),
		nullptr,
		true);
}

void Renderer::compile_trace_kernel(const char* kernel_file_path, const char* kernel_function_name)
{
	std::vector<std::string> include_paths = { "./", "../thirdparties/hiprt/include" };
	std::vector<std::pair<std::string, std::string>> precompiler_defines;
	std::vector<const char*> options;

	std::string additional_include = std::string("-I") + std::string(KERNEL_COMPILER_ADDITIONAL_INCLUDE);
	options.push_back(additional_include.c_str());
	options.push_back("-I./");

	std::vector<hiprtApiFunction> functionsOut(1);
	customBuildTraceKernels(m_hiprt_orochi_ctx->hiprt_ctx, kernel_file_path, { kernel_function_name }, functionsOut, options, std::nullopt, 0, 1);
	m_trace_kernel = *reinterpret_cast<oroFunction*>(&functionsOut.back());
}


























































void Renderer::launch_kernel(int tile_size_x, int tile_size_y, int res_x, int res_y, void** launch_args)
{
	hiprtInt2 nb_groups;
	nb_groups.x = std::ceil(static_cast<float>(res_x) / tile_size_x);
	nb_groups.y = std::ceil(static_cast<float>(res_y) / tile_size_y);

	OROCHI_CHECK_ERROR(oroModuleLaunchKernel(m_trace_kernel, nb_groups.x, nb_groups.y, 1, tile_size_x, tile_size_y, 1, 0, 0, launch_args, 0));
}

void Renderer::set_hiprt_scene_from_scene(Scene& scene)
{
	m_hiprt_scene = HIPRTScene(m_hiprt_orochi_ctx->hiprt_ctx);
	HIPRTScene& hiprt_scene = m_hiprt_scene;
	
	hiprtTriangleMeshPrimitive& mesh = hiprt_scene.mesh;

	// Allocating and initializing the indices buffer
	mesh.triangleCount = scene.triangle_indices.size() / 3;
	mesh.triangleStride = sizeof(int3);
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&mesh.triangleIndices), mesh.triangleCount * sizeof(int3)));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(mesh.triangleIndices), scene.triangle_indices.data(), mesh.triangleCount * sizeof(int3)));

	// Allocating and initializing the vertices positions buiffer
	mesh.vertexCount = scene.vertices_positions.size();
	mesh.vertexStride = sizeof(float3);
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&mesh.vertices), mesh.vertexCount * sizeof(float3)));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(mesh.vertices), scene.vertices_positions.data(), mesh.vertexCount * sizeof(float3)));

	hiprtGeometryBuildInput geometry_build_input;
	geometry_build_input.type = hiprtPrimitiveTypeTriangleMesh;
	geometry_build_input.primitive.triangleMesh = hiprt_scene.mesh;

	// Getting the buffer sizes for the construction of the BVH
	size_t geometry_temp_size;
	hiprtDevicePtr geometry_temp;
	hiprtBuildOptions build_options;
	build_options.buildFlags = hiprtBuildFlagBitPreferFastBuild;

	HIPRT_CHECK_ERROR(hiprtGetGeometryBuildTemporaryBufferSize(m_hiprt_orochi_ctx->hiprt_ctx, geometry_build_input, build_options, geometry_temp_size));
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&geometry_temp), geometry_temp_size));

	// Building the BVH
	hiprtGeometry& scene_geometry = hiprt_scene.geometry;
	HIPRT_CHECK_ERROR(hiprtCreateGeometry(m_hiprt_orochi_ctx->hiprt_ctx, geometry_build_input, build_options, scene_geometry));
	HIPRT_CHECK_ERROR(hiprtBuildGeometry(m_hiprt_orochi_ctx->hiprt_ctx, hiprtBuildOperationBuild, geometry_build_input, build_options, geometry_temp, 0, scene_geometry));

	OROCHI_CHECK_ERROR(oroFree(reinterpret_cast<oroDeviceptr>(geometry_temp)));

	// TODO, use orochiBuffers here
	hiprtDevicePtr normals_present_buffer;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&normals_present_buffer), sizeof(unsigned char) * scene.normals_present.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(normals_present_buffer), scene.normals_present.data(), sizeof(unsigned char) * scene.normals_present.size()));
	hiprt_scene.normals_present = normals_present_buffer;

	hiprtDevicePtr vertex_normals_buffer;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&vertex_normals_buffer), sizeof(float3) * scene.vertex_normals.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(vertex_normals_buffer), scene.vertex_normals.data(), sizeof(float3) * scene.vertex_normals.size()));
	hiprt_scene.vertex_normals = vertex_normals_buffer;

	hiprtDevicePtr material_indices_buffer;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&material_indices_buffer), sizeof(int) * scene.material_indices.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(material_indices_buffer), scene.material_indices.data(), sizeof(int) * scene.material_indices.size()));
	hiprt_scene.material_indices = material_indices_buffer;

	hiprtDevicePtr materials_buffer;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&materials_buffer), sizeof(RendererMaterial) * scene.materials.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(materials_buffer), scene.materials.data(), sizeof(RendererMaterial) * scene.materials.size()));
	hiprt_scene.materials_buffer = materials_buffer;

	hiprt_scene.emissive_triangles_count = scene.emissive_triangle_indices.size();

	hiprtDevicePtr emissive_triangle_indices;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&emissive_triangle_indices), sizeof(int) * scene.emissive_triangle_indices.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(emissive_triangle_indices), scene.emissive_triangle_indices.data(), sizeof(int) * scene.emissive_triangle_indices.size()));
	hiprt_scene.emissive_triangles_indices = emissive_triangle_indices;
}

void Renderer::set_scene(Scene& scene)
{
	set_hiprt_scene_from_scene(scene);
	m_materials = scene.materials;
}

const std::vector<RendererMaterial>& Renderer::get_materials()
{
	return m_materials;
}

void Renderer::update_materials(std::vector<RendererMaterial>& materials)
{
	m_materials = materials;

	if (m_hiprt_scene.materials_buffer)
		OROCHI_CHECK_ERROR(oroFree(reinterpret_cast<oroDeviceptr>(m_hiprt_scene.materials_buffer)));

	hiprtDevicePtr materials_buffer;
	OROCHI_CHECK_ERROR(oroMalloc(reinterpret_cast<oroDeviceptr*>(&materials_buffer), sizeof(RendererMaterial) * materials.size()));
	OROCHI_CHECK_ERROR(oroMemcpyHtoD(reinterpret_cast<oroDeviceptr>(materials_buffer), materials.data(), sizeof(RendererMaterial) * materials.size()));
	m_hiprt_scene.materials_buffer = materials_buffer;
}

void Renderer::set_camera(const Camera& camera)
{
	m_camera = camera;
}

void Renderer::translate_camera_view(glm::vec3 translation)
{
	m_camera.translation = m_camera.translation + translation * glm::conjugate(m_camera.rotation);
}

void Renderer::rotate_camera_view(glm::vec3 rotation_angles)
{
	glm::quat qx = glm::angleAxis(rotation_angles.y, glm::vec3(1.0f, 0.0f, 0.0f));
	glm::quat qy = glm::angleAxis(rotation_angles.x, glm::vec3(0.0f, 1.0f, 0.0f));

	glm::quat orientation = glm::normalize(qy * m_camera.rotation * qx);
	m_camera.rotation = orientation;
}

void Renderer::zoom_camera_view(float offset)
{
	glm::vec3 translation(0, 0, offset);
	m_camera.translation = m_camera.translation + translation * glm::conjugate(m_camera.rotation);
}
