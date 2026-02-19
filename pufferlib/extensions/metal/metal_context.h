/**
 * @fileoverview Metal compute context singleton for PufferLib GPU kernels.
 *
 * Manages MTLDevice and JIT-compiled shader library. Kernel dispatch uses
 * PyTorch MPS's native stream pattern with zero-copy access to MPS tensor
 * backing storage via Apple Silicon unified memory.
 */

#ifndef PUFFERLIB_METAL_CONTEXT_H
#define PUFFERLIB_METAL_CONTEXT_H

#ifdef WITH_METAL

#include <torch/extension.h>

// Initialize the Metal context (device, compile shaders).
// Called once from create_pufferl_impl when device == "mps".
void metal_init();

// Check whether Metal context is initialized and ready.
bool metal_is_ready();

// Synchronize the MPS stream (commit and wait for all submitted GPU work).
void metal_synchronize();

// --- Kernel dispatch functions ---
// Each kernel has a dedicated C++ function declared in metal_kernels.h.
// The context provides the device/library internally.

#endif // WITH_METAL
#endif // PUFFERLIB_METAL_CONTEXT_H
