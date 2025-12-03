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

val _getReadbackBufferInfo(uintptr_t simulator_ptr)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    return simulator->get_readback_buffer_info();
}

void _setReadbackData(uintptr_t simulator_ptr, val data)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    simulator->set_readback_data(data);
}

void _setColorMapBounds(uintptr_t simulator_ptr, const std::string& layerName, float minValue, float maxValue)
{
    auto* simulator = reinterpret_cast<AvalancheSimulator*>(simulator_ptr);
    simulator->set_color_map_bounds(layerName, minValue, maxValue);
}

val _getDefaultColorMapBounds()
{
    return AvalancheSimulator::get_default_color_map_bounds();
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
    function("_getReadbackBufferInfo", &_getReadbackBufferInfo);
    function("_setReadbackData", &_setReadbackData);
    function("_setColorMapBounds", &_setColorMapBounds);
    function("_getDefaultColorMapBounds", &_getDefaultColorMapBounds);
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

    // Resolve run promise from C++ - with async buffer readback for all layers
    Module._resolveRun = async function(simulatorPtr, result) {
        var callbacks = Module._simulatorCallbacks && Module._simulatorCallbacks[simulatorPtr];
        if (!callbacks) {
            console.error('[JS] _resolveRun: No callbacks for simulator', simulatorPtr);
            return;
        }

        try {
            // Get readback buffer info from C++ (now contains all 5 layers)
            var bufferInfo = Module._getReadbackBufferInfo(simulatorPtr);
            console.log('[JS] Buffer info:', bufferInfo);

            if (bufferInfo.valid && bufferInfo.layers && Module.preinitializedWebGPUDevice) {
                console.log('[JS] Performing async buffer readback for all layers...');

                // Process each layer
                var layerData = {};
                var firstWidth = 0;
                var firstHeight = 0;

                for (var i = 0; i < bufferInfo.layers.length; i++) {
                    var layerInfo = bufferInfo.layers[i];
                    if (!layerInfo.valid) {
                        console.warn('[JS] Layer', layerInfo.name, 'not valid');
                        continue;
                    }

                    console.log('[JS] Processing layer:', layerInfo.name);

                    // Get the WebGPU buffer object from the pointer
                    var buffer = null;
                    if (typeof WebGPU !== 'undefined' && WebGPU.getJsObject) {
                        buffer = WebGPU.getJsObject(layerInfo.bufferPtr);
                    }

                    if (buffer) {
                        // Map the buffer asynchronously
                        await buffer.mapAsync(GPUMapMode.READ);
                        console.log('[JS] Buffer mapped for layer:', layerInfo.name);

                        // Get the mapped range
                        var mappedRange = buffer.getMappedRange();
                        var srcData = new Uint8Array(mappedRange);

                        // Copy data, removing row padding
                        var width = layerInfo.width;
                        var height = layerInfo.height;
                        var paddedBytesPerRow = layerInfo.paddedBytesPerRow;
                        var unpaddedBytesPerRow = layerInfo.unpaddedBytesPerRow;

                        var imageData = new Uint8Array(width * height * 4);
                        for (var y = 0; y < height; y++) {
                            var srcOffset = y * paddedBytesPerRow;
                            var dstOffset = y * unpaddedBytesPerRow;
                            imageData.set(srcData.subarray(srcOffset, srcOffset + unpaddedBytesPerRow), dstOffset);
                        }

                        // Unmap the buffer
                        buffer.unmap();

                        console.log('[JS] Copied', imageData.length, 'bytes for layer:', layerInfo.name);

                        // Store layer data
                        layerData[layerInfo.name] = imageData;

                        // Store dimensions from first layer
                        if (firstWidth === 0) {
                            firstWidth = width;
                            firstHeight = height;
                        }
                    } else {
                        console.warn('[JS] Could not get WebGPU buffer for layer:', layerInfo.name);
                    }
                }

                // Send all layer data back to C++
                Module._setReadbackData(simulatorPtr, layerData);

                // Update result with all layers
                result.layers = layerData;
                result.width = firstWidth;
                result.height = firstHeight;

                // For backwards compatibility, set imageData to zdelta
                if (layerData.zdelta) {
                    result.imageData = layerData.zdelta;
                }

                console.log('[JS] All layers processed, width=' + firstWidth + ', height=' + firstHeight);
            } else {
                console.log('[JS] No valid buffer for readback');
            }

            callbacks.resolve(result);
        } catch (error) {
            console.error('[JS] Buffer readback error:', error);
            // Still resolve with the result we have
            callbacks.resolve(result);
        } finally {
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
