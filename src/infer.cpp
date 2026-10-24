#include "infer.h"

#include <iostream>

InferEngine::InferEngine(const std::string& modelPath)
    : env_(ORT_LOGGING_LEVEL_WARNING, "imgsvc")
{
    try {
        // 模型只在这里加载一次
        session_ = std::make_unique<Ort::Session>(env_, modelPath.c_str(), opts_);

        // 取出输入输出的名字：按索引取
        // （这个模型的输出名字是 PyTorch 内部张量 ID，如 1959，换个导出环境就变）
        auto in  = session_->GetInputNameAllocated(0, alloc_);
        auto out = session_->GetOutputNameAllocated(0, alloc_);
        in_name_  = in.get();
        out_name_ = out.get();

        // 打印模型接口（换模型时一眼就能看到要求）
        auto shape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        std::cout << "模型已加载: 输入 [" << in_name_ << "] shape=[";
        for (auto d : shape) std::cout << d << ",";
        std::cout << "]  输出 [" << out_name_ << "] (共 "
                  << session_->GetOutputCount() << " 个输出)" << std::endl;

    } catch (const Ort::Exception& e) {
        std::cout << "模型加载失败: " << e.what() << std::endl;
        session_.reset();               // ok() 会返回 false
    }
}

bool InferEngine::run(const cv::Mat& blob, std::vector<float>& mask)
{
    if (!session_) {
        return false;
    }

    try {
        const int64_t shape_arr[4] = {1, 3, 320, 320};
        std::vector<int64_t> shape(shape_arr, shape_arr + 4);

        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // CreateTensor 不拷贝数据，只是指向 blob 的内存。
        // ORT 的 API 没标 const，所以做一次 const_cast（它内部不修改数据）
        float* data = const_cast<float*>(blob.ptr<float>());

        Ort::Value input = Ort::Value::CreateTensor<float>(
                memInfo, data, 3 * 320 * 320, shape.data(), shape.size());

        const char* inNames[1]  = { in_name_.c_str() };
        const char* outNames[1] = { out_name_.c_str() };

        auto outputs = session_->Run(Ort::RunOptions{},
                                     inNames,  &input, 1,
                                     outNames, 1);

        // 把结果【拷贝】出来：outputs 是局部变量，函数返回时它持有的内存会被释放，
        // 只传指针出去就是悬垂指针
        float* p = outputs[0].GetTensorMutableData<float>();
        mask.assign(p, p + 320 * 320);

        return true;
    } catch (const Ort::Exception& e) {
        std::cout << "推理失败: " << e.what() << std::endl;
        return false;
    }
}
