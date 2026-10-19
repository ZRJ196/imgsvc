#include "preprocess.h"
#include "infer.h"
#include "postprocess.h"

#include <iostream>
#include <string>
#include <vector>

// 图像抠图工具：读图 → 预处理 → 模型推理 → 输出透明背景 PNG
int main(int argc, char* argv[])
{
    // ---- 参数检查 ----
    if (argc < 3) {
        std::cout << "用法: " << argv[0] << " <输入图片> <输出图片>" << std::endl;
        return 1;
    }
    if (std::string(argv[1]) == std::string(argv[2])) {
        std::cout << "输入图片和输出图片不能相同" << std::endl;
        return 1;
    }

    // ---- ① 读图 + 预处理 ----
    cv::Mat blob, orig;
    if (!preprocess(argv[1], blob, orig)) {
        return 1;
    }

    // ---- ② 加载模型（只加载一次）----
    InferEngine engine("models/u2net.onnx");
    if (!engine.ok()) {
        return 1;
    }

    // ---- ③ 推理 ----
    std::vector<float> mask;
    if (!engine.run(blob, mask)) {
        return 1;
    }

    // ---- ④ 后处理 + 保存 ----
    if (!saveCutout(argv[2], orig, mask)) {
        return 1;
    }

    std::cout << "完成: " << argv[2] << std::endl;
    return 0;
}
