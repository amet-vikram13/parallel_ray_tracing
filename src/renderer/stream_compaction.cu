#include "renderer/stream_compaction.h"
#include "utils/cuda_utils.h"
#include <thrust/device_ptr.h>
#include <thrust/copy.h>
#include <thrust/count.h>

struct IsActive {
    __host__ __device__ bool operator()(const RayState& r) const {
        return r.active;
    }
};

int compact_rays(RayState* d_rays, RayState* d_compacted, int count) {
    thrust::device_ptr<RayState> src(d_rays);
    thrust::device_ptr<RayState> dst(d_compacted);

    auto end = thrust::copy_if(src, src + count, dst, IsActive());
    return static_cast<int>(end - dst);
}
