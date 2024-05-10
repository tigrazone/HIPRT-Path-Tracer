/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef DEVICE_TEXTURE_H
#define DEVICE_TEXTURE_H

#include "Device/includes/FixIntellisense.h"
#include "HostDeviceCommon/Color.h"

#ifndef __KERNELCC__
#include "Image/Image.h"
#endif

HIPRT_HOST_DEVICE HIPRT_INLINE float luminance(ColorRGB pixel)
{
    return 0.3086f * pixel.r + 0.6094f * pixel.g + 0.0820f * pixel.b;
}

HIPRT_HOST_DEVICE HIPRT_INLINE float luminance(ColorRGBA pixel)
{
    return 0.3086f * pixel.r + 0.6094f * pixel.g + 0.0820f * pixel.b;
}

HIPRT_HOST_DEVICE HIPRT_INLINE ColorRGB sample_texture_pixel(void* texture_pointer, bool is_srgb, float2 uv)
{
    ColorRGBA rgba;

#ifdef __KERNELCC__
    rgba = ColorRGBA(tex2D<float4>(reinterpret_cast<oroTextureObject_t>(texture_pointer), uv.x, uv.y));
#else
    ImageRGBA& envmap = *reinterpret_cast<ImageRGBA*>(texture_pointer);
    rgba = envmap[static_cast<int>(uv.y * envmap.height) * envmap.width + uv.x * envmap.width];
#endif

    // sRGB to linear conversion
    if (is_srgb)
        return pow(ColorRGB(rgba.r, rgba.g, rgba.b), 2.2f);
    else
        return ColorRGB(rgba.r, rgba.g, rgba.b);
}

template <typename T>
HIPRT_HOST_DEVICE HIPRT_INLINE T uv_interpolate(int* vertex_indices, int primitive_index, T* data, float2 uv)
{
    int vertex_A_index = vertex_indices[primitive_index * 3 + 0];
    int vertex_B_index = vertex_indices[primitive_index * 3 + 1];
    int vertex_C_index = vertex_indices[primitive_index * 3 + 2];
    return data[vertex_B_index] * uv.x + data[vertex_C_index] * uv.y + data[vertex_A_index] * (1.0f - uv.x - uv.y);
}

#endif
