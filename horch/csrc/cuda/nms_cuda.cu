#include <ATen/ATen.h>
#include <ATen/cuda/CUDAContext.h>
// #include <c10/cuda/CUDAGuard.h>
#include <ATen/cuda/CUDAApplyUtils.cuh>

#include "cuda_helpers.h"

#include <iostream>
#include <vector>

int const threadsPerBlock = sizeof(unsigned long long) * 8;

template <typename T>
__device__ inline float devIoU(T const* const a, T const* const b) {
  T left = max(a[0], b[0]), right = min(a[2], b[2]);
  T top = max(a[1], b[1]), bottom = min(a[3], b[3]);
  T width = max(right - left, (T)0), height = max(bottom - top, (T)0);
  T interS = width * height;
  T Sa = (a[2] - a[0]) * (a[3] - a[1]);
  T Sb = (b[2] - b[0]) * (b[3] - b[1]);
  return interS / (Sa + Sb - interS);
}

template <typename T>
__global__ void nms_kernel(
    const int n_boxes,
    const float nms_overlap_thresh,
    const T* dev_boxes,
    unsigned long long* dev_mask) {
  const int row_start = blockIdx.y;
  const int col_start = blockIdx.x;

  // if (row_start > col_start) return;

  const int row_size =
      min(n_boxes - row_start * threadsPerBlock, threadsPerBlock);
  const int col_size =
      min(n_boxes - col_start * threadsPerBlock, threadsPerBlock);

  __shared__ T block_boxes[threadsPerBlock * 5];
  if (threadIdx.x < col_size) {
    block_boxes[threadIdx.x * 5 + 0] =
        dev_boxes[(threadsPerBlock * col_start + threadIdx.x) * 5 + 0];
    block_boxes[threadIdx.x * 5 + 1] =
        dev_boxes[(threadsPerBlock * col_start + threadIdx.x) * 5 + 1];
    block_boxes[threadIdx.x * 5 + 2] =
        dev_boxes[(threadsPerBlock * col_start + threadIdx.x) * 5 + 2];
    block_boxes[threadIdx.x * 5 + 3] =
        dev_boxes[(threadsPerBlock * col_start + threadIdx.x) * 5 + 3];
    block_boxes[threadIdx.x * 5 + 4] =
        dev_boxes[(threadsPerBlock * col_start + threadIdx.x) * 5 + 4];
  }
  __syncthreads();

  if (threadIdx.x < row_size) {
    const int cur_box_idx = threadsPerBlock * row_start + threadIdx.x;
    const T* cur_box = dev_boxes + cur_box_idx * 5;
    int i = 0;
    unsigned long long t = 0;
    int start = 0;
    if (row_start == col_start) {
      start = threadIdx.x + 1;
    }
    for (i = start; i < col_size; i++) {
      if (devIoU<T>(cur_box, block_boxes + i * 5) > nms_overlap_thresh) {
        t |= 1ULL << i;
      }
    }
    const int col_blocks = at::cuda::ATenCeilDiv(n_boxes, threadsPerBlock);
    dev_mask[cur_box_idx * col_blocks + col_start] = t;
  }
}

// boxes is a N x 5 tensor
at::Tensor nms_cuda(const at::Tensor boxes, float nms_overlap_thresh) {
  using scalar_t = float;
  AT_ASSERTM(boxes.type().is_cuda(), "boxes must be a CUDA tensor");
  // at::cuda::CUDAGuard device_guard(boxes.device());

  auto scores = boxes.select(1, 4);
  auto order_t = std::get<1>(scores.sort(0, /* descending=*/true));
  auto boxes_sorted = boxes.index_select(0, order_t);

  int boxes_num = boxes.size(0);

  const int col_blocks = at::cuda::ATenCeilDiv(boxes_num, threadsPerBlock);

  at::Tensor mask =
      at::empty({boxes_num * col_blocks}, boxes.options().dtype(at::kLong));

  dim3 blocks(col_blocks, col_blocks);
  dim3 threads(threadsPerBlock);
  cudaStream_t stream = at::cuda::getCurrentCUDAStream();

  AT_DISPATCH_FLOATING_TYPES_AND_HALF(
      boxes_sorted.type(), "nms_kernel_cuda", [&] {
        nms_kernel<scalar_t><<<blocks, threads, 0, stream>>>(
            boxes_num,
            nms_overlap_thresh,
            boxes_sorted.data<scalar_t>(),
            (unsigned long long*)mask.data<int64_t>());
      });

  at::Tensor mask_cpu = mask.to(at::kCPU);
  unsigned long long* mask_host = (unsigned long long*)mask_cpu.data<int64_t>();

  std::vector<unsigned long long> remv(col_blocks);
  memset(&remv[0], 0, sizeof(unsigned long long) * col_blocks);

  at::Tensor keep =
      at::empty({boxes_num}, boxes.options().dtype(at::kLong).device(at::kCPU));
  int64_t* keep_out = keep.data<int64_t>();

  int num_to_keep = 0;
  for (int i = 0; i < boxes_num; i++) {
    int nblock = i / threadsPerBlock;
    int inblock = i % threadsPerBlock;

    if (!(remv[nblock] & (1ULL << inblock))) {
      keep_out[num_to_keep++] = i;
      unsigned long long* p = mask_host + i * col_blocks;
      for (int j = nblock; j < col_blocks; j++) {
        remv[j] |= p[j];
      }
    }
  }

  AT_CUDA_CHECK(cudaGetLastError());
  return
      order_t
          .index({keep.narrow(/*dim=*/0, /*start=*/0, /*length=*/num_to_keep)
                      .to(order_t.device(), keep.scalar_type())});
}