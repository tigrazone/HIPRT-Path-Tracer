/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef DEVICE_RESTIR_DI_UTILS_H
#define DEVICE_RESTIR_DI_UTILS_H 

#include "Device/includes/Dispatcher.h"
#include "Device/includes/Envmap.h"
#include "Device/includes/Intersect.h"
#include "Device/includes/LightUtils.h"
#include "Device/includes/ReSTIR/DI/Surface.h"

#include "HostDeviceCommon/RenderData.h"

template <bool withVisiblity>
HIPRT_HOST_DEVICE HIPRT_INLINE float ReSTIR_DI_evaluate_target_function(const HIPRTRenderData& render_data, const ReSTIRDISample& sample, const ReSTIRDISurface& surface)
{
#ifndef __KERNELCC__
	std::cerr << "ReSTIR_DI_evaluate_target_function() wrong specialization called: " << withVisiblity << std::endl;
	Utils::debugbreak();
#endif

	return -1.0f;
}

template <>
HIPRT_HOST_DEVICE HIPRT_INLINE float ReSTIR_DI_evaluate_target_function<KERNEL_OPTION_FALSE>(const HIPRTRenderData& render_data, const ReSTIRDISample& sample, const ReSTIRDISurface& surface)
{
	if (sample.emissive_triangle_index == -1 && !(sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE))
		// Not an envmap sample and no emissive triangle sampled
		return 0.0f;

	float bsdf_pdf;
	float3 sample_direction;

	if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
		sample_direction = sample.point_on_light_source;
	else
		sample_direction = hippt::normalize(sample.point_on_light_source - surface.shading_point);

	float cosine_term = hippt::max(0.0f, hippt::dot(surface.shading_normal, sample_direction));
	if (cosine_term == 0.0f)
		// If the cosine term is 0.0f, the rest is going to be multiplied by that zero-cosine-term
		// and everything is going to be 0.0f anyway so we can return already
		return 0.0f;

	RayVolumeState trash_volume_state = surface.ray_volume_state;
	ColorRGB32F bsdf_color = bsdf_dispatcher_eval(render_data.buffers.materials_buffer, surface.material, trash_volume_state, surface.view_direction, surface.shading_normal, sample_direction, bsdf_pdf);

	ColorRGB32F sample_emission;
	if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
	{
		float envmap_pdf;
		sample_emission = envmap_eval(render_data, sample_direction, envmap_pdf);
	}
	else
	{
		int material_index = render_data.buffers.material_indices[sample.emissive_triangle_index];
		sample_emission = render_data.buffers.materials_buffer[material_index].emission;
	}

	float target_function = (bsdf_color * sample_emission * cosine_term).luminance();
	if (target_function == 0.0f)
		// Quick exit because computing the visiblity that follows isn't going
		// to change anything to the fact that we have 0.0f target function here
		return 0.0f;

	return target_function;
}

template <>
HIPRT_HOST_DEVICE HIPRT_INLINE float ReSTIR_DI_evaluate_target_function<KERNEL_OPTION_TRUE>(const HIPRTRenderData& render_data, const ReSTIRDISample& sample, const ReSTIRDISurface& surface)
{
	if (sample.emissive_triangle_index == -1 && !(sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE))
		// No sample
		return 0.0f;

	float bsdf_pdf;
	float distance_to_light;
	float3 sample_direction;
	if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
	{
		sample_direction = sample.point_on_light_source;
		distance_to_light = 1.0e35f;
	}
	else
	{
		sample_direction = sample.point_on_light_source - surface.shading_point;
		sample_direction = sample_direction / (distance_to_light = hippt::length(sample_direction));
	}

	float cosine_term = hippt::max(0.0f, hippt::dot(surface.shading_normal, sample_direction));
	if (cosine_term == 0.0f)
		// If the cosine term is 0.0f, the rest is going to be multiplied by that zero-cosine-term
		// and everything is going to be 0.0f anyway so we can return already
		return 0.0f;

	RayVolumeState trash_volume_state = surface.ray_volume_state;
	ColorRGB32F bsdf_color = bsdf_dispatcher_eval(render_data.buffers.materials_buffer, surface.material, trash_volume_state, surface.view_direction, surface.shading_normal, sample_direction, bsdf_pdf);

	ColorRGB32F sample_emission;
	if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
	{
		float envmap_pdf;
		sample_emission = envmap_eval(render_data, sample_direction, envmap_pdf);
	}
	else
	{
		int material_index = render_data.buffers.material_indices[sample.emissive_triangle_index];
		sample_emission = render_data.buffers.materials_buffer[material_index].emission;
	}

	float target_function = (bsdf_color * sample_emission * cosine_term).luminance();
	if (target_function == 0.0f)
		// Quick exit because computing the visiblity that follows isn't going
		// to change anything to the fact that we have 0.0f target function here
		return 0.0f;

	hiprtRay shadow_ray;
	shadow_ray.origin = surface.shading_point;
	shadow_ray.direction = sample_direction;

	bool visible = !evaluate_shadow_ray(render_data, shadow_ray, distance_to_light);

	target_function *= visible;

	return target_function;
}

HIPRT_HOST_DEVICE HIPRT_INLINE void ReSTIR_DI_visibility_reuse(const HIPRTRenderData& render_data, ReSTIRDIReservoir& reservoir, float3 shading_point)
{
	if (reservoir.UCW <= 0.0f)
		return;
	// TODO try uncomment this to see performance difference
	/*else if (reservoir.sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_INITIAL_CANDIDATE_UNOCCLUDED)
		return;*/

	float distance_to_light;
	float3 sample_direction = reservoir.sample.point_on_light_source - shading_point;
	if (reservoir.sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
	{
		sample_direction = reservoir.sample.point_on_light_source;
		distance_to_light = 1.0e35f;
	}
	else
		sample_direction /= (distance_to_light = hippt::length(sample_direction));

	hiprtRay shadow_ray;
	shadow_ray.origin = shading_point;
	shadow_ray.direction = sample_direction;

	bool visible = !evaluate_shadow_ray(render_data, shadow_ray, distance_to_light);
	if (!visible)
		// Setting to -1 here so that we know when debugging that this is because of visibility reuse
		reservoir.UCW = -1.0f;
}

HIPRT_HOST_DEVICE HIPRT_INLINE float get_jacobian_determinant_reconnection_shift(const HIPRTRenderData& render_data, const ReSTIRDIReservoir& neighbor_reservoir, const float3& center_pixel_shading_point, const float3& neighbor_shading_point)
{
	float distance_to_light_at_center;
	float distance_to_light_at_neighbor;
	float3 to_light_direction_at_center = neighbor_reservoir.sample.point_on_light_source - center_pixel_shading_point;
	float3 to_light_direction_at_neighbor = neighbor_reservoir.sample.point_on_light_source - neighbor_shading_point;
	to_light_direction_at_center /= (distance_to_light_at_center = hippt::length(to_light_direction_at_center));
	to_light_direction_at_neighbor /= (distance_to_light_at_neighbor = hippt::length(to_light_direction_at_neighbor));

	float3 light_source_normal = hippt::normalize(get_triangle_normal_non_normalized(render_data, neighbor_reservoir.sample.emissive_triangle_index));

	float cosine_light_source_at_center = hippt::abs(hippt::dot(-to_light_direction_at_center, light_source_normal));
	float cosine_light_source_at_neighbor = hippt::abs(hippt::dot(-to_light_direction_at_neighbor, light_source_normal));

	float cosine_ratio = cosine_light_source_at_center / cosine_light_source_at_neighbor;
	float distance_squared_ratio = (distance_to_light_at_neighbor * distance_to_light_at_neighbor) / (distance_to_light_at_center * distance_to_light_at_center);

	float jacobian = cosine_ratio * distance_squared_ratio;

	float jacobian_clamp = 20.0f;
	if (jacobian > jacobian_clamp || jacobian < 1.0f / jacobian_clamp || hippt::isNaN(jacobian))
		// Samples are too dissimilar, returning -1 to indicate that we must reject the sample
		return -1;
	else
		return jacobian;
}

HIPRT_HOST_DEVICE HIPRT_INLINE float get_jacobian_determinant_reconnection_shift(const HIPRTRenderData& render_data, const ReSTIRDIReservoir& neighbor_reservoir, const float3& center_pixel_shading_point, int neighbor_pixel_index)
{
	return get_jacobian_determinant_reconnection_shift(render_data, neighbor_reservoir, center_pixel_shading_point, render_data.g_buffer.first_hits[neighbor_pixel_index]);
}

/**
 * Returns true if the two given points pass the plane distance check, false otherwise
 */
HIPRT_HOST_DEVICE HIPRT_INLINE bool plane_distance_heuristic(const ReSTIRDISettings& restir_di_settings, const float3& temporal_world_space_point, const float3& current_point, const float3& current_surface_normal, float plane_distance_threshold)
{
	if (!restir_di_settings.use_plane_distance_heuristic)
		return true;

	float3 direction_between_points = temporal_world_space_point - current_point;
	float distance_to_plane = hippt::abs(hippt::dot(direction_between_points, current_surface_normal));

	return distance_to_plane < plane_distance_threshold;
}

HIPRT_HOST_DEVICE HIPRT_INLINE bool normal_similarity_heuristic(const ReSTIRDISettings& restir_di_settings, const float3& current_normal, const float3& neighbor_normal, float threshold)
{
	if (restir_di_settings.use_normal_similarity_heuristic)
		return true;

	return hippt::dot(current_normal, neighbor_normal) > threshold;
}

HIPRT_HOST_DEVICE HIPRT_INLINE bool roughness_similarity_heuristic(const ReSTIRDISettings& restir_di_settings, float neighbor_roughness, float center_pixel_roughness, float threshold)
{
	if (!restir_di_settings.use_roughness_similarity_heuristic)
		return true;

	// We don't want to temporally reuse on materials smoother than 0.075f because this
	// causes near-specular/glossy reflections to darken when camera ray jittering is used.
	// 
	// This glossy reflections darkening only happens with confidence weights and 
	// ray jittering but I'm not sure why. Probably because samples from one pixel (or sub-pixel location)
	// cannot efficiently be reused at another pixel (or sub-pixel location through jittering)
	// but confidence weights overweight these bad neighbor samples --> you end up using these
	// bad samples --> the shading loses in energy since we're now shading with samples that
	// don't align well with the glossy reflection direction
	return hippt::abs(neighbor_roughness - center_pixel_roughness) < threshold;
}

HIPRT_HOST_DEVICE HIPRT_INLINE bool check_neighbor_similarity_heuristics(const HIPRTRenderData& render_data, int neighbor_index, int center_pixel_index, const float3& current_shading_point, const float3& current_normal)
{
	float3 neighbor_world_space_point = render_data.g_buffer.first_hits[neighbor_index];

	float neighbor_roughness = render_data.g_buffer.materials[neighbor_index].roughness;
	float current_material_roughness = render_data.g_buffer.materials[center_pixel_index].roughness;

	bool plane_distance_passed = plane_distance_heuristic(render_data.render_settings.restir_di_settings, neighbor_world_space_point, current_shading_point, current_normal, render_data.render_settings.restir_di_settings.plane_distance_threshold);
	bool normal_similarity_passed = normal_similarity_heuristic(render_data.render_settings.restir_di_settings, current_normal, render_data.g_buffer.shading_normals[neighbor_index], render_data.render_settings.restir_di_settings.normal_similarity_angle_precomp);
	bool roughness_similarity_passed = roughness_similarity_heuristic(render_data.render_settings.restir_di_settings, neighbor_roughness, current_material_roughness, render_data.render_settings.restir_di_settings.roughness_similarity_threshold);

	return plane_distance_passed && normal_similarity_passed && roughness_similarity_passed;
}

/**
 * Returns the linear index that can be used directly to index a buffer
 * of render_data of the 'neighbor_number'th neighbor that we're going
 * to spatially reuse from
 *
 * 'neighbor_number' is in [0, neighbor_reuse_count]
 * 'neighbor_reuse_count' is in [1, ReSTIR_DI_Settings.spatial_reuse_neighbor_count]
 * 'neighbor_reuse_radius' is the radius of the disk within which the neighbors are sampled
 * 'center_pixel_coords' is the coordinates of the center pixel that is currently
 *		doing the resampling of its neighbors
 * 'res' is the resolution of the viewport. This is used to check whether the generated
 *		neighbor location is outside of the viewport or not
 * 'cos_sin_theta_rotation' is a pair of float [x, y] with x = cos(random_rotation) and
 *		y = sin(random_rotation). This is used to rotate the points generated by the Hammersley
 *		sampler so that not each pixel on the image resample the exact same neighbors (and so
 *		that a given pixel P resamples different neighbors accros different frame, otherwise
 *		the Hammersley sampler would always generate the exact same points
 * 'rng_converged_neighbor_reuse' is a random generator used specifically for generating
 *		random numbers to test against the 'restir_di_settings.spatial_pass.converged_neighbor_reuse_probability'
 *		if the user has allowed reusing converged neighbors (when adaptive sampling is used).
 *		The same random number generator with the same seed must be given to *all* get_spatial_neighbor_pixel_index()
 *		calls of this thread invocation
 */
HIPRT_HOST_DEVICE HIPRT_INLINE int get_spatial_neighbor_pixel_index(const HIPRTRenderData& render_data, int neighbor_number, int neighbor_reuse_count, int neighbor_reuse_radius, int2 center_pixel_coords, int2 res, float2 cos_sin_theta_rotation, Xorshift32Generator rng_converged_neighbor_reuse)
{
	int neighbor_pixel_index;

	if (neighbor_number == neighbor_reuse_count)
	{
		// If this is the last neighbor, we set it to ourselves
		// This is why our loop on the neighbors goes up to 'i < NEIGHBOR_REUSE_COUNT + 1'
		// It's so that when i == NEIGHBOR_REUSE_COUNT, we resample ourselves
		neighbor_pixel_index = center_pixel_coords.x + center_pixel_coords.y * res.x;
	}
	else
	{
		// +1 and +1 here because we want to skip the first point as it is always (0, 0)
		// which means that we would be resampling ourselves (the center pixel) --> 
		// pointless because we already resample ourselves "manually" (that's why there's that
		// "if (neighbor_number == neighbor_reuse_count)" above, to resample the center pixel)
		float2 uv = sample_hammersley_2D(neighbor_reuse_count + 1, neighbor_number + 1);
		float2 neighbor_offset_in_disk = sample_in_disk_uv(neighbor_reuse_radius, uv);

		// 2D rotation matrix: https://en.wikipedia.org/wiki/Rotation_matrix
		float cos_theta = cos_sin_theta_rotation.x;
		float sin_theta = cos_sin_theta_rotation.y;
		float2 neighbor_offset_rotated = make_float2(neighbor_offset_in_disk.x * cos_theta - neighbor_offset_in_disk.y * sin_theta, neighbor_offset_in_disk.x * sin_theta + neighbor_offset_in_disk.y * cos_theta);
		int2 neighbor_offset_int = make_int2(static_cast<int>(neighbor_offset_rotated.x), static_cast<int>(neighbor_offset_rotated.y));

		int2 neighbor_pixel_coords;
		if (render_data.render_settings.restir_di_settings.spatial_pass.debug_neighbor_location)
			neighbor_pixel_coords = center_pixel_coords + make_int2(15, 0);
		else
			neighbor_pixel_coords = center_pixel_coords + neighbor_offset_int;
		if (neighbor_pixel_coords.x < 0 || neighbor_pixel_coords.x >= res.x || neighbor_pixel_coords.y < 0 || neighbor_pixel_coords.y >= res.y)
			// Rejecting the sample if it's outside of the viewport
			return -1;

		neighbor_pixel_index = neighbor_pixel_coords.x + neighbor_pixel_coords.y * res.x;
		if (render_data.render_settings.enable_adaptive_sampling && render_data.render_settings.sample_number >= render_data.render_settings.adaptive_sampling_min_samples)
		{
			// If adaptive sampling is enabled, we only want to reuse a converged neighbor if the user allowed it
			// We also check whether or not we've reached the minimum amount of samples of adaptive sampling because
			// if adaptive sampling hasn't kicked in yet, there's no need to check whether the neighbor has converged or not yet

			if (render_data.render_settings.restir_di_settings.spatial_pass.allow_converged_neighbors_reuse)
			{
				// If we're allowing the reuse of converged neighbors, only doing so with a certain probability

				if (rng_converged_neighbor_reuse() > render_data.render_settings.restir_di_settings.spatial_pass.converged_neighbor_reuse_probability)
				{
					// We didn't pass the probability check, we are not allowed to reuse the neighbor if it
					// has converged

					if (render_data.aux_buffers.pixel_converged_sample_count[neighbor_pixel_index] != -1)
						// The neighbor is indeed converged, returning invalid neighbor with -1
						return -1;
				}
			}
			else if (render_data.aux_buffers.pixel_converged_sample_count[neighbor_pixel_index] != -1)
				// The user doesn't allow reusing converged neighbors and the neighbor is indeed converged
				// Returning -1 for invalid neighbor
				return -1;
		}
	}

	return neighbor_pixel_index;
}

/**
 * Returns the linear index that can be used directly to index a buffer
 * of render_data for getting data of the temporal neighbor
 */
HIPRT_HOST_DEVICE HIPRT_INLINE int find_temporal_neighbor_index(const HIPRTRenderData& render_data, const float3& current_shading_point, const float3& current_normal, int2 resolution, int2 center_pixel_coords, int center_pixel_index, float center_pixel_roughness, Xorshift32Generator& random_number_generator)
{
	float3 previous_screen_space_point_xyz = matrix_X_point(render_data.prev_camera.view_projection, current_shading_point);
	float2 previous_screen_space_point = make_float2(previous_screen_space_point_xyz.x, previous_screen_space_point_xyz.y);

	// Bringing back in [0, 1] from [-1, 1]
	previous_screen_space_point += make_float2(1.0f, 1.0f);
	previous_screen_space_point *= make_float2(0.5f, 0.5f);

	float2 prev_pixel_float = make_float2(previous_screen_space_point.x * resolution.x, previous_screen_space_point.y * resolution.y);
	// Bringing back in the center of the pixel
	prev_pixel_float -= make_float2(0.5f, 0.5f);

	// We're going to randomly look for an acceptable neighbor around the back-projected pixel location to find
	// in a given radius
	int temporal_neighbor_index = -1;
	for (int i = 0; i < render_data.render_settings.restir_di_settings.temporal_pass.max_neighbor_search_count + 1; i++)
	{
		float2 offset = make_float2(0.0f, 0.0f);
		if (i > 0)
			// Only randomly looking after we've at least checked whether or not the exact temporally reprojected location
			// is valid or not
			offset = make_float2(random_number_generator() - 0.5f, random_number_generator() - 0.5f) * render_data.render_settings.restir_di_settings.temporal_pass.neighbor_search_radius;

		int2 temporal_neighbor_screen_pixel_pos = make_int2(round(prev_pixel_float.x + offset.x), round(prev_pixel_float.y + offset.y));
		if (temporal_neighbor_screen_pixel_pos.x < 0 || temporal_neighbor_screen_pixel_pos.x >= resolution.x || temporal_neighbor_screen_pixel_pos.y < 0 || temporal_neighbor_screen_pixel_pos.y >= resolution.y)
			// Previous pixel is out of the current viewport
			continue;

		temporal_neighbor_index = temporal_neighbor_screen_pixel_pos.x + temporal_neighbor_screen_pixel_pos.y * resolution.x;
		if (check_neighbor_similarity_heuristics(render_data, temporal_neighbor_index, center_pixel_index, current_shading_point, current_normal))
			// We found a good neighbor
			break;

		// We didn't break so we didn't find a good neighbor
		temporal_neighbor_index = -1;
	}

	return temporal_neighbor_index;
}

#endif
