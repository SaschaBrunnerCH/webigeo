/**
 * weBIGeo JavaScript/WebAssembly Bindings
 *
 * GPU-accelerated avalanche simulation using WebGPU.
 *
 * @example
 * ```typescript
 * import createWebigeoModule from './webigeo.js';
 *
 * const module = await createWebigeoModule();
 * const simulator = new module.AvalancheSimulator();
 *
 * await simulator.init();
 *
 * const result = await simulator.run({
 *   aabb: { minX: 1759886, minY: 6069405, maxX: 1761720, maxY: 6070323 },
 *   heightmap: { width: 384, height: 192, data: heightmapData },
 *   releaseCells: { width: 384, height: 192, data: releaseCellsData }
 * }, {
 *   numParticlesPerReleaseCell: 1024,
 *   maxRunoutAngle: 25
 * });
 *
 * console.log('Z-delta:', result.zdelta);
 * simulator.destroy();
 * ```
 */

/**
 * Axis-aligned bounding box in Web Mercator coordinates (EPSG:3857)
 */
export interface AABB {
  /** Minimum X coordinate (meters) */
  minX: number;
  /** Minimum Y coordinate (meters) */
  minY: number;
  /** Maximum X coordinate (meters) */
  maxX: number;
  /** Maximum Y coordinate (meters) */
  maxY: number;
}

/**
 * Texture data (RGBA8 format)
 */
export interface TextureData {
  /** Width in pixels */
  width: number;
  /** Height in pixels */
  height: number;
  /** RGBA8 pixel data (length = width * height * 4) */
  data: Uint8Array;
}

/**
 * Simulation input data
 */
export interface SimulationInput {
  /**
   * Axis-aligned bounding box defining the simulation region
   * in Web Mercator coordinates (EPSG:3857)
   */
  aabb: AABB;

  /**
   * Height raster encoded as RGBA8 texture.
   * Height is encoded in R+G channels as 16-bit value:
   * - R: high 8 bits
   * - G: low 8 bits
   * - Height range: 0 to 8191.875 meters
   */
  heightmap: TextureData;

  /**
   * Release cells mask as RGBA8 texture.
   * A pixel is a release cell if its alpha channel > 0.
   * Must have same dimensions as heightmap.
   */
  releaseCells: TextureData;
}

/**
 * Simulation settings (all optional with defaults matching CLI)
 */
export interface SimulationSettings {
  /**
   * Output resolution multiplier.
   * Output dimensions = input dimensions * multiplier.
   * @default 1
   */
  resolutionMultiplier?: number;

  /**
   * Number of consecutive simulation runs.
   * Results are accumulated across runs.
   * @default 1
   */
  numSimulationRuns?: number;

  /**
   * Number of particles to release from each release cell.
   * @default 1024
   */
  numParticlesPerReleaseCell?: number;

  /**
   * Maximum number of simulation steps per particle.
   * @default 10000
   */
  numSimulationSteps?: number;

  /**
   * Length of a single simulation step (meters).
   * @default 0.1
   */
  simulationStepLength?: number;

  /**
   * Random seed for reproducible results.
   * @default 1
   */
  randomSeed?: number;

  /**
   * Maximum random deviation angle (degrees).
   * Also called theta (θ). Controls randomness in particle direction.
   * @default 25
   */
  maxRandomDeviation?: number;

  /**
   * Direction persistence factor [0, 1].
   * 0 = only local terrain, 1 = only previous direction.
   * @default 0.9
   */
  persistence?: number;

  /**
   * Maximum runout angle (degrees).
   * Also called alpha (α). Particles stop when travel angle exceeds this.
   * Based on FlowPy model.
   * @default 25
   */
  maxRunoutAngle?: number;
}

/**
 * Simulation output data
 */
export interface SimulationOutput {
  /** Output width in pixels */
  width: number;

  /** Output height in pixels */
  height: number;

  /**
   * Maximum Z-delta per cell (meters).
   * Can be used to compute velocity: v = sqrt(2 * zdelta * g)
   */
  zdelta: Float32Array;

  /**
   * Number of particles that passed through each cell.
   */
  cellCounts: Float32Array;

  /**
   * Maximum travel length of particles through each cell (meters).
   */
  travelLength: Float32Array;

  /**
   * Maximum local travel angle at each cell (degrees).
   */
  travelAngle: Float32Array;

  /**
   * Maximum height difference of particles at each cell (meters).
   */
  heightDifference: Float32Array;

  /**
   * Color-mapped trajectories texture (RGBA8).
   * Velocity mapped to color gradient.
   */
  trajectories?: Uint8Array;

  /**
   * Timing information for each compute node (milliseconds).
   */
  timings?: Record<string, number>;
}

/**
 * Avalanche simulator class.
 * Main interface for running GPU-accelerated avalanche simulations.
 */
export declare class AvalancheSimulator {
  /**
   * Create a new simulator instance.
   */
  constructor();

  /**
   * Initialize WebGPU.
   * Must be called before run().
   * @returns Promise that resolves when initialization is complete.
   * @throws Error if WebGPU is not supported or initialization fails.
   */
  init(): Promise<void>;

  /**
   * Run the avalanche simulation.
   * @param input Simulation input data (AABB, heightmap, release cells)
   * @param settings Optional simulation settings
   * @returns Promise that resolves with simulation output.
   * @throws Error if not initialized or simulation fails.
   */
  run(input: SimulationInput, settings?: SimulationSettings): Promise<SimulationOutput>;

  /**
   * Check if WebGPU is supported in the current browser.
   * @returns Promise that resolves to true if WebGPU is available.
   */
  static isSupported(): Promise<boolean>;

  /**
   * Clean up resources.
   * Call when done with the simulator.
   */
  destroy(): void;
}

/**
 * WebAssembly module factory function.
 * Creates and initializes the weBIGeo WASM module.
 */
export interface WebigeoModule {
  AvalancheSimulator: typeof AvalancheSimulator;
}

/**
 * Create the weBIGeo WebAssembly module.
 * @returns Promise that resolves with the initialized module.
 */
declare function createWebigeoModule(): Promise<WebigeoModule>;

export default createWebigeoModule;
