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

### 1. undefined reference 的两种方向
- 现象A：undefined reference to 'main'      → 没有任何 .o 提供 main
- 现象B：undefined reference to 'infer(...)' → 有 .o 需要 infer，但没人提供
- 共同点：都是【链接阶段】的错误，都是"符号的供需对不上"
- 判断方法：如果报错发生在编译的最后一个环节（已经生成了 .o），就是链接问题
- 学到的：C++ 是分离编译——每个 .cpp 独立编译成 .o，最后由链接器拼装

### 2. 头文件为什么只有声明
- 声明告诉编译器"函数长什么样"（名字/参数类型/返回类型）
- 编译器据此生成调用代码，在 .o 里留一个【未解析的符号引用】
- 具体实现在哪，编译 main.cpp 时不需要知道——那是链接器的事
- 所以声明和定义必须完全一致，否则符号名对不上（参数类型不同→mangled name 不同）

### 3. 悬垂指针（dangling pointer）
- 现象：函数返回指向局部对象内部数据的指针；调用者拿到后内存已被释放
- 为什么危险：释放≠清零。内存内容往往还在，所以【看起来正常】
  但内存随时可能被复用 → 数据变垃圾、或崩溃
  属于【未定义行为】，每次跑结果可能不同 → 极难复现
- 怎么防：把数据拷贝出来（mask.assign），别把局部对象的内存传出去
- 怎么检测：-fsanitize=address 会报 heap-use-after-free