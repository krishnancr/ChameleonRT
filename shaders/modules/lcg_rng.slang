// Linear Congruential Generator for random number generation
// Ported from backends/dxr/lcg_rng.hlsl

#ifndef LCG_RNG_SLANG
#define LCG_RNG_SLANG

// RNG state structure
struct LCGRand {
    uint state;
};

// Murmur3 hash mixing function for seed initialization
uint murmur_hash3_mix(uint hash, uint k)
{
    const uint c1 = 0xcc9e2d51;
    const uint c2 = 0x1b873593;
    const uint r1 = 15;
    const uint r2 = 13;
    const uint m = 5;
    const uint n = 0xe6546b64;

    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;

    hash ^= k;
    hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;

    return hash;
}

// Murmur3 hash finalization
uint murmur_hash3_finalize(uint hash)
{
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    return hash;
}

// Generate next random integer
uint lcg_random(inout LCGRand rng)
{
    const uint m = 1664525;
    const uint n = 1013904223;
    rng.state = rng.state * m + n;
    return rng.state;
}

// Generate random float in [0, 1)
float lcg_randomf(inout LCGRand rng)
{
    return ldexp(float(lcg_random(rng)), -32);
}

// Initialize RNG with pixel coordinates and frame ID
// Note: This version doesn't use DispatchRaysIndex() to remain standalone
LCGRand get_rng(uint2 pixel, uint2 dims, int frame_id)
{
    LCGRand rng;
    rng.state = murmur_hash3_mix(0, pixel.x + pixel.y * dims.x);
    rng.state = murmur_hash3_mix(rng.state, frame_id);
    rng.state = murmur_hash3_finalize(rng.state);

    return rng;
}

// Convenience function for ray generation shaders
LCGRand get_rng_raygen(int frame_id)
{
    const uint2 pixel = DispatchRaysIndex().xy;
    const uint2 dims = DispatchRaysDimensions().xy;
    return get_rng(pixel, dims, frame_id);
}

#endif // LCG_RNG_SLANG
