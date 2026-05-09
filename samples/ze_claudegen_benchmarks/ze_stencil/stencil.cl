__kernel void jacobi(__global const float* in,
                     __global float* out,
                     int width,
                     int height) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x == 0 || x >= width - 1 || y == 0 || y >= height - 1) {
        out[y * width + x] = in[y * width + x];
        return;
    }
    out[y * width + x] = 0.25f * (in[(y - 1) * width + x] +
                                   in[(y + 1) * width + x] +
                                   in[y * width + (x - 1)] +
                                   in[y * width + (x + 1)]);
}
