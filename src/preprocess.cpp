#include "preprocess.h"
#include <iostream>

bool preprocess(const std::string& imgPath, cv::Mat& blob, cv::Mat& orig)
{
    // ---- 读图 ----
    orig = cv::imread(imgPath, cv::IMREAD_COLOR);
    if (orig.empty()) {
        std::cout << "图片不存在或者打不开: " << imgPath << std::endl;
        return false;
    }

    // ---- 缩放 + BGR→RGB + /255 + HWC→NCHW ----
    // 注意：U²-Net 训练时就是直接拉伸到 320x320（不保持宽高比）
    blob = cv::dnn::blobFromImage(orig, 1.0 / 255.0, cv::Size(320, 320),
                                  cv::Scalar(), true, false, CV_32F);

    // ---- 按通道归一化 (x - mean) / std ----
    // blob 是 NCHW 布局：内存里依次是 [通道0 的全部像素][通道1][通道2]
    // 所以"通道 c 的第 i 个像素"在内存中的下标是 c * HW + i
    const float mean[3] = {0.485f, 0.456f, 0.406f};   // RGB 顺序
    const float stdv[3] = {0.229f, 0.224f, 0.225f};
    const int HW = 320 * 320;

    float* p = blob.ptr<float>();
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < HW; ++i) {
            p[c * HW + i] = (p[c * HW + i] - mean[c]) / stdv[c];
        }
    }

    return true;
}
