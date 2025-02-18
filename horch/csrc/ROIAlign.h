#pragma once

#include "cpu/vision.h"

#ifdef WITH_CUDA
#include "cuda/vision.h"
#endif

// Interface for Python
at::Tensor ROIAlign_forward(const at::Tensor& input,    // Input feature map.
                            const at::Tensor& rois,     // List of ROIs to pool over.
                            const float scale_h,  // The scale of the image features. ROIs will be scaled to this.
                            const float scale_w,
                            const int pooled_height,    // The height of the pooled feature map.
                            const int pooled_width,     // The width of the pooled feature
                            const int sampling_ratio)   // The number of points to sample in each bin along each axis.
{
  if (input.type().is_cuda()) {
#ifdef WITH_CUDA
    return ROIAlign_forward_cuda(input, rois, scale_h, scale_w, pooled_height, pooled_width, sampling_ratio);
#else
    AT_ERROR("Not compiled with GPU support");
#endif
  }
  return ROIAlign_forward_cpu(input, rois, scale_h, scale_w, pooled_height, pooled_width, sampling_ratio);
}

at::Tensor ROIAlign_backward(const at::Tensor& grad,
                             const at::Tensor& rois,
                             const float scale_h,
                             const float scale_w,
                             const int pooled_height,
                             const int pooled_width,
                             const int batch_size,
                             const int channels,
                             const int height,
                             const int width,
                             const int sampling_ratio) {
  if (grad.type().is_cuda()) {
#ifdef WITH_CUDA
    return ROIAlign_backward_cuda(grad, rois, scale_h, scale_w, pooled_height, pooled_width, batch_size, channels, height, width, sampling_ratio);
#else
    AT_ERROR("Not compiled with GPU support");
#endif
  }
  return ROIAlign_backward_cpu(grad, rois, scale_h, scale_w, pooled_height, pooled_width, batch_size, channels, height, width, sampling_ratio);
}
