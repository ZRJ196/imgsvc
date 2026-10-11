本文件是记录一些常用的API

## OpenCV
### 读图 + 判空（必做）
cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
if (img.empty()) { ... }        // imread 失败不报错，返回空 Mat

### resize 的插值选择（官方头文件注释）
- 缩小 → INTER_AREA（decimation 首选，moire-free）
- 放大 → INTER_CUBIC / INTER_LANCZOS4
- 默认 INTER_LINEAR 不是最优，要显式指定

### blobFromImage 能做什么、不能做什么
能做：resize + swapRB + /255 + HWC→NCHW
不能做：按通道的 (x-mean)/std（scalefactor 只有一个数）

## ONNX Runtime
### 完整推理骨架
（加载 session → 查 I/O → CreateTensor → Run → GetTensorMutableData）
注意：CreateTensor 是【零拷贝】的，blob 必须活到 Run 结束
注意：输出名字可能是 PyTorch 的内部 ID（如 1959），要用【索引】取不要用名字