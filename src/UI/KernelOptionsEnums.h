/*
 * Copyright 2024 Tom Clabault. GNU GPL3 license.
 * GNU GPL3 license copy: https://www.gnu.org/licenses/gpl-3.0.txt
 */

#ifndef KERNEL_OPTIONS_ENUMS_H
#define KERNEL_OPTIONS_ENUMS_H

#include "HostDeviceCommon/KernelOptions.h"

enum class InteriorStackStrategyEnum : int
{
	AUTOMATIC = ISS_AUTOMATIC,
	WITH_PRIORITIES = ISS_WITH_PRIORITES
};

enum class DirectLightSamplingStrategyEnum : int
{
	NO_DIRECT_LIGHT_SAMPLING = LSS_NO_DIRECT_LIGHT_SAMPLING,
	ONE_RANDOM_LIGHT = LSS_UNIFORM_ONE_LIGHT,
	ONE_RANDOM_LIGHT_MIS = LSS_MIS_LIGHT_BSDF,
	ONE_RANDOM_LIGHT_RIS = LSS_RIS_ONLY_LIGHT_CANDIDATES
};

#endif