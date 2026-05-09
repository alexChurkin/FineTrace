__kernel void mandelbrot(__global int* output,
                         int width,
                         int height,
                         int max_iter,
                         float x_min,
                         float x_max,
                         float y_min,
                         float y_max) {
    int px = get_global_id(0);
    int py = get_global_id(1);
    if (px >= width || py >= height) return;

    float cr = x_min + px * (x_max - x_min) / (float)width;
    float ci = y_min + py * (y_max - y_min) / (float)height;
    float zr = 0.0f, zi = 0.0f;
    int iter = 0;
    while (iter < max_iter && zr * zr + zi * zi < 4.0f) {
        float tmp = zr * zr - zi * zi + cr;
        zi = 2.0f * zr * zi + ci;
        zr = tmp;
        ++iter;
    }
    output[py * width + px] = iter;
}
