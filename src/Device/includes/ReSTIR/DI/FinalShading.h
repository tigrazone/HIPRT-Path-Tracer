/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef DEVICE_RESTIR_DI_FINAL_SHADING_H
#define DEVICE_RESTIR_DI_FINAL_SHADING_H

#include "Device/includes/Envmap.h"

#include "HostDeviceCommon/Color.h"
#include "HostDeviceCommon/HitInfo.h"
#include "HostDeviceCommon/RenderData.h"

 // TODO make some simplification assuming that ReSTIR DI is never inside a surface (the camera being inside a surface may be an annoying case to handle)
HIPRT_HOST_DEVICE HIPRT_INLINE ColorRGB32F evaluate_ReSTIR_DI_reservoir(const HIPRTRenderData& render_data, const RayPayload& ray_payload, const float3& shading_point, const float3& shading_normal, const float3& view_direction, const ReSTIRDIReservoir& reservoir)
{
    ColorRGB32F final_color;

    if (reservoir.UCW <= 0.0f)
        // No valid sample means no light contribution
        return ColorRGB32F(0.0f);

    ReSTIRDISample sample = reservoir.sample;

    bool in_shadow;
    float distance_to_light;
    float3 evaluated_point = shading_point + shading_normal * 1.0e-4f;

    float3 shadow_ray_direction_normalized;
    if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
    {
        shadow_ray_direction_normalized = sample.point_on_light_source;
        distance_to_light = 1.0e35f;
    }
    else
    {
        float3 shadow_ray_direction = sample.point_on_light_source - evaluated_point;
        shadow_ray_direction_normalized = shadow_ray_direction / (distance_to_light = hippt::length(shadow_ray_direction));
    }
     
    hiprtRay shadow_ray;
    shadow_ray.origin = evaluated_point;
    shadow_ray.direction = shadow_ray_direction_normalized;

    in_shadow = evaluate_shadow_ray(render_data, shadow_ray, distance_to_light);

    if (!in_shadow)
    {
        float bsdf_pdf;
        float cosine_at_evaluated_point;
        ColorRGB32F bsdf_color;
        RayVolumeState trash_volume_state = ray_payload.volume_state;

        bsdf_color = bsdf_dispatcher_eval(render_data.buffers.materials_buffer, ray_payload.material, trash_volume_state, view_direction, shading_normal, shadow_ray_direction_normalized, bsdf_pdf);

        cosine_at_evaluated_point = hippt::max(0.0f, hippt::dot(shading_normal, shadow_ray_direction_normalized));

        if (cosine_at_evaluated_point > 0.0f)
        {
            ColorRGB32F sample_emission;

            if (sample.flags & ReSTIRDISampleFlags::RESTIR_DI_FLAGS_ENVMAP_SAMPLE)
            {
                float envmap_pdf;
                sample_emission = envmap_eval(render_data, shadow_ray_direction_normalized, envmap_pdf);
            }
            else
            {
                int material_index = render_data.buffers.material_indices[sample.emissive_triangle_index];
                sample_emission = render_data.buffers.materials_buffer[material_index].emission;
            }

            final_color = bsdf_color * reservoir.UCW * sample_emission * cosine_at_evaluated_point;
        }
    }

    return final_color;
}

HIPRT_HOST_DEVICE HIPRT_INLINE ColorRGB32F sample_light_ReSTIR_DI(const HIPRTRenderData& render_data, const RayPayload& ray_payload, const HitInfo closest_hit_info, const float3& view_direction, Xorshift32Generator& random_number_generator, int2 pixel_coords, int2 resolution)
{
	int pixel_index = pixel_coords.x + pixel_coords.y * resolution.x;

	// Because the spatial reuse pass runs last, the output buffer of the spatial
	// pass contains the reservoir whose sample we're going to shade
	ReSTIRDIReservoir reservoir = render_data.render_settings.restir_di_settings.restir_output_reservoirs[pixel_index];

	return evaluate_ReSTIR_DI_reservoir(render_data, ray_payload, closest_hit_info.inter_point, closest_hit_info.shading_normal, view_direction, reservoir);
}

#endif
