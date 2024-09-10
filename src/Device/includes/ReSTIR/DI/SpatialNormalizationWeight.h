/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef DEVICE_RESTIR_DI_SPATIAL_NORMALIZATION_WEIGHT_H
#define DEVICE_RESTIR_DI_SPATIAL_NORMALIZATION_WEIGHT_H

#include "Device/includes/ReSTIR/DI/Utils.h"

template <int BiasCorrectionMode>
struct ReSTIRDISpatialNormalizationWeight {};

template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_1_OVER_M>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		if (final_reservoir.weight_sum <= 0)
		{
			// Invalid reservoir, returning directly
			out_normalization_nume = 1.0;
			out_normalization_denom = 1.0f;

			return;
		}

		// 1/M MIS weights are basically confidence weights only i.e. c_i / sum(c_j) with
		// c_i = r_i.M

		out_normalization_nume = 1.0f;
		// We're simply going to divide by the sum of all the M values of all the neighbors we resampled (including the center pixel)
		// so we're only going to set the denominator to that and the numerator isn't going to change
		out_normalization_denom = 0.0f;
		for (int neighbor = 0; neighbor < reused_neighbors_count + 1; neighbor++)
		{
			int neighbor_pixel_index = get_spatial_neighbor_pixel_index(render_data, neighbor, reused_neighbors_count, render_data.render_settings.restir_di_settings.spatial_pass.spatial_reuse_radius, center_pixel_coords, res, cos_sin_theta_rotation, Xorshift32Generator(render_data.random_seed));
			if (neighbor_pixel_index == -1)
				// Neighbor out of the viewport
				continue;

			int center_pixel_index = center_pixel_coords.x + center_pixel_coords.y * res.x;
			if (!check_neighbor_similarity_heuristics(render_data, neighbor_pixel_index, center_pixel_index, center_pixel_surface.shading_point, center_pixel_surface.shading_normal))
				continue;

			ReSTIRDIReservoir neighbor_reservoir = render_data.render_settings.restir_di_settings.spatial_pass.input_reservoirs[neighbor_pixel_index];
			out_normalization_denom += neighbor_reservoir.M;
		}
	}
};

template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_1_OVER_Z>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		if (final_reservoir.weight_sum <= 0)
		{
			// Invalid reservoir, returning directly
			out_normalization_nume = 1.0;
			out_normalization_denom = 1.0f;

			return;
		}

		// Checking how many of our neighbors could have produced the sample that we just picked
		// and we're going to divide by the sum of M values of those neighbors
		out_normalization_denom = 0.0f;
		out_normalization_nume = 1.0f;

		for (int neighbor = 0; neighbor < reused_neighbors_count + 1; neighbor++)
		{
			int neighbor_pixel_index = get_spatial_neighbor_pixel_index(render_data, neighbor, reused_neighbors_count, render_data.render_settings.restir_di_settings.spatial_pass.spatial_reuse_radius, center_pixel_coords, res, cos_sin_theta_rotation, Xorshift32Generator(render_data.random_seed));
			if (neighbor_pixel_index == -1)
				// Invalid neighbor
				continue;

			int center_pixel_index = center_pixel_coords.x + center_pixel_coords.y * res.x;
			if (!check_neighbor_similarity_heuristics(render_data, neighbor_pixel_index, center_pixel_index, center_pixel_surface.shading_point, center_pixel_surface.shading_normal))
				continue;

			// Getting the surface data at the neighbor
			ReSTIRDISurface neighbor_surface = get_pixel_surface(render_data, neighbor_pixel_index);

			float target_function_at_neighbor = 0.0f;
			if (render_data.render_settings.restir_di_settings.spatial_pass.spatial_pass_index == 0)
				target_function_at_neighbor = ReSTIR_DI_evaluate_target_function<ReSTIR_DI_BiasCorrectionUseVisiblity>(render_data, final_reservoir.sample, neighbor_surface);
			else
				target_function_at_neighbor = ReSTIR_DI_evaluate_target_function<KERNEL_OPTION_FALSE>(render_data, final_reservoir.sample, neighbor_surface);

			if (target_function_at_neighbor > 0.0f)
			{
				// If the neighbor could have produced this sample...
				ReSTIRDIReservoir neighbor_reservoir = render_data.render_settings.restir_di_settings.spatial_pass.input_reservoirs[neighbor_pixel_index];

				out_normalization_denom += neighbor_reservoir.M;
			}
		}
	};
};





template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_MIS_LIKE>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		if (final_reservoir.weight_sum <= 0)
		{
			// Invalid reservoir, returning directly
			out_normalization_nume = 1.0;
			out_normalization_denom = 1.0f;

			return;
		}

		out_normalization_denom = 0.0f;
		out_normalization_nume = 0.0f;

		for (int neighbor = 0; neighbor < reused_neighbors_count + 1; neighbor++)
		{
			int neighbor_pixel_index = get_spatial_neighbor_pixel_index(render_data, neighbor, reused_neighbors_count, render_data.render_settings.restir_di_settings.spatial_pass.spatial_reuse_radius, center_pixel_coords, res, cos_sin_theta_rotation, Xorshift32Generator(render_data.random_seed));
			if (neighbor_pixel_index == -1)
				// Invalid neighbor
				continue;

			int center_pixel_index = center_pixel_coords.x + center_pixel_coords.y * res.x;
			if (!check_neighbor_similarity_heuristics(render_data, neighbor_pixel_index, center_pixel_index, center_pixel_surface.shading_point, center_pixel_surface.shading_normal))
				continue;

			// Getting the surface data at the neighbor
			ReSTIRDISurface neighbor_surface = get_pixel_surface(render_data, neighbor_pixel_index);

			float target_function_at_neighbor = ReSTIR_DI_evaluate_target_function<ReSTIR_DI_BiasCorrectionUseVisiblity>(render_data, final_reservoir.sample, neighbor_surface);

			if (target_function_at_neighbor > 0.0f)
			{
				// If the neighbor could have produced this sample...
				ReSTIRDIReservoir neighbor_reservoir = render_data.render_settings.restir_di_settings.spatial_pass.input_reservoirs[neighbor_pixel_index];

				if (neighbor == selected_neighbor)
					out_normalization_nume += target_function_at_neighbor;
				out_normalization_denom += target_function_at_neighbor;
			};
		}
	}
};

template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_MIS_LIKE_CONFIDENCE_WEIGHTS>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		if (final_reservoir.weight_sum <= 0)
		{
			// Invalid reservoir, returning directly
			out_normalization_nume = 1.0;
			out_normalization_denom = 1.0f;

			return;
		}

		out_normalization_denom = 0.0f;
		out_normalization_nume = 0.0f;

		for (int neighbor = 0; neighbor < reused_neighbors_count + 1; neighbor++)
		{
			int neighbor_pixel_index = get_spatial_neighbor_pixel_index(render_data, neighbor, reused_neighbors_count, render_data.render_settings.restir_di_settings.spatial_pass.spatial_reuse_radius, center_pixel_coords, res, cos_sin_theta_rotation, Xorshift32Generator(render_data.random_seed));
			if (neighbor_pixel_index == -1)
				// Invalid neighbor
				continue;

			int center_pixel_index = center_pixel_coords.x + center_pixel_coords.y * res.x;
			if (!check_neighbor_similarity_heuristics(render_data, neighbor_pixel_index, center_pixel_index, center_pixel_surface.shading_point, center_pixel_surface.shading_normal))
				continue;

			// Getting the surface data at the neighbor
			ReSTIRDISurface neighbor_surface = get_pixel_surface(render_data, neighbor_pixel_index);

			float target_function_at_neighbor = ReSTIR_DI_evaluate_target_function<ReSTIR_DI_BiasCorrectionUseVisiblity>(render_data, final_reservoir.sample, neighbor_surface);

			if (target_function_at_neighbor > 0.0f)
			{
				// If the neighbor could have produced this sample...
				ReSTIRDIReservoir neighbor_reservoir = render_data.render_settings.restir_di_settings.spatial_pass.input_reservoirs[neighbor_pixel_index];

				if (neighbor == selected_neighbor)
					// Not multiplying by M here, this was done already when resampling the sample
					out_normalization_nume += target_function_at_neighbor;
				out_normalization_denom += target_function_at_neighbor * neighbor_reservoir.M;
			};
		}
	}
};





template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_MIS_GBH>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		// Nothing more to normalize, everything is already handled when resampling the neighbors with balance heuristic MIS weights in the m_i terms
		out_normalization_nume = 1.0f;
		out_normalization_denom = 1.0f;
	}
};

template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_MIS_GBH_CONFIDENCE_WEIGHTS>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		// Nothing more to normalize, everything is already handled when resampling the neighbors with balance heuristic MIS weights in the m_i terms
		out_normalization_nume = 1.0f;
		out_normalization_denom = 1.0f;
	}
};





template <>
struct ReSTIRDISpatialNormalizationWeight<RESTIR_DI_BIAS_CORRECTION_PAIRWISE_MIS>
{
	HIPRT_HOST_DEVICE void get_normalization(const HIPRTRenderData& render_data,
		const ReSTIRDIReservoir& final_reservoir, const ReSTIRDISurface& center_pixel_surface,
		int selected_neighbor, int reused_neighbors_count,
		int2 center_pixel_coords, int2 res,
		float2 cos_sin_theta_rotation,
		Xorshift32Generator& random_number_generator, float& out_normalization_nume, float& out_normalization_denom)
	{
		// Nothing more to normalize, everything is already handled when resampling the neighbors
		out_normalization_nume = 1.0f;
		out_normalization_denom = 1.0f;
	}
};

#endif