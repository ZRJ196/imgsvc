#include<iostream>
#include<opencv2/opencv.hpp>
#include<string>
#include<onnxruntime_cxx_api.h>
#include<vector>
using namespace std;
int main(int argc,char* argv[])//argc参数个数 argv参数 argv[0]是程序名 都在命令行输入
{
    if(argc<3)
    {
        cout<<"用法："<<argv[0]<<"<输入图片><输出图片>"; //当命令输入不合法时提示
        return 1;
    }
    
    if((string)argv[1] == (string) argv[2])
    {
        cout<<"输入图片和输出图片不能相同"<<endl;
        return 1;
    }

    // ---- 加载 ONNX 模型 ---- 了解onnx模型的输入输出接口
try {
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "imgsvc");   // 全局环境，一个进程一个
    Ort::SessionOptions opts;                            // 会话配置（现在用默认）
    Ort::Session session(env, "models/u2net.onnx", opts);// 加载进内存的模型实例

    Ort::AllocatorWithDefaultOptions alloc;              // 分配器 用来接收 ORT 返回的字符串

    // ---------- 输入：----------
    size_t nIn = session.GetInputCount();//输入节点数量
    cout << "输入个数: " << nIn << endl;
    for (size_t i = 0; i < nIn; ++i) {
        auto name  = session.GetInputNameAllocated(i, alloc);//第i个输入的名称
        auto shape = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();//类型信息.张量信息.维度
        cout << "输入[" << i << "]: " << name.get() << "  shape=[";
        for (auto d : shape) cout << d << ",";
        cout << "]" << endl;
    }

    // ---------- 输出： ----------
    size_t nOut=session.GetOutputCount();
    cout<<"输出个数："<<nOut<<endl;
    for(size_t i=0;i<nOut;++i) {
        auto name = session.GetOutputNameAllocated(i,alloc);
        auto shape = session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape();
        cout<<"输出["<<i<<"]:"<<name.get()<<" shape=[";
        for(auto d:shape) cout<<d<<",";
        cout<<"]"<<endl;
    }
} catch (const Ort::Exception& e) {
    cout << "模型加载失败: " << e.what() << endl;//当try中的代码有误时进入catch
    return 1;
}//这一步是为了搞清楚onnx模型的输入输出节点的名称 属性 输入节点[1,3,320,320]表示输入一张3通道320*320的图
  // 输出节点7个[1,1,320,320]表示从7个不同层输出1个单通道的320*320图 用第0个推理


    cv::Mat img=cv::imread(argv[1],cv::IMREAD_COLOR); //Mat表示矩阵 imread读取图像 IMREAD_COLOR强制读3通道彩色BGR

    if(img.empty())
    {
        cout<<"图片不存在或者打不开:"<<argv[1]<<endl;
        return 1;
    }
    cv::Mat blob = cv::dnn::blobFromImage(// 作用 resize + BGR→RGB + /255 + HWC→NCHW（深度学习模型）
        img,
        1.0 / 255.0,          // scalefactor：实现 /255
        cv::Size(320, 320),   // 目标尺寸（模型要求的）
        cv::Scalar(),         // mean 留空，后面自己按通道减
        true,                 // swapRB：BGR → RGB
        false,                // crop：false = 直接拉伸（符合 U²-Net）
        CV_32F);
    
const float mean[3] = {0.485f, 0.456f, 0.406f};   // RGB 顺序 两个数组分别是R G B的均值和标准差
const float stdv[3] = {0.229f, 0.224f, 0.225f};
const int HW = 320 * 320;

float* p = blob.ptr<float>();     // blob 的数据首地址 相当于是把blob看作成一块连续的float数组
for(int i=0;i<3;++i)
{
    for(int j=0;j<HW;++j)
    {
        p[i*HW+j]=(p[i*HW+j]-mean[i])/stdv[i];
    }
}
double mn = 0, mx = 0;
cv::minMaxIdx(blob, &mn, &mx);
cout << "张量形状: [" << blob.size[0] << "," << blob.size[1] << ","
     << blob.size[2] << "," << blob.size[3] << "]" << endl;
cout << "数值范围: min=" << mn << "  max=" << mx << endl;

    return 0;
}