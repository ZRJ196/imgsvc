#include "postprocess.h"
#include <iostream>

bool saveCutout(const std::string& outPath, const cv::Mat& orig,
                const std::vector<float>& mask)
{
    const int W = 320;
    const int H = 320;

    // 用 OpenCV 包一层（不拷贝数据，只读）
    // mask 是 const，但 cv::Mat 的构造函数只接受非 const 指针，所以 const_cast
    cv::Mat maskMat(H, W, CV_32FC1, const_cast<float*>(mask.data()));

    // ---- 放大回原图尺寸 ----
    // 注意：这里是【放大】，用 INTER_LINEAR；缩小时才用 INTER_AREA
    cv::Mat maskFull;
    cv::resize(maskMat, maskFull, orig.size(), 0, 0, cv::INTER_LINEAR);

    // ---- 转成 8 位灰度（0~255）----
    cv::Mat mask8;
    maskFull.convertTo(mask8, CV_8U, 255.0);

    // 顺手存一份掩码，方便肉眼确认模型学到了什么
    // TODO: 路径先写死，以后改成从配置读
    cv::imwrite("out/mask.png", mask8);

    // ---- 把掩码作为 alpha 通道，和原图合并成 BGRA ----
    std::vector<cv::Mat> ch;
    cv::split(orig, ch);          // BGR → 三个单通道
    ch.push_back(mask8);          // 加上 alpha
    cv::Mat rgba;
    cv::merge(ch, rgba);          // → BGRA 四通道

    if (!cv::imwrite(outPath, rgba)) {
        std::cout << "写入失败: " << outPath << std::endl;
        return false;
    }
    return true;
}
