# docs 索引（带编号）

| 编号 | 文档 | 内容 |
|---|---|---|
| 01 | [端侧模型选型与GGUF量化](01-端侧模型选型与GGUF量化.md) | 端侧约束、GGUF/量化档位、模型推荐、获取渠道、采样参数 |
| 02 | [llama.cpp动态库构建指南](02-llama.cpp动态库构建指南.md) | 共享库构建（Win/Linux/macOS/Android/GPU后端）、产物与发布事项 |
| 03 | [llama.cpp动态库C-API调用与集成示例](03-llama.cpp动态库C-API调用与集成示例.md) | 主流程、最小示例、核心API速查、动态加载、三路线对比、踩坑清单 |
| 04 | [ONNX模型与ONNX-Runtime端侧部署](04-ONNX模型与ONNX-Runtime端侧部署.md) | ONNX格式/转换链路、EP机制、GenAI跑LLM、CUDA EP版本要求、端侧形态 |
| 05 | [llama.cpp与ONNX综合能力及架构兼容对比](05-llama.cpp与ONNX综合能力及架构兼容对比.md) | 十维能力对比、40系vs50系架构依赖、CUDA/驱动门槛、共存发布策略 |
| 06 | [llama.cpp预编译、跨架构兼容与CI打包实战](06-llama.cpp预编译、跨架构兼容与CI打包实战.md) | 预编译vs自编、CUDA跨代兼容、官方CI实录、可直接用的打包workflow、例子仓库 |

> 说明：用户原问题"怎么使用llama.cpp的动态库进行…"后半截缺失，默认按"进行推理与应用集成"整理。
> 后续专篇继续编号 07 往后加。

测试模型路径

E:\tools\smodeltrans\models\downloads\hy-mt2-1.8b-q4\Hy-MT2-1.8B-Q4_K_M.gguf

对应仓库网址

https://modelscope.cn/models/Tencent-Hunyuan/Hy-MT2-1.8B/files