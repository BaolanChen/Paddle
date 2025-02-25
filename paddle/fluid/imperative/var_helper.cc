// Copyright (c) 2021 PaddlePaddle Authors. All Rights Reserved.
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

#include "paddle/fluid/imperative/var_helper.h"

#include "paddle/fluid/eager/eager_tensor.h"
#include "paddle/fluid/framework/convert_utils.h"
#include "paddle/fluid/framework/dense_tensor_array.h"
#include "paddle/fluid/framework/feed_fetch_type.h"
#include "paddle/fluid/framework/lod_tensor.h"
#include "paddle/fluid/framework/scope.h"
#include "paddle/fluid/framework/tensor.h"
#include "paddle/fluid/framework/var_type_traits.h"
#include "paddle/fluid/imperative/layer.h"
#include "paddle/phi/common/place.h"
#include "paddle/phi/core/framework/reader.h"
#include "paddle/phi/core/selected_rows.h"
namespace paddle::imperative {

/* GetVariableWrapper */
template <>
const std::shared_ptr<VariableWrapper> &GetVariableWrapper<VarBase>(
    const std::shared_ptr<VarBase> &var) {
  return var->SharedVar();
}
template <>
const std::shared_ptr<VariableWrapper> &GetVariableWrapper<VariableWrapper>(
    const std::shared_ptr<VariableWrapper> &var) {
  return var;
}

void InitializeVariable(paddle::framework::Variable *var,
                        paddle::framework::proto::VarType::Type var_type) {
  if (var_type == paddle::framework::proto::VarType::DENSE_TENSOR) {
    var->GetMutable<phi::DenseTensor>();
  } else if (var_type == paddle::framework::proto::VarType::SELECTED_ROWS) {
    var->GetMutable<phi::SelectedRows>();
  } else if (var_type == paddle::framework::proto::VarType::FEED_MINIBATCH) {
    var->GetMutable<paddle::framework::FeedList>();
  } else if (var_type ==
             paddle::framework::proto::VarType::FETCH_LIST) {  // NOLINT
    var->GetMutable<paddle::framework::FetchList>();
  } else if (var_type == paddle::framework::proto::VarType::STEP_SCOPES) {
    var->GetMutable<std::vector<paddle::framework::Scope *>>();
  } else if (var_type ==
             paddle::framework::proto::VarType::DENSE_TENSOR_ARRAY) {
    var->GetMutable<phi::TensorArray>();
  } else if (var_type == paddle::framework::proto::VarType::STRINGS) {
    var->GetMutable<phi::Strings>();
  } else if (var_type == paddle::framework::proto::VarType::VOCAB) {
    var->GetMutable<phi::Vocab>();
  } else if (var_type == paddle::framework::proto::VarType::PLACE_LIST) {
    var->GetMutable<phi::PlaceList>();
  } else if (var_type == paddle::framework::proto::VarType::READER) {
    var->GetMutable<paddle::framework::ReaderHolder>();
  } else if (var_type == paddle::framework::proto::VarType::RAW) {
    // GetMutable will be called in operator
  } else {
    PADDLE_THROW(common::errors::Unavailable(
        "paddle::framework::Variable type %d is not in "
        "[DENSE_TENSOR, SELECTED_ROWS, FEED_MINIBATCH, FETCH_LIST, "
        "LOD_RANK_TABLE, PLACE_LIST, READER, RAW].",
        var_type));
  }
}

/* GetPlace */
template <typename VarType>
const phi::Place &GetPlace(const std::shared_ptr<VarType> &var) {
  paddle::framework::Variable variable = var->Var();
  if (variable.IsType<phi::DenseTensor>()) {
    return variable.Get<phi::DenseTensor>().place();
  } else if (variable.IsType<phi::SelectedRows>()) {
    return variable.Get<phi::SelectedRows>().place();
  } else {
    PADDLE_THROW(common::errors::InvalidArgument(
        "Variable type is %s, expect DenseTensor or SelectedRows.",
        paddle::framework::ToTypeName(var->Var().Type())));
  }
}
template const phi::Place &GetPlace<VarBase>(
    const std::shared_ptr<VarBase> &var);
template const phi::Place &GetPlace<VariableWrapper>(
    const std::shared_ptr<VariableWrapper> &var);
template const phi::Place &GetPlace<egr::EagerVariable>(
    const std::shared_ptr<egr::EagerVariable> &var);

/* GetNameFromVar */
template <typename VarType>
const std::string &GetNameFromVar(std::shared_ptr<VarType> var) {
  return var->Name();
}
template <>
const std::string &GetNameFromVar<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> tensor) {
  return tensor->name();
}
template const std::string &GetNameFromVar<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var);
template const std::string &GetNameFromVar<VarBase>(
    std::shared_ptr<VarBase> var);

/* SetType */
template <typename VarType>
void SetType(std::shared_ptr<VarType> var,
             framework::proto::VarType::Type type) {
  var->SetType(type);
}
template <>
void SetType<egr::EagerVariable>(std::shared_ptr<egr::EagerVariable> var,
                                 framework::proto::VarType::Type type) {
  switch (type) {
    case paddle::framework::proto::VarType::DENSE_TENSOR: {
      var->MutableVar()->GetMutable<phi::DenseTensor>();
      break;
    }
    case paddle::framework::proto::VarType::SELECTED_ROWS: {
      var->MutableVar()->GetMutable<phi::SelectedRows>();
      break;
    }
    default: {
      PADDLE_THROW(common::errors::NotFound(
          "Cannot found var type: %s while running runtime InferVarType",
          paddle::framework::ToTypeName(type)));
    }
  }
}
template void SetType<VarBase>(std::shared_ptr<VarBase> var,
                               framework::proto::VarType::Type type);
template void SetType<VariableWrapper>(std::shared_ptr<VariableWrapper> var,
                                       framework::proto::VarType::Type type);

/* GetType */
template <typename VarType>
framework::proto::VarType::Type GetType(std::shared_ptr<VarType> var) {
  return var->Type();
}
template <>
framework::proto::VarType::Type GetType<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> var) {
  if (var->Var().IsInitialized()) {
    return paddle::framework::ToVarType(var->Var().Type());
  } else {
    return paddle::framework::proto::VarType::DENSE_TENSOR;
  }
}
template framework::proto::VarType::Type GetType<VarBase>(
    std::shared_ptr<VarBase> var);
template framework::proto::VarType::Type GetType<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var);

/* GetDataType */
template <typename VarType>
framework::proto::VarType::Type GetDataType(std::shared_ptr<VarType> var) {
  return var->DataType();
}
template <>
framework::proto::VarType::Type GetDataType<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> var) {
  if (var->Var().IsType<phi::SelectedRows>()) {
    return framework::TransToProtoVarType(
        var->Var().Get<phi::SelectedRows>().value().type());
  } else if (var->Var().IsType<phi::DenseTensor>()) {
    return framework::TransToProtoVarType(
        var->Var().Get<phi::DenseTensor>().type());
  } else {
    PADDLE_THROW(common::errors::PermissionDenied(
        "We only support phi::SelectedRows and phi::DenseTensor in "
        "eager mode, but we got %s here, please checkout your var type of "
        "tensor: %s",
        paddle::framework::ToTypeName(framework::ToVarType(var->Var().Type())),
        var->name()));
  }
}
template framework::proto::VarType::Type GetDataType<VarBase>(
    std::shared_ptr<VarBase> var);
template framework::proto::VarType::Type GetDataType<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var);

/* GetDataLayout */
template <typename VarType>
phi::DataLayout GetDataLayout(std::shared_ptr<VarType> var) {
  return var->DataLayout();
}
template <>
phi::DataLayout GetDataLayout<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> var) {
  if (var->Var().IsType<phi::DenseTensor>()) {
    return var->Var().Get<phi::DenseTensor>().layout();
  } else {
    PADDLE_THROW(common::errors::PermissionDenied(
        "Only support phi::DenseTensor, but got %s here, please checkout "
        "var type of "
        "tensor: %s",
        paddle::framework::ToTypeName(framework::ToVarType(var->Var().Type())),
        var->name()));
  }
}
template phi::DataLayout GetDataLayout<VarBase>(std::shared_ptr<VarBase> var);
template phi::DataLayout GetDataLayout<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var);

/* SetDataLayout */
template <typename VarType>
void SetDataLayout(std::shared_ptr<VarType> var, const phi::DataLayout layout) {
  var->SetDataLayout(layout);
}
template <>
void SetDataLayout<egr::EagerVariable>(std::shared_ptr<egr::EagerVariable> var,
                                       const phi::DataLayout layout) {
  if (var->Var().IsType<phi::DenseTensor>()) {
    var->MutableVar()->GetMutable<phi::DenseTensor>()->set_layout(layout);
  } else {
    PADDLE_THROW(common::errors::PermissionDenied(
        "Only support phi::DenseTensor, but got %s here, please checkout "
        "var type of "
        "tensor: %s",
        paddle::framework::ToTypeName(framework::ToVarType(var->Var().Type())),
        var->name()));
  }
}
template void SetDataLayout<VarBase>(std::shared_ptr<VarBase> var,
                                     const phi::DataLayout layout);
template void SetDataLayout<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var, const phi::DataLayout layout);

/* CheckCachedKey */
template <typename VarType>
bool CheckCachedKey(std::shared_ptr<VarType> var, const phi::KernelKey &key) {
  return GetVariableWrapper(var)->hasCacheKey(key);
}
template <>
bool CheckCachedKey<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> tensor, const phi::KernelKey &key) {
  // TODO(jiabin): Support this later
  // VLOG(10) << "CheckCachedKey with tensor: " << tensor->name() << "and key is
  // equal to self: " << key == key.
  return false;
}
template bool CheckCachedKey<VarBase>(std::shared_ptr<VarBase> var,
                                      const phi::KernelKey &key);
template bool CheckCachedKey<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var, const phi::KernelKey &key);

/* GetCachedValue */
template <typename VarType>
std::shared_ptr<VariableWrapper> GetCachedValue(std::shared_ptr<VarType> var,
                                                const phi::KernelKey &key) {
  return GetVariableWrapper(var)->getCacheValue(key);
}
template <>
std::shared_ptr<VariableWrapper> GetCachedValue(
    std::shared_ptr<egr::EagerVariable> var, const phi::KernelKey &key) {
  // TODO(jiabin): Support this later
  //   PADDLE_THROW(common::errors::Fatal("In eager mode program should not
  //   reach this, support cache and remove this error check later, or this
  //   should not be supported."));
  //   VLOG(10) << "CheckCachedKey with tensor: " << tensor->name() << "and key
  //   is equal to self: " << key == key.
  return std::make_shared<VariableWrapper>("");
}
template std::shared_ptr<VariableWrapper> GetCachedValue<VarBase>(
    std::shared_ptr<VarBase> var, const phi::KernelKey &key);
template std::shared_ptr<VariableWrapper> GetCachedValue<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var, const phi::KernelKey &key);

/* SetCachedValue */
template <typename VarType>
void SetCachedValue(std::shared_ptr<VarType> var,
                    const phi::KernelKey &key,
                    std::shared_ptr<VarType> res) {
  GetVariableWrapper(var)->setCacheValue(key, GetVariableWrapper(res));
}
template <>
void SetCachedValue<egr::EagerVariable>(
    std::shared_ptr<egr::EagerVariable> tensor,
    const phi::KernelKey &key,
    std::shared_ptr<egr::EagerVariable> res) {
  //   PADDLE_THROW(common::errors::Fatal("In eager mode program should not
  //   reach this, support cache and remove this error check later, or this
  //   should not be supported."));
  //   VLOG(10) << "CheckCachedKey with tensor: " << tensor->name() << "and key
  //   is equal to self: " << key == key << " and res name is:" << res->Name().
}
template void SetCachedValue<VarBase>(std::shared_ptr<VarBase> var,
                                      const phi::KernelKey &key,
                                      std::shared_ptr<VarBase> res);
template void SetCachedValue<VariableWrapper>(
    std::shared_ptr<VariableWrapper> var,
    const phi::KernelKey &key,
    std::shared_ptr<VariableWrapper> res);
}  // namespace paddle::imperative
