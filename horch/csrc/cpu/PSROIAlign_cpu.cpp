#include "cpu/vision.h"
#include <ATen/TensorUtils.h>

// implementation taken from Caffe2
template <typename T> struct PreCalc {
    int pos1;
    int pos2;
    int pos3;
    int pos4;
    T w1;
    T w2;
    T w3;
    T w4;
};

template <typename T>
void pre_calc_for_bilinear_interpolate(
    const int height, const int width, const int pooled_height,
    const int pooled_width, const int iy_upper, const int ix_upper,
    T roi_start_h, T roi_start_w, T bin_size_h, T bin_size_w,
    int roi_bin_grid_h, int roi_bin_grid_w, std::vector<PreCalc<T>> &pre_calc) {
    int pre_calc_index = 0;
    for (int ph = 0; ph < pooled_height; ph++) {
        for (int pw = 0; pw < pooled_width; pw++) {
            for (int iy = 0; iy < iy_upper; iy++) {
                const T yy =
                    roi_start_h + ph * bin_size_h +
                    static_cast<T>(iy + .5f) * bin_size_h /
                        static_cast<T>(roi_bin_grid_h); // e.g., 0.5, 1.5
                for (int ix = 0; ix < ix_upper; ix++) {
                    const T xx = roi_start_w + pw * bin_size_w +
                                 static_cast<T>(ix + .5f) * bin_size_w /
                                     static_cast<T>(roi_bin_grid_w);

                    T x = xx;
                    T y = yy;
                    // deal with: inverse elements are out of feature map
                    // boundary
                    if (y < -1.0 || y > height || x < -1.0 || x > width) {
                        // empty
                        PreCalc<T> pc;
                        pc.pos1 = 0;
                        pc.pos2 = 0;
                        pc.pos3 = 0;
                        pc.pos4 = 0;
                        pc.w1 = 0;
                        pc.w2 = 0;
                        pc.w3 = 0;
                        pc.w4 = 0;
                        pre_calc[pre_calc_index] = pc;
                        pre_calc_index += 1;
                        continue;
                    }

                    if (y <= 0) {
                        y = 0;
                    }
                    if (x <= 0) {
                        x = 0;
                    }

                    int y_low = (int)y;
                    int x_low = (int)x;
                    int y_high;
                    int x_high;

                    if (y_low >= height - 1) {
                        y_high = y_low = height - 1;
                        y = (T)y_low;
                    } else {
                        y_high = y_low + 1;
                    }

                    if (x_low >= width - 1) {
                        x_high = x_low = width - 1;
                        x = (T)x_low;
                    } else {
                        x_high = x_low + 1;
                    }

                    T ly = y - y_low;
                    T lx = x - x_low;
                    T hy = 1. - ly, hx = 1. - lx;
                    T w1 = hy * hx, w2 = hy * lx, w3 = ly * hx, w4 = ly * lx;

                    // save weights and indeces
                    PreCalc<T> pc;
                    pc.pos1 = y_low * width + x_low;
                    pc.pos2 = y_low * width + x_high;
                    pc.pos3 = y_high * width + x_low;
                    pc.pos4 = y_high * width + x_high;
                    pc.w1 = w1;
                    pc.w2 = w2;
                    pc.w3 = w3;
                    pc.w4 = w4;
                    pre_calc[pre_calc_index] = pc;

                    pre_calc_index += 1;
                }
            }
        }
    }
}

template <typename T>
void PSROIAlignForward(const int nthreads, const T *input,
                       const T &scale_h, const T& scale_w, const int channels,
                       const int height, const int width,
                       const int out_channels, const int pooled_height,
                       const int pooled_width, const int sampling_ratio,
                       const T *rois, T *output) {
    int n_rois = nthreads / out_channels / pooled_width / pooled_height;
    // (n, c, ph, pw) is an element in the pooled output
    // can be parallelized using omp
    // #pragma omp parallel for num_threads(32)
    for (int n = 0; n < n_rois; n++) {
        int index_n = n * out_channels * pooled_width * pooled_height;

        const T *offset_rois = rois + n * 5;
        int roi_batch_ind = offset_rois[0];

        // Do not using rounding; this implementation detail is critical
        T roi_start_w = offset_rois[1] * scale_w;
        T roi_start_h = offset_rois[2] * scale_h;
        T roi_end_w = offset_rois[3] * scale_w;
        T roi_end_h = offset_rois[4] * scale_h;
        // T roi_start_w = round(offset_rois[0] * spatial_scale);
        // T roi_start_h = round(offset_rois[1] * spatial_scale);
        // T roi_end_w = round(offset_rois[2] * spatial_scale);
        // T roi_end_h = round(offset_rois[3] * spatial_scale);

        // Force malformed ROIs to be 1x1
        T roi_width = std::max(roi_end_w - roi_start_w, (T)1.);
        T roi_height = std::max(roi_end_h - roi_start_h, (T)1.);
        T bin_size_h =
            static_cast<T>(roi_height) / static_cast<T>(pooled_height);
        T bin_size_w = static_cast<T>(roi_width) / static_cast<T>(pooled_width);

        // We use roi_bin_grid to sample the grid and mimic integral
        int roi_bin_grid_h =
            (sampling_ratio > 0)
                ? sampling_ratio
                : ceil(roi_height / pooled_height); // e.g., = 2
        int roi_bin_grid_w = (sampling_ratio > 0)
                                 ? sampling_ratio
                                 : ceil(roi_width / pooled_width);

        // We do average (integral) pooling inside a bin
        const T count = roi_bin_grid_h * roi_bin_grid_w; // e.g. = 4

        // we want to precalculate indeces and weights shared by all chanels,
        // this is the key point of optimization
        std::vector<PreCalc<T>> pre_calc(roi_bin_grid_h * roi_bin_grid_w *
                                         pooled_height * pooled_width);
        pre_calc_for_bilinear_interpolate(
            height, width, pooled_height, pooled_width, roi_bin_grid_h,
            roi_bin_grid_w, roi_start_h, roi_start_w, bin_size_h, bin_size_w,
            roi_bin_grid_h, roi_bin_grid_w, pre_calc);

        for (int c = 0; c < out_channels; c++) {
            int c_offset = c * pooled_height * pooled_width;
            int pre_calc_index = 0;

            for (int ph = 0; ph < pooled_height; ph++) {
                for (int pw = 0; pw < pooled_width; pw++) {
                    int offset = c_offset + ph * pooled_width + pw;
                    int index = index_n + offset;
                    const T *offset_input =
                        input +
                        (roi_batch_ind * channels + offset) * height * width;

                    T output_val = 0.;
                    for (int iy = 0; iy < roi_bin_grid_h; iy++) {
                        for (int ix = 0; ix < roi_bin_grid_w; ix++) {
                            PreCalc<T> pc = pre_calc[pre_calc_index];
                            output_val += pc.w1 * offset_input[pc.pos1] +
                                          pc.w2 * offset_input[pc.pos2] +
                                          pc.w3 * offset_input[pc.pos3] +
                                          pc.w4 * offset_input[pc.pos4];

                            pre_calc_index += 1;
                        }
                    }
                    output_val /= count;

                    output[index] = output_val;
                } // for pw
            }     // for ph
        }         // for c
    }             // for n
}

template <typename T>
void bilinear_interpolate_gradient(const int height, const int width, T y, T x,
                                   T &w1, T &w2, T &w3, T &w4, int &x_low,
                                   int &x_high, int &y_low, int &y_high,
                                   const int index /* index for debug only*/) {

    // deal with cases that inverse elements are out of feature map boundary
    if (y < -1.0 || y > height || x < -1.0 || x > width) {
        // empty
        w1 = w2 = w3 = w4 = 0.;
        x_low = x_high = y_low = y_high = -1;
        return;
    }

    if (y <= 0)
        y = 0;
    if (x <= 0)
        x = 0;

    y_low = (int)y;
    x_low = (int)x;

    if (y_low >= height - 1) {
        y_high = y_low = height - 1;
        y = (T)y_low;
    } else {
        y_high = y_low + 1;
    }

    if (x_low >= width - 1) {
        x_high = x_low = width - 1;
        x = (T)x_low;
    } else {
        x_high = x_low + 1;
    }

    T ly = y - y_low;
    T lx = x - x_low;
    T hy = 1. - ly, hx = 1. - lx;

    // reference in forward
    // T v1 = input[y_low * width + x_low];
    // T v2 = input[y_low * width + x_high];
    // T v3 = input[y_high * width + x_low];
    // T v4 = input[y_high * width + x_high];
    // T val = (w1 * v1 + w2 * v2 + w3 * v3 + w4 * v4);

    w1 = hy * hx, w2 = hy * lx, w3 = ly * hx, w4 = ly * lx;

    return;
}

template <class T> inline void add(T *address, const T &val) {
    *address += val;
}

template <typename T>
void PSROIAlignBackward(const int nthreads, const T *grad_output,
                        const T &scale_h, const T& scale_w, const int channels,
                        const int height, const int width,
                        const int out_channels, const int pooled_height,
                        const int pooled_width, const int sampling_ratio,
                        T *grad_input, const T *rois, const int n_stride,
                        const int c_stride, const int h_stride,
                        const int w_stride) {
    for (int index = 0; index < nthreads; index++) {
        // (n, c, ph, pw) is an element in the pooled output
        int pw = index % pooled_width;
        int ph = (index / pooled_width) % pooled_height;
        int c = (index / pooled_width / pooled_height) % out_channels;
        int n = index / pooled_width / pooled_height / out_channels;
        int ic = c * pooled_height * pooled_width + (ph * pooled_width) + pw;

        const T *offset_rois = rois + n * 5;
        int roi_batch_ind = offset_rois[0];

        // Do not using rounding; this implementation detail is critical
        T roi_start_w = offset_rois[1] * scale_w;
        T roi_start_h = offset_rois[2] * scale_h;
        T roi_end_w = offset_rois[3] * scale_w;
        T roi_end_h = offset_rois[4] * scale_h;

        // Force malformed ROIs to be 1x1
        T roi_width = std::max(roi_end_w - roi_start_w, (T)1.);
        T roi_height = std::max(roi_end_h - roi_start_h, (T)1.);
        T bin_size_h =
            static_cast<T>(roi_height) / static_cast<T>(pooled_height);
        T bin_size_w = static_cast<T>(roi_width) / static_cast<T>(pooled_width);

        T *offset_grad_input =
            grad_input + ((roi_batch_ind * channels + ic) * height * width);

        int output_offset =
            n * n_stride + c * c_stride + ph * h_stride + pw * w_stride;
        const T grad_output_this_bin = *(grad_output + output_offset);

        // We use roi_bin_grid to sample the grid and mimic integral
        int roi_bin_grid_h =
            (sampling_ratio > 0)
                ? sampling_ratio
                : ceil(roi_height / pooled_height); // e.g., = 2
        int roi_bin_grid_w = (sampling_ratio > 0)
                                 ? sampling_ratio
                                 : ceil(roi_width / pooled_width);

        // We do average (integral) pooling inside a bin
        const T count = roi_bin_grid_h * roi_bin_grid_w; // e.g. = 4

        for (int iy = 0; iy < roi_bin_grid_h; iy++) {
            const T y = roi_start_h + ph * bin_size_h +
                        static_cast<T>(iy + .5f) * bin_size_h /
                            static_cast<T>(roi_bin_grid_h); // e.g., 0.5, 1.5
            for (int ix = 0; ix < roi_bin_grid_w; ix++) {
                const T x = roi_start_w + pw * bin_size_w +
                            static_cast<T>(ix + .5f) * bin_size_w /
                                static_cast<T>(roi_bin_grid_w);

                T w1, w2, w3, w4;
                int x_low, x_high, y_low, y_high;

                bilinear_interpolate_gradient(height, width, y, x, w1, w2, w3,
                                              w4, x_low, x_high, y_low, y_high,
                                              index);

                T g1 = grad_output_this_bin * w1 / count;
                T g2 = grad_output_this_bin * w2 / count;
                T g3 = grad_output_this_bin * w3 / count;
                T g4 = grad_output_this_bin * w4 / count;

                if (x_low >= 0 && x_high >= 0 && y_low >= 0 && y_high >= 0) {
                    // atomic add is not needed for now since it is single
                    // threaded
                    add(offset_grad_input + y_low * width + x_low,
                        static_cast<T>(g1));
                    add(offset_grad_input + y_low * width + x_high,
                        static_cast<T>(g2));
                    add(offset_grad_input + y_high * width + x_low,
                        static_cast<T>(g3));
                    add(offset_grad_input + y_high * width + x_high,
                        static_cast<T>(g4));
                } // if
            }     // ix
        }         // iy
    }             // for
} // ROIAlignBackward

at::Tensor
PSROIAlign_forward_cpu(const at::Tensor &input, const at::Tensor &rois,
                       const float scale_h, const float scale_w, const int out_channels,
                       const int pooled_height, const int pooled_width,
                       const int sampling_ratio) {
    AT_ASSERTM(input.device().is_cpu(), "input must be a CPU tensor");
    AT_ASSERTM(rois.device().is_cpu(), "rois must be a CPU tensor");

    at::TensorArg input_t{input, "input", 1}, rois_t{rois, "rois", 2};

    at::CheckedFrom c = "PSROIAlign_forward_cpu";
    at::checkAllSameType(c, {input_t, rois_t});

    auto num_rois = rois.size(0);
    auto channels = input.size(1);
    auto height = input.size(2);
    auto width = input.size(3);
    AT_ASSERTM(channels == (out_channels * pooled_height * pooled_width),
               "the number of input channels must be equal to out_channels * "
               "pooled_height * pooled_width");

    at::Tensor output = at::zeros(
        {num_rois, out_channels, pooled_height, pooled_width}, input.options());

    auto output_size = num_rois * out_channels * pooled_height * pooled_width;

    if (output.numel() == 0)
        return output;

    AT_DISPATCH_FLOATING_TYPES_AND_HALF(
        input.type(), "PSROIAlign_forward", [&] {
            PSROIAlignForward<scalar_t>(
                output_size, input.contiguous().data<scalar_t>(), scale_h, scale_w,
                channels, height, width, out_channels, pooled_height,
                pooled_width, sampling_ratio,
                rois.contiguous().data<scalar_t>(), output.data<scalar_t>());
        });
    return output;
}

at::Tensor PSROIAlign_backward_cpu(
    const at::Tensor &grad, const at::Tensor &rois, const float scale_h, const float scale_w,
    const int out_channels, const int pooled_height, const int pooled_width,
    const int batch_size, const int channels, const int height, const int width,
    const int sampling_ratio) {
    AT_ASSERTM(grad.device().is_cpu(), "grad must be a CPU tensor");
    AT_ASSERTM(rois.device().is_cpu(), "rois must be a CPU tensor");

    at::TensorArg grad_t{grad, "grad", 1}, rois_t{rois, "rois", 2};

    at::CheckedFrom c = "PSROIAlign_backward_cpu";
    at::checkAllSameType(c, {grad_t, rois_t});

    at::Tensor grad_input =
        at::zeros({batch_size, channels, height, width}, grad.options());

    // handle possibly empty gradients
    if (grad.numel() == 0) {
        return grad_input;
    }

    // get stride values to ensure indexing into gradients is correct.
    int n_stride = grad.stride(0);
    int c_stride = grad.stride(1);
    int h_stride = grad.stride(2);
    int w_stride = grad.stride(3);

    AT_DISPATCH_FLOATING_TYPES_AND_HALF(
        grad.type(), "PSROIAlign_backward", [&] {
            PSROIAlignBackward<scalar_t>(
                grad.numel(), grad.contiguous().data<scalar_t>(), scale_h, scale_w,
                channels, height, width, out_channels, pooled_height,
                pooled_width, sampling_ratio, grad_input.data<scalar_t>(),
                rois.contiguous().data<scalar_t>(), n_stride, c_stride,
                h_stride, w_stride);
        });
    return grad_input;
}