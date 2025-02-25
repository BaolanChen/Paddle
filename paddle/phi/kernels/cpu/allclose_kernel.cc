// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/phi/kernels/allclose_kernel.h"

#include <cmath>
#include <type_traits>

#include "glog/logging.h"
#include "paddle/phi/core/enforce.h"
#include "paddle/phi/core/kernel_registry.h"

namespace phi {

template <typename T, typename Context>
void AllCloseKernel(const Context& dev_ctx,
                    const DenseTensor& x,
                    const DenseTensor& y,
                    const Scalar& rtol,
                    const Scalar& atol,
                    bool equal_nan,
                    DenseTensor* out) {
  double rtol_v = NAN, atol_v = NAN;
  if (rtol.dtype() == DataType::FLOAT64) {
    rtol_v = rtol.to<double>();
  } else if (rtol.dtype() == DataType::FLOAT32) {
    rtol_v = rtol.to<float>();
  } else {
    PADDLE_THROW(common::errors::InvalidArgument(
        "Input (Rtol) type must be double or float, but get %s.",
        rtol.dtype()));
  }
  if (atol.dtype() == DataType::FLOAT64) {
    atol_v = atol.to<double>();
  } else if (atol.dtype() == DataType::FLOAT32) {
    atol_v = atol.to<float>();
  } else {
    PADDLE_THROW(common::errors::InvalidArgument(
        "Input (Atol) type must be double or float, but get %s.",
        atol.dtype()));
  }
  VLOG(3) << "rtol and atol is : " << rtol_v << " " << atol_v;
  auto* in_a = x.data<T>();
  auto* in_b = y.data<T>();
  auto* out_data = dev_ctx.template Alloc<bool>(out);
  *out_data = true;

  auto num = x.numel();
  if (std::is_same<T, int32_t>::value || std::is_same<T, int64_t>::value ||
      std::is_same<T, bool>::value) {
    for (int64_t i = 0; i < num; ++i) {
      const double a = static_cast<double>(in_a[i]),
                   b = static_cast<double>(in_b[i]);
      double left = (a > b ? a - b : b - a);
      double right = atol_v + (b > 0 ? rtol_v * b : (-rtol_v) * b);
      double diff = (left > right ? left - right : right - left);
      bool val = a == b || left <= right || diff <= 1e-15;
      *out_data &= val;
    }
  } else {
    for (int64_t i = 0; i < num; ++i) {
      const T a = in_a[i], b = in_b[i];
      bool val = false;
      if (std::isnan(static_cast<double>(a)) ||
          std::isnan(static_cast<double>(b))) {
        val = equal_nan && std::isnan(static_cast<double>(a)) ==
                               std::isnan(static_cast<double>(b));
      } else {
        T left = (a > b ? a - b : b - a);
        T right = atol_v + (b > 0 ? rtol_v * b : (-rtol_v) * b);
        T diff = (left > right ? left - right : right - left);
        val = a == b || left <= right || diff <= 1e-15;
      }
      *out_data &= val;
    }
  }
}

}  // namespace phi

PD_REGISTER_KERNEL(allclose,
                   CPU,
                   ALL_LAYOUT,
                   phi::AllCloseKernel,
                   float,
                   double,
                   bool,
                   int,
                   int64_t) {
  kernel->OutputAt(0).SetDataType(phi::DataType::BOOL);
}
