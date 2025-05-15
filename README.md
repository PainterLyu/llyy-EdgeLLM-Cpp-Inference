# llyy_llm_inference: 基于llama.cpp的轻量级大模型推理与交互平台

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT) ## 🌟 项目简介 (Introduction)

本项目 `llyy_llm_inference` 是一个基于强大的 `llama.cpp` 框架构建的C++大语言模型推理与交互服务平台。核心目标是解决在资源受限的边缘计算设备（如Raspberry Pi 4B和NVIDIA Jetson Nano）上高效部署和运行3B规模大语言模型的挑战。通过集成先进的模型量化技术、优化的计算图机制、灵活的后端调度以及高效的并发处理框架，本项目旨在提供一个轻量化、低依赖、高性能的本地LLM推理解决方案，并支持实时的流式交互体验。

`llyy_llm_inference` is a C++ based Large Language Model (LLM) inference and interaction service platform built upon the powerful `llama.cpp` framework. The primary goal is to address the challenges of efficiently deploying and running 3B-parameter LLMs on resource-constrained edge computing devices such as the Raspberry Pi 4B and NVIDIA Jetson Nano. By integrating advanced model quantization techniques, optimized computation graph mechanisms, flexible backend scheduling, and an efficient concurrent processing framework, this project aims to deliver a lightweight, low-dependency, high-performance local LLM inference solution with real-time streaming interaction capabilities.

## ✨ 主要特性 (Key Features)

* **边缘设备优化部署**:
    * 在 **Raspberry Pi 4B** 上以 `ctx` 模式高效运行。
    * 在 **NVIDIA Jetson Nano** 上利用 `backend` 模式实现 **CPU/GPU混合推理**，优化异构硬件性能。
* **高效模型量化**:
    * 采用 **Q4_K_M 4-bit量化** 技术，将3B模型的内存占用从约2.8GB显著压缩至约1.0GB。
* **先进的推理引擎**:
    * 基于 `llama.cpp` 的 `ggml` 库，利用 **cgraph计算图机制** 高效构建和管理推理流程。
    * 结合 **sched后端调度器** 实现子图分割，支持灵活的计算任务分配。
* **高并发处理**:
    * 实现 **请求槽 (Request Slots) + 动态批处理 (Dynamic Batching)** 并发框架，有效提升服务吞吐量和多用户响应能力。
* **灵活的采样策略**:
    * 封装 **Min-p、Temperature、Distribution (Deterministic/Greedy)** 三种核心采样策略链，允许运行时灵活调整生成文本的风格和多样性。
* **实时交互与监控**:
    * 构建**前后端分离架构**，后端为纯C++实现，前端采用Vue.js。
    * 通过 **WebSocket / Server-Sent Events (SSE)** 实现逐token的实时流式输出。
    * 支持实时**性能监控**，展示如推理速度 (tokens/s)、内存占用等关键指标。
* **轻量化与低依赖**:
    * 后端核心逻辑**无重量级HTTP库依赖** (仅依赖cJSON进行请求解析，nlohmann::json进行指标格式化)，提升了在嵌入式场景下的可移植性和轻量化水平。
* **目标推理性能**: 在边缘设备上实现了约 **4.8 tokens/s** 的推理速度。

## 🚀 技术栈 (Tech Stack)

* **核心框架**: `llama.cpp` (ggml)
* **编程语言**: C++ (后端), Vue.js (前端，可选展示)
* **量化技术**: Q4_K_M (ggml)
* **并发模型**: 请求槽, 动态批处理
* **通信协议**: HTTP, WebSocket, Server-Sent Events (SSE)
* **数据格式**: JSON (cJSON, nlohmann::json)
* **目标硬件**: Raspberry Pi 4B, NVIDIA Jetson Nano

## 🛠️ 项目结构 (Project Structure)




前后端配置信息
![image](https://github.com/user-attachments/assets/0b4582d5-d61c-4f3a-9f46-f36bcfac5729)
![image](https://github.com/user-attachments/assets/24c0b527-2c76-4fb4-8ed8-c3562a5c68d5)
