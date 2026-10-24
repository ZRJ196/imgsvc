# imgsvc · C++ 图像抠图服务

用 **原生 socket** 手写的 HTTP 服务：浏览器上传图片 → U²-Net 推理抠图 → 返回透明背景 PNG，推理记录入 MySQL，可查询历史。

**不依赖 Web 框架、不依赖 HTTP 库**——HTTP 报文解析、TCP 字节流分帧、并发模型全部自己实现。

<p align="center">
  <em>输入：一张普通照片 → 输出：透明背景 PNG</em>
</p>

---

## 目录

- [功能](#功能)
- [技术栈](#技术栈)
- [快速开始](#快速开始)
- [接口](#接口)
- [架构](#架构)
- [性能数据](#性能数据)
- [设计取舍](#设计取舍)
- [已知限制](#已知限制)
- [踩坑集](#踩坑集)
- [项目结构](#项目结构)

---

## 功能

| 能力 | 说明 |
|---|---|
| **浏览器抠图** | 拖拽/点击上传，前端展示原图与结果对比 |
| **透明背景 PNG** | 输出 8-bit RGBA，背景透明 |
| **命令行工具** | `imgsvc <输入> <输出>`，同一套模块的第二个入口 |
| **推理记录入库** | SHA-256 指纹、尺寸、耗时、时间写入 MySQL |
| **历史查询** | `GET /history` 返回最近 20 条记录的 JSON |
| **静态文件服务** | 带路径穿越防护（`realpath` 规范化 + 前缀校验） |

### 两个可执行文件，共享同一套模块

```
build/imgsvc          命令行工具（调试 / 基准 / 交叉验证）
build/imgsvc-server   HTTP 服务
build/test_db         数据库模块的独立测试
```

**`imgsvc` 不是为了凑数**——它是调试和验证工具：

1. **模块出问题时**，不经过网络层直接复现（省掉 HTTP 这一层干扰）
2. **交叉验证**：同一张图走命令行和走 HTTP，输出的 **SHA-256 完全一致**，用来证明模块化重构没有改变行为
3. **性能基准**：测纯计算耗时（不含网络、不含模型加载），和服务器耗时相减可以算出各层开销

---

## 技术栈

| 层 | 选型 | 版本 |
|---|---|---|
| 语言 | C++17 | g++ 13.3.0 |
| 构建 | CMake | 3.28.3 |
| 图像处理 | OpenCV | 4.6.0 |
| 推理引擎 | ONNX Runtime (CPU) | 1.30.0 |
| 模型 | U²-Net（显著目标检测） | u2net.onnx, 167 MB |
| 数据库 | MySQL | 8.0.46 |
| 哈希 | OpenSSL (`EVP_Digest`) | 3.0.13 |
| JSON | nlohmann/json | 3.11.3 |
| 网络 | **原生 POSIX socket** | — |
| 前端 | **原生 JS**（无框架） | — |
| 运行环境 | Ubuntu 24.04 (WSL2) | — |

---

## 快速开始

### 1. 安装依赖

```bash
sudo apt install -y build-essential cmake \
    libopencv-dev libmysqlclient-dev libssl-dev nlohmann-json3-dev \
    mysql-server
```

**ONNX Runtime**（官方 tarball 解压到 `/opt/onnxruntime`）：

```bash
# 从 https://github.com/microsoft/onnxruntime/releases 下载
#   onnxruntime-linux-x64-1.30.0.tgz
sudo tar xzf onnxruntime-linux-x64-1.30.0.tgz -C /opt
sudo mv /opt/onnxruntime-linux-x64-1.30.0 /opt/onnxruntime
```

### 2. 准备模型

```bash
mkdir -p models
# 把 u2net.onnx 放到 models/ 下（167 MB，不纳入 git）
ls -lh models/u2net.onnx
```

### 3. 初始化数据库

```bash
sudo mysql <<'SQL'
CREATE DATABASE imgsvc CHARACTER SET utf8mb4;
CREATE USER 'imgsvc'@'localhost' IDENTIFIED BY 'imgsvc123';
GRANT ALL PRIVILEGES ON imgsvc.* TO 'imgsvc'@'localhost';
FLUSH PRIVILEGES;
SQL

mysql -uimgsvc -pimgsvc123 imgsvc <<'SQL'
CREATE TABLE infer_log (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  image_name  VARCHAR(255)    NOT NULL,
  image_hash  CHAR(64)        NOT NULL COMMENT '图片内容 SHA-256',
  result_json JSON            DEFAULT NULL,
  cost_ms     INT UNSIGNED    NOT NULL DEFAULT 0,
  created_at  TIMESTAMP       NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  KEY idx_created_at (created_at),
  KEY idx_image_hash (image_hash)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
SQL
```

### 4. 编译

```bash
cmake -B build              # 生成 Makefile（第一次 / 改了 CMakeLists 后跑）
cmake --build build         # 编译链接（每次改 .cpp 后跑）
```

产物：

```
build/imgsvc
build/imgsvc-server
build/test_db
```

### 5. 运行

**命令行工具：**

```bash
./build/imgsvc data/test.jpg out/result.png
file out/result.png
# → PNG image data, 1245 x 843, 8-bit/color RGBA
```

**HTTP 服务：**

```bash
./build/imgsvc-server
# [db] 已连接 imgsvc@127.0.0.1/imgsvc
# HTTP 服务端启动，监听端口 8080，静态目录 static/
```

**浏览器打开 <http://127.0.0.1:8080/>**，上传图片即可。

**或用 curl：**

```bash
curl -X POST --data-binary @data/test.jpg \
     -H "X-Filename: 消防栓.jpg" \
     -o out/result.png \
     http://127.0.0.1:8080/infer
```

> **注意**：curl 要用 `--data-binary`，不能用 `-d`。
> `-d @file` 会**去掉末尾换行符**（form 表单语义），导致图片少字节、无法解码。

**查询历史：**

```bash
curl -s http://127.0.0.1:8080/history | python3 -m json.tool
```

**数据库测试：**

```bash
./build/test_db
```

---

## 接口

### `GET /`

返回 `static/index.html`（上传页面）。

### `GET /<path>`

返回 `static/` 下的静态文件。

**安全**：经 `realpath` 规范化绝对路径 + 校验是否仍在 `static/` 内，防路径穿越。

### `GET /history`

返回最近 20 条推理记录的 JSON 数组（最新的在前）。

```json
[
  {
    "id": 2,
    "image_name": "小恐龙.png",
    "image_hash": "1ba6b450a3beec2bc2031cafd2c44628b4d55d2f6be7469cd4f566e5d8a25282",
    "cost_ms": 263,
    "created_at": "2026-10-23 20:31:32"
  }
]
```

### `POST /infer`

**请求**：body 就是图片的**原始字节**（不是 base64，不是 multipart）。

可选请求头：

| 头 | 说明 |
|---|---|
| `X-Filename: xxx.jpg` | 记录用的文件名（不传则记为 `upload`） |

**成功响应**：

```
HTTP/1.1 200 OK
Content-Type: image/png
Content-Length: 2232006

<PNG 字节>
```

**错误响应**：

| 状态码 | 场景 |
|---|---|
| `400` | 请求格式不对 / 不是有效图片 |
| `404` | 路径不存在 |
| `405` | 方法不支持 |
| `413` | 请求体超过 10 MB |
| `500` | 推理失败 / 生成图片失败 |

### `POST /echo`

把请求 body 原样返回。**仅用于调试**——验证"body 是否收全"。

---

## 架构

### 调用链

```
浏览器 ──HTTP/TCP:8080──→ imgsvc-server（单进程）
                            │
  server.cpp                │  InferEngine engine("models/u2net.onnx");   ← 启动时加载一次
                            │  Db db; db.connect(...);                   ← 连接复用
  ┌─────────────────────────┴──────────────────────────────────────┐
  │ while (1) {                                                    │
  │   accept()  → 收连接                                            │
  │   循环 recv  → 收到完整请求头（找 \r\n\r\n）                      │
  │   解析 Content-Length → 循环 recv 收完整 body                    │
  │                                                                │
  │   preprocess_bytes(bytes, blob, orig)                          │
  │        └─→ imdecode → blobFromImage → 按通道归一化              │
  │                                                                │
  │   engine.run(blob, mask)                                       │
  │        └─→ Ort::Session::Run → 320×320 前景概率                 │
  │                                                                │
  │   make_cutout_png(orig, mask, png)                             │
  │        └─→ 掩码放大 → alpha 通道 → BGRA → imencode              │
  │                                                                │
  │   sha256_hex(bytes)  →  db.log_infer(...)                      │
  │   send_response("200 OK", "image/png", png)                    │
  │   close(conn_fd)                                               │
  │ }                                                              │
  └────────────────────────────────────────────────────────────────┘
```

### 数据在每一步的形状

| 步骤 | 变量 | 类型 / 形状 | 内容 |
|---|---|---|---|
| socket | `body` | `char*`，N 字节 | 上传的**原始文件字节**（JPEG/PNG） |
| 解码 | `orig` | `cv::Mat`，**H×W×3**，`CV_8U` | BGR 像素矩阵 |
| 预处理 | `blob` | `cv::Mat`，**1×3×320×320**，`CV_32F` | 归一化后的张量（NCHW，值域 ≈ -2.1 ~ 2.6） |
| 推理 | `mask` | `std::vector<float>`，**102400 个** | 前景概率 0~1 |
| 后处理 | `png` | `std::vector<unsigned char>` | 编码后的透明 PNG 字节 |

### 模块划分

```
include/            接口（声明）
  preprocess.h      读图/解码 → 张量
  infer.h           InferEngine 类（模型常驻）
  postprocess.h     掩码 → RGBA → PNG
  db.h              Db 类（连接复用 + 重连）
  hash.h            SHA-256

src/                实现
  main.cpp          命令行入口
  server.cpp        HTTP 服务
  preprocess.cpp
  infer.cpp
  postprocess.cpp
  db.cpp
  hash.cpp
```

**分层原则**：`server.cpp` 完全不碰 `Ort::` 和 `cv::` 的底层对象，只调用 5 个函数。换模型 / 换预处理只改模块，网络层一行不动。

---

## 性能数据

> 全部为**实测值**，环境：WSL2 / Ubuntu 24.04 / CPU 推理。

### 模型加载 vs 常驻

| 方案 | 每次请求耗时 | 100 次请求 |
|---|---|---|
| 每次请求重新加载模型 | **0.730 秒** | 73 秒 |
| **模型常驻（类）** | **0.220 秒** | **22 秒** |

**3.3 倍差距。** 这是把推理从"函数"改成"类"（`InferEngine`）的直接依据。

### 端到端（HTTP）

| 场景 | 耗时 |
|---|---|
| 1245×843 图片（202 KB 输入 → 2.2 MB PNG） | **0.25 ~ 0.32 秒** |
| 128×128 图片 | **0.22 ~ 0.28 秒** |
| 服务启动（加载 167 MB 模型 + 连数据库） | **0.62 秒** |

### 索引效果（MySQL，10 万行、数据全在内存）

| 场景 | 无索引 | 有索引 | 倍数 |
|---|---|---|---|
| 按 `image_hash` 精确查 | 8.15 ms | 0.30 ms | **27×** |
| 按 `cost_ms` 排序取 20 | 9.81 ms | 0.077 ms | **128×** |

`EXPLAIN` 的 `rows`：**99994 → 1**（精确查）/ **20**（排序）。

**索引的代价**：10 万行建索引 `ALTER TABLE` 耗时 **5.19 秒 / 1.14 秒**，且每次 INSERT 都要更新索引。

### 资源占用

| 指标 | 值 |
|---|---|
| RSS（模型常驻 + OpenCV + ORT） | **~1.0 GB** |
| 启动时间 | 0.62 秒 |
| 单线程吞吐上限 | **约 4 请求/秒** |

### 正确性验证

| 验证项 | 方法 | 结果 |
|---|---|---|
| 预处理正确性 | 张量 min/max 对比理论边界 | **精确命中 -2.1179 / 2.6400** |
| 模块化重构无回归 | CLI 输出 vs HTTP 输出比 SHA-256 | **完全一致** |
| SHA-256 实现正确 | 与系统 `sha256sum` 对比 | **完全一致** |
| 大 body 收发 | 5 MB 随机二进制往返 | **逐字节一致** |
| 数据库连接恢复 | 外部 `KILL` 连接后继续查询 | **自动重连成功** |
| 路径穿越防护 | 5 种攻击写法（`..`、`%2f`、`//`） | **全部 404** |

---

## 设计取舍
### 主动做的技术选择

| 选择 | 理由 |
|---|---|
| **原始字节传图 + 返回 PNG** | 比 base64/JSON 方案少三层转换，体积少 33% |
| **模型常驻（类而非函数）** | 实测 3.3 倍加速（0.73s → 0.22s） |
| **请求缓冲区只分配一次、复用** | 避免每请求 `malloc(10MB)` |
| **`SO_RCVTIMEO` 超时** | 防止客户端只连不发，把单线程服务器卡死 |
| **`MSG_NOSIGNAL`** | 防止 `send` 到已关闭连接触发 `SIGPIPE` 导致进程静默死亡 |
| **日志走 `stderr`** | `stdout` 重定向到文件时是全缓冲，进程被杀会丢日志；`stderr` 无缓冲 |
| **`realpath` + 前缀校验防路径穿越** | 黑名单永远有绕过方法，规范化后校验才可靠 |

---

## 已知限制

| 限制 | 说明 | 影响 |
|---|---|---|
| **模型只适合单主体图片** | U²-Net 是显著目标检测（SOD），风景照/多主体照片效果差。**实测：大桥照片的掩码最大值只有 0.0384（模型判定"无显著目标"）** | 这是模型能力边界，不是实现 bug |
| **单线程** | 吞吐约 4 请求/秒，并发请求会排队 | 单用户场景足够 |
| **内存约 1 GB** | 模型 167 MB + ORT 中间张量 + OpenCV | 做并发时需重新评估 |
| **无鉴权** | 任何人可调用 | 仅本地使用 |
| **无去重** | 同一张图重复上传会重复推理 | 计划中的功能，尚未实现 |
| **请求体上限 10 MB** | 超过返回 413 | 手机原图可能超限 |
| **无 HTTPS** | 明文传输 | 仅本地使用 |

---

## 踩坑集

**`docs/踩坑集.md`** 记录了项目过程中 **38 个真实踩坑**，每条按【现象 → 定位方法 → 根因 → 解决方案 → 面试话术】组织。

---

## 项目结构

```
imgsvc/
├── CMakeLists.txt              构建配置（3 个 target）
├── project_plan.md             项目计划与阶段划分
├── README.md                   本文件
│
├── include/                    接口（声明）
│   ├── preprocess.h
│   ├── infer.h
│   ├── postprocess.h
│   ├── db.h
│   └── hash.h
│
├── src/                        实现
│   ├── main.cpp                命令行入口
│   ├── server.cpp              HTTP 服务（原生 socket）
│   ├── preprocess.cpp
│   ├── infer.cpp
│   ├── postprocess.cpp
│   ├── db.cpp
│   └── hash.cpp
│
├── tests/
│   └── test_db.cpp             数据库模块的独立测试
│
├── examples/
│   └── mysql_demo.cpp          MySQL C API 的完整示例（学习用）
│
├── docs/
│   ├── 踩坑集.md                38 个坑 + 面试话术
│   ├── 数据库笔记.md            MySQL 最小知识
│   └── snippets.md             常用 API 片段
│
├── static/
│   └── index.html              前端页面（原生 JS）
│
├── models/
│   └── u2net.onnx              模型（167 MB，不纳入 git）
│
├── data/                       测试图片
│
└── build/                      编译产物（不纳入 git）
```

### `.gitignore`

```
build/
out/
models/*.onnx
```

---
模型 U²-Net 版权归原作者（[xuebinqin/U-2-Net](https://github.com/xuebinqin/U-2-Net)）。
