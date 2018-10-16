#include "oneflow/core/kernel/accuracy_kernel.h"

namespace oneflow {

template<DeviceType device_type, typename PredType, typename LabelType>
int32_t AccuracyKernel<device_type, PredType, LabelType>::AddInstanceNum(
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  int32_t instance_num_sum = 0;
  Blob* label_blob = BnInOp2Blob("label");
  if (label_blob->has_dim0_valid_num_field()) {
    for (int32_t i = 0; i < label_blob->dim0_inner_shape().At(0); i++) {
      instance_num_sum += label_blob->dim0_valid_num(i);
    }
  } else {
    instance_num_sum = label_blob->static_shape().At(0);
  }
  return instance_num_sum;
}

template<DeviceType device_type, typename PredType, typename LabelType>
void AccuracyKernel<device_type, PredType, LabelType>::SetInstanceNumSum(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  int32_t instance_num_sum = AddInstanceNum(BnInOp2Blob);
  KernelUtil<device_type, PredType>::Set(ctx.device_ctx, static_cast<PredType>(instance_num_sum),
                                         BnInOp2Blob("batch_instance_num")->mut_dptr<PredType>());
}

template<DeviceType device_type, typename PredType, typename LabelType>
void AccuracyKernel<device_type, PredType, LabelType>::ForwardDataContent(
    const KernelCtx& ctx, std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  const Blob* X = BnInOp2Blob("prediction");
  const Blob* label = BnInOp2Blob("label");
  Blob* accuracy = BnInOp2Blob("accuracy");
  auto kernel_conf = this->kernel_conf();
  const int32_t top_k = kernel_conf.op_attribute().op_conf().accuracy_conf().top_k();
  int32_t N = BnInOp2Blob("prediction")->shape().At(0);
  int32_t D = BnInOp2Blob("prediction")->shape().Count(1);
  CHECK_EQ(label->shape().NumAxes(), 1);
  CHECK_EQ(X->blob_desc().shape().At(0), N);

  AccuracyKernelUtil<device_type, PredType, LabelType>::Forward(
      ctx.device_ctx, N, D, top_k, X->dptr<PredType>(), label->dptr<LabelType>(),
      accuracy->mut_dptr<PredType>());
  SetInstanceNumSum(ctx, BnInOp2Blob);
}

template<typename PredType, typename LabelType>
struct AccuracyKernelUtil<DeviceType::kCPU, PredType, LabelType> {
  static void Forward(DeviceCtx* ctx, const int32_t N, const int32_t D, int32_t top_k,
                      const PredType* XData, const LabelType* labelData, PredType* accuracyData) {
    int correct = 0;
    for (int i = 0; i < N; ++i) {
      auto label_i = labelData[i];
      auto label_pred = XData[i * D + label_i];
      int cnt = 1;
      for (int j = 0; j < D; ++j) {
        auto pred = XData[i * D + j];
        if (pred > label_pred) {
          if (++cnt > top_k) { break; }
        }
      }
      if (cnt <= top_k) { ++correct; }
    }
    CHECK_LE(correct, N);
    *accuracyData = static_cast<PredType>(correct);
  }
};

namespace {

Kernel* CreateAccuracyKernel(const KernelConf& kernel_conf) {
  static const HashMap<std::string, std::function<Kernel*()>> creators = {
#define ACCURACY_KERNEL_ENTRY(device_type, pred_type_pair, label_type_pair)                        \
  {GetHashKey(device_type, OF_PP_PAIR_SECOND(pred_type_pair), OF_PP_PAIR_SECOND(label_type_pair)), \
   []() {                                                                                          \
     return new AccuracyKernel<device_type, OF_PP_PAIR_FIRST(pred_type_pair),                      \
                               OF_PP_PAIR_FIRST(label_type_pair)>();                               \
   }},
      OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(ACCURACY_KERNEL_ENTRY, DEVICE_TYPE_SEQ,
                                       FLOATING_DATA_TYPE_SEQ, INT_DATA_TYPE_SEQ)};
  return creators.at(GetHashKey(kernel_conf.op_attribute().op_conf().device_type(),
                                kernel_conf.accuracy_conf().prediction_type(),
                                kernel_conf.accuracy_conf().label_type()))();
}

}  // namespace

REGISTER_KERNEL_CREATOR(OperatorConf::kAccuracyConf, CreateAccuracyKernel);

#define MAKE_ENTRY(data_type_pair, label_type_pair)                                      \
  template struct AccuracyKernelUtil<DeviceType::kCPU, OF_PP_PAIR_FIRST(data_type_pair), \
                                     OF_PP_PAIR_FIRST(label_type_pair)>;

OF_PP_SEQ_PRODUCT_FOR_EACH_TUPLE(MAKE_ENTRY, FLOATING_DATA_TYPE_SEQ, INT_DATA_TYPE_SEQ)

}  // namespace oneflow
