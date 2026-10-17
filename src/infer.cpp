#include "infer.h"
#include <onnxruntime_cxx_api.h>
#include <iostream>

bool infer(const std::string& modelPath, const cv::Mat& blob, std::vector<float>& mask)
{
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "imgsvc");      // 全局环境
        Ort::SessionOptions opts;                              // 会话配置
        Ort::Session session(env, modelPath.c_str(), opts);    // 加载模型
        Ort::AllocatorWithDefaultOptions alloc;                // 用于接收 ORT 返回的字符串

        // ---- 打印模型的输入输出接口（换模型时能看到要求）----
        size_t nIn = session.GetInputCount();
        for (size_t i = 0; i < nIn; ++i) {
            auto name  = session.GetInputNameAllocated(i, alloc);
            auto shape = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
            std::cout << "输入[" << i << "]: " << name.get() << "  shape=[";
            for (auto d : shape) std::cout << d << ",";
            std::cout << "]" << std::endl;
        }
        size_t nOut = session.GetOutputCount();
        std::cout << "输出个数: " << nOut << std::endl;

        // ---- 把 blob 包装成 ORT 输入张量 ----
        std::vector<int64_t> inputShape = {1, 3, 320, 320};
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // ORT 的 CreateTensor 只接受非 const 指针（它内部不改数据，但 API 没标 const），
        // 所以这里做一个 const_cast。CreateTensor 不拷贝数据，只是"指向"这块内存。
        float* blobData = const_cast<float*>(blob.ptr<float>());

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
                memInfo, blobData, 3 * 320 * 320,
                inputShape.data(), inputShape.size());

        // ---- 取输入输出的名字：按索引取，不要写死 ----
        // 因为这个模型的输出名字是 PyTorch 内部张量 ID（1959、1960…），
        // 换个导出环境就会变。
        auto inName  = session.GetInputNameAllocated(0, alloc);
        auto outName = session.GetOutputNameAllocated(0, alloc);
        const char* inNames[]  = { inName.get() };
        const char* outNames[] = { outName.get() };

        // ---- 推理（只请求第 0 个输出）----
        auto outputs = session.Run(Ort::RunOptions{},
                                   inNames,  &inputTensor, 1,
                                   outNames, 1);

        // ---- 把结果【拷贝】出来（关键！）----
        // outputs 是局部变量，函数返回时就析构了，它持有的内存会被释放。
        // 如果只把指针传出去，调用者拿到的是悬垂指针（野指针）——
        // 这是 C++ 最经典的坑之一，而且通常不报错、只是数据变成垃圾。
        float* p = outputs[0].GetTensorMutableData<float>();
        mask.assign(p, p + 320 * 320);

        return true;
    } catch (const Ort::Exception& e) {
        std::cout << "推理失败: " << e.what() << std::endl;
        return false;
    }
}
