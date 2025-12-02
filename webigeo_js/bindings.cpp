/*****************************************************************************
 * weBIGeo
 * Copyright (C) 2025
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <emscripten.h>

#include "AvalancheSimulator.h"

using namespace emscripten;

namespace webigeo_js {

// JavaScript helper functions for async operations
// These are defined in JavaScript and called from C++

EM_JS(void, js_init_webgpu, (uintptr_t simulator_ptr, val resolve, val reject), {
    // This function initiates WebGPU initialization from JavaScript
    // It's called when init() Promise is created

    const simulator = simulator_ptr;

    if (!navigator.gpu) {
        reject("WebGPU is not supported in this browser");
        return;
    }

    navigator.gpu.requestAdapter({ powerPreference: "high-performance" })
        .then(adapter => {
            if (!adapter) {
                reject("Failed to get WebGPU adapter");
                return;
            }

            return adapter.requestDevice({
                requiredFeatures: ["timestamp-query"],
                requiredLimits: {
                    maxStorageBufferBindingSize: 268435456,
                    maxBindGroups: 4,
                    maxColorAttachmentBytesPerSample: 32
                }
            });
        })
        .then(device => {
            if (!device) {
                reject("Failed to get WebGPU device");
                return;
            }

            // Store device reference and call C++ initialization
            Module._webigeo_complete_init(simulator, device);
            resolve();
        })
        .catch(error => {
            reject("WebGPU initialization failed: " + error.message);
        });
});


// C++ functions called from JavaScript helpers
void _completeInit(uintptr_t simulator_ptr)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    simulator->complete_init();
}

void _startRun(uintptr_t simulator_ptr)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    simulator->start_run();
}

void _processEvents(uintptr_t simulator_ptr)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    simulator->process_events();
}

} // namespace webigeo_js

// Embind bindings
EMSCRIPTEN_BINDINGS(webigeo)
{
    using namespace webigeo_js;

    class_<AvalancheSimulator>("AvalancheSimulator")
        .constructor<>()
        .function("init", &AvalancheSimulator::init)
        .function("run", &AvalancheSimulator::run)
        .function("destroy", &AvalancheSimulator::destroy)
        .class_function("isSupported", &AvalancheSimulator::isSupported);

    // Internal functions for JavaScript callbacks
    function("_completeInit", &_completeInit);
    function("_startRun", &_startRun);
    function("_processEvents", &_processEvents);
}

// JavaScript module initialization code
EM_JS(void, setup_js_helpers, (), {
    // Helper to create init promise
    Module._createInitPromise = function(simulatorPtr) {
        return function(resolve, reject) {
            if (!navigator.gpu) {
                reject(new Error("WebGPU is not supported in this browser"));
                return;
            }

            navigator.gpu.requestAdapter({ powerPreference: "high-performance" })
                .then(function(adapter) {
                    if (!adapter) {
                        reject(new Error("Failed to get WebGPU adapter"));
                        return null;
                    }

                    return adapter.requestDevice({
                        requiredLimits: {
                            maxStorageBufferBindingSize: 268435456,
                            maxBindGroups: 4
                        }
                    });
                })
                .then(function(device) {
                    if (!device) {
                        reject(new Error("Failed to get WebGPU device"));
                        return;
                    }

                    // Store device reference globally for C++ to access via preinitializedWebGPUDevice
                    Module.preinitializedWebGPUDevice = device;

                    // Complete initialization in C++ - this sets m_initialized = true
                    Module._completeInit(simulatorPtr);
                    resolve();
                })
                .catch(function(error) {
                    reject(error);
                });
        };
    };

    // Helper to create run promise
    Module._createRunPromise = function(simulatorPtr) {
        return function(resolve, reject) {
            console.log('[JS] _createRunPromise executor called');
            // Store callbacks for C++ to call later
            if (!Module._simulatorCallbacks) {
                Module._simulatorCallbacks = {};
            }
            Module._simulatorCallbacks[simulatorPtr] = { resolve: resolve, reject: reject };

            // Start simulation in C++
            console.log('[JS] Calling _startRun...');
            Module._startRun(simulatorPtr);
            console.log('[JS] _startRun returned, starting poll loop');

            // Poll for WebGPU work completion
            // We need to call _processEvents to poll WebGPU callbacks
            var pollCount = 0;
            function pollWebGPU() {
                // Check if we're still waiting for this simulation
                if (Module._simulatorCallbacks && Module._simulatorCallbacks[simulatorPtr]) {
                    // Process WebGPU events - this fires any pending callbacks
                    Module._processEvents(simulatorPtr);
                    pollCount++;
                    if (pollCount % 60 === 0) {
                        console.log('[JS] Poll #' + pollCount + ' - still waiting...');
                    }
                    // Schedule next poll
                    requestAnimationFrame(pollWebGPU);
                } else {
                    console.log('[JS] Polling stopped after ' + pollCount + ' iterations');
                }
            }
            requestAnimationFrame(pollWebGPU);
        };
    };

    // Helper to check WebGPU support
    Module._createSupportCheckPromise = function() {
        return function(resolve, reject) {
            if (navigator.gpu) {
                navigator.gpu.requestAdapter()
                    .then(function(adapter) {
                        resolve(adapter !== null);
                    })
                    .catch(function() {
                        resolve(false);
                    });
            } else {
                resolve(false);
            }
        };
    };

    // Resolve run promise from C++
    Module._resolveRun = function(simulatorPtr, result) {
        var callbacks = Module._simulatorCallbacks && Module._simulatorCallbacks[simulatorPtr];
        if (callbacks) {
            callbacks.resolve(result);
            delete Module._simulatorCallbacks[simulatorPtr];
        }
    };

    // Reject run promise from C++
    Module._rejectRun = function(simulatorPtr, error) {
        var callbacks = Module._simulatorCallbacks && Module._simulatorCallbacks[simulatorPtr];
        if (callbacks) {
            callbacks.reject(new Error(error));
            delete Module._simulatorCallbacks[simulatorPtr];
        }
    };
});

// Module initialization
__attribute__((constructor))
static void init_module()
{
    setup_js_helpers();
}
