__kernel void reduce_sum(__global const float* input,
                         __global float* partial_sums,
                         int n,
                         __local float* scratch) {
    int gid   = get_global_id(0);
    int lid   = get_local_id(0);
    int lsize = get_local_size(0);

    scratch[lid] = (gid < n) ? input[gid] : 0.0f;
    barrier(CLK_LOCAL_MEM_FENCE);

    for (int stride = lsize >> 1; stride > 0; stride >>= 1) {
        if (lid < stride)
            scratch[lid] += scratch[lid + stride];
        barrier(CLK_LOCAL_MEM_FENCE);
    }

    if (lid == 0)
        partial_sums[get_group_id(0)] = scratch[0];
}
