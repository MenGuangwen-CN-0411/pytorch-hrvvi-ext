#pragma once
#include <torch/extension.h>

at::Tensor PSROIAlign_forward_cpu(const at::Tensor &input,
                                  const at::Tensor &rois, const float scale_h,
                                  const float scale_w, const int out_channels,
                                  const int pooled_height,
                                  const int pooled_width,
                                  const int sampling_ratio);

at::Tensor PSROIAlign_backward_cpu(const at::Tensor &grad,
                                   const at::Tensor &rois, const float scale_h,
                                   const float scale_w, const int out_channels,
                                   const int pooled_height,
                                   const int pooled_width, const int batch_size,
                                   const int channels, const int height,
                                   const int width, const int sampling_ratio);

at::Tensor ROIAlign_forward_cpu(const at::Tensor &input, const at::Tensor &rois,
                                const float scale_h, const float scale_w,
                                const int pooled_height, const int pooled_width,
                                const int sampling_ratio);

at::Tensor ROIAlign_backward_cpu(const at::Tensor &grad, const at::Tensor &rois,
                                 const float scale_h, const float scale_w,
                                 const int pooled_height,
                                 const int pooled_width, const int batch_size,
                                 const int channels, const int height,
                                 const int width, const int sampling_ratio);

at::Tensor iou_mn_forward_cpu(const at::Tensor &boxes1,
                              const at::Tensor &boxes2);

std::tuple<at::Tensor, at::Tensor> iou_mn_backward_cpu(const at::Tensor &dious,
                                                       const at::Tensor &boxes1,
                                                       const at::Tensor &boxes2,
                                                       const at::Tensor &ious);

at::Tensor nms_cpu(const at::Tensor &dets, const at::Tensor &scores,
                   const float threshold);

at::Tensor soft_nms_cpu(const at::Tensor &dets, at::Tensor &scores,
                        const float iou_threshold, const int topk,
                        const float score_threshold);

at::Tensor softer_nms_cpu(at::Tensor &dets, at::Tensor &scores,
                          const at::Tensor &vars, const float iou_threshold,
                          const int topk, const float sigma,
                          const float min_score);