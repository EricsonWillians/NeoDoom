#pragma once

#ifdef NEODOOM_GLTF_SUPPORT

#include "printf.h"

//===========================================================================
//
// Debug and Logging Support for glTF Models
//
//===========================================================================

// Debug levels for glTF logging
enum class GLTFDebugLevel
{
    None = 0,
    Error = 1,      // Only errors
    Warning = 2,    // Errors and warnings
    Info = 3,       // Basic information
    Verbose = 4,    // Detailed information
    Debug = 5       // Full debug output
};

// Global debug settings
extern GLTFDebugLevel gltf_debug_level;
extern bool gltf_debug_performance;
extern bool gltf_debug_validation;
extern bool gltf_debug_memory;

//===========================================================================
//
// Debug Macros
//
//===========================================================================

#define GLTF_LOG(level, ...) \
    do { \
        if (gltf_debug_level >= (level)) { \
            Printf("[glTF %s] ", GLTFDebugLevelToString(level)); \
            Printf(__VA_ARGS__); \
            Printf("\n"); \
        } \
    } while(0)

#define GLTF_ERROR(...) GLTF_LOG(GLTFDebugLevel::Error, __VA_ARGS__)
#define GLTF_WARNING(...) GLTF_LOG(GLTFDebugLevel::Warning, __VA_ARGS__)
#define GLTF_INFO(...) GLTF_LOG(GLTFDebugLevel::Info, __VA_ARGS__)
#define GLTF_VERBOSE(...) GLTF_LOG(GLTFDebugLevel::Verbose, __VA_ARGS__)
#define GLTF_DEBUG(...) GLTF_LOG(GLTFDebugLevel::Debug, __VA_ARGS__)

// Performance tracking macros
#define GLTF_PERF_BEGIN(name) \
    auto gltf_perf_start_##name = std::chrono::high_resolution_clock::now()

#define GLTF_PERF_END(name) \
    do { \
        if (gltf_debug_performance) { \
            auto gltf_perf_end_##name = std::chrono::high_resolution_clock::now(); \
            auto duration = std::chrono::duration<double, std::milli>(gltf_perf_end_##name - gltf_perf_start_##name).count(); \
            Printf("[glTF PERF] %s: %.3f ms\n", #name, duration); \
        } \
    } while(0)

// Memory tracking macros
#define GLTF_MEM_ALLOC(size, desc) \
    do { \
        if (gltf_debug_memory) { \
            Printf("[glTF MEM] Allocated %zu bytes for %s\n", (size_t)(size), (desc)); \
        } \
    } while(0)

#define GLTF_MEM_FREE(size, desc) \
    do { \
        if (gltf_debug_memory) { \
            Printf("[glTF MEM] Freed %zu bytes from %s\n", (size_t)(size), (desc)); \
        } \
    } while(0)

// Validation macros
#define GLTF_VALIDATE(condition, message) \
    do { \
        if (gltf_debug_validation && !(condition)) { \
            Printf("[glTF VALIDATION] Failed: %s (at %s:%d)\n", (message), __FILE__, __LINE__); \
        } \
    } while(0)

#define GLTF_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            Printf("[glTF ASSERT] %s (at %s:%d)\n", (message), __FILE__, __LINE__); \
            assert(false); \
        } \
    } while(0)

//===========================================================================
//
// Debug Helper Functions
//
//===========================================================================

const char* GLTFDebugLevelToString(GLTFDebugLevel level);
void GLTFSetDebugLevel(GLTFDebugLevel level);
void GLTFSetDebugFlags(bool performance, bool validation, bool memory);
void GLTFDumpModelInfo(const class FGLTFModel* model);
void GLTFDumpSceneHierarchy(const struct GLTFScene& scene);
void GLTFDumpAnimationInfo(const struct GLTFAnimation& animation);
void GLTFDumpMaterialInfo(const struct PBRMaterialProperties& material);

//===========================================================================
//
// Performance Profiler
//
//===========================================================================

class GLTFProfiler
{
private:
    struct ProfileEntry {
        const char* name;
        std::chrono::high_resolution_clock::time_point startTime;
        double totalTime = 0.0;
        int callCount = 0;
        bool active = false;
    };

    TArray<ProfileEntry> entries;
    int currentEntryIndex = -1;

public:
    void Begin(const char* name);
    void End();
    void Reset();
    void PrintReport() const;
    double GetTotalTime(const char* name) const;
    int GetCallCount(const char* name) const;

private:
    int FindEntry(const char* name);
};

extern GLTFProfiler gltf_profiler;

//===========================================================================
//
// Scoped Profiler Helper
//
//===========================================================================

class GLTFScopedProfiler
{
private:
    const char* name;

public:
    GLTFScopedProfiler(const char* profilerName) : name(profilerName) {
        gltf_profiler.Begin(name);
    }

    ~GLTFScopedProfiler() {
        gltf_profiler.End();
    }
};

#define GLTF_PROFILE(name) GLTFScopedProfiler gltf_scoped_prof(name)

//===========================================================================
//
// Memory Tracker
//
//===========================================================================

class GLTFMemoryTracker
{
private:
    struct AllocationInfo {
        size_t size;
        const char* description;
        std::chrono::high_resolution_clock::time_point timestamp;
    };

    TMap<void*, AllocationInfo> allocations;
    size_t totalAllocated = 0;
    size_t peakUsage = 0;
    int allocationCount = 0;

public:
    void RecordAllocation(void* ptr, size_t size, const char* desc);
    void RecordDeallocation(void* ptr);
    void Reset();
    void PrintReport() const;

    size_t GetTotalAllocated() const { return totalAllocated; }
    size_t GetPeakUsage() const { return peakUsage; }
    int GetAllocationCount() const { return allocationCount; }
};

extern GLTFMemoryTracker gltf_memory_tracker;

//===========================================================================
//
// Validation Helper
//
//===========================================================================

class GLTFValidator
{
public:
    static bool ValidateVector3(const FVector3& vec, const char* name);
    static bool ValidateVector4(const FVector4& vec, const char* name);
    static bool ValidateQuaternion(const FQuaternion& quat, const char* name);
    static bool ValidateMatrix(const VSMatrix& matrix, const char* name);
    static bool ValidateUVCoordinates(float u, float v, const char* name);
    static bool ValidateColorValue(float value, const char* name);
    static bool ValidateArrayBounds(int index, int size, const char* name);
    static bool ValidateFileOffset(size_t offset, size_t fileSize, size_t dataSize, const char* name);
};

#else // !NEODOOM_GLTF_SUPPORT

// Stub macros when glTF support is disabled
#define GLTF_LOG(level, ...)
#define GLTF_ERROR(...)
#define GLTF_WARNING(...)
#define GLTF_INFO(...)
#define GLTF_VERBOSE(...)
#define GLTF_DEBUG(...)
#define GLTF_PERF_BEGIN(name)
#define GLTF_PERF_END(name)
#define GLTF_MEM_ALLOC(size, desc)
#define GLTF_MEM_FREE(size, desc)
#define GLTF_VALIDATE(condition, message)
#define GLTF_ASSERT(condition, message)
#define GLTF_PROFILE(name)

#endif // NEODOOM_GLTF_SUPPORT