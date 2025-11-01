#pragma once

#include "DeferredShadingParametersDebugGUI.h"
#include "RayTracingDebugGUI.h"
extern bgfx::RayTracingConfiguration *gRayTracingConfiguration;
extern dragon::framerenderer::DeferredShadingParameters *gDeferredParams;
void initializeImGui(bool);
void updateImGui();
void updateKeys();
void updateOptions();
