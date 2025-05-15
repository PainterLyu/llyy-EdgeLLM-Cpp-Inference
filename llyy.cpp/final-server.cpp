// 编写人：林宇
// 编写日期：2024年12月
// 版本：v4.0
// 1.0版本是一个基础的推理服务器，只支持发送单独一个json到客户端，性能低下
// 2.0版本：升级改进为流式输出
// 3.0版本：优化代码逻辑，将http响应写活
// 4.0版本：增加了一个路径可以查看系统性能(以便于改善模型)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cJSON.h"
#include "llama.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MIMETYPE_JSON "application/json; charset=utf-8"

enum error_type
{
    ERROR_TYPE_INVALID_REQUEST = 400,
    ERROR_TYPE_AUTHENTICATION = 401,
    ERROR_TYPE_NOT_FOUND = 404,
    ERROR_TYPE_NOT_SUPPORTED = 405,
    ERROR_TYPE_UNAVAILABLE = 503,
    ERROR_TYPE_SERVER = 500
};

/**
 * @brief 格式化错误响应消息
 * @param error 错误信息
 * @param type 错误类型枚举值
 * @return 格式化后的HTTP响应字符串
 */
std::string format_error_response(const std::string &error, const enum error_type type)
{
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "\r\n"
             "{\"error\":{\"message\":\"%s\",\"type\":%d,\"code\":%d}}",
             type, error.c_str(), MIMETYPE_JSON, error.c_str(), type, type);
    return std::string(response);
}

std::string format_success_response(const std::string &data)
{
    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "\r\n"
             "%s",
             MIMETYPE_JSON, data.length(), data.c_str());
    return std::string(response);
}

// SSE工具函数声明
void send_sse_headers(int client_socket);
void send_sse_message(int client_socket, const std::string &data);
void send_sse_error(int client_socket, const std::string &error);
void send_sse_done(int client_socket);

// 前向声明LLaMAServer类
class LLaMAServer;

// metrics处理函数声明
void handle_metrics_request(int client_socket, LLaMAServer &llama);

// 使用nlohmann::json
using json = nlohmann::json;

// 在server_metrics定义之前添加server_slot结构体定义
struct server_slot
{
    int id;                        // 槽位的唯一标识符，用于区分不同的处理槽位
    int n_prompt_tokens_processed; // 此槽位已处理的提示token数量
    int n_decoded;                 // 此槽位已解码(生成)的token数量
    double t_prompt_processing;    // 处理提示所花费的时间(毫秒)
    double t_token_generation;     // 生成所有token所花费的总时间(毫秒)

    // 添加状态枚举
    enum slot_state
    {
        SLOT_STATE_IDLE,       // 空闲状态
        SLOT_STATE_PROCESSING, // 正在处理请
        SLOT_STATE_ERROR       // 错误状态
    } state = SLOT_STATE_IDLE; // 默认为空闲状态

    // 检查是否正在处理
    bool is_processing() const
    {
        return state == SLOT_STATE_PROCESSING;
    }

    // 设置槽位状态
    void set_state(slot_state new_state)
    {
        state = new_state;
    }
};

// 添加服务器指标监控结构体
struct server_metrics
{
    // 基础计时
    int64_t t_start = 0; // 服务启动时间

    // 累计统计
    uint64_t n_prompt_tokens_processed_total = 0; // 处理的prompt token总数
    uint64_t t_prompt_processing_total = 0;       // prompt处理总时间
    uint64_t n_tokens_predicted_total = 0;        // 生成的token总数
    uint64_t t_tokens_generation_total = 0;       // token生成总时间

    // 当前时段统计
    uint64_t n_prompt_tokens_processed = 0; // 当前时段处理的prompt token数
    uint64_t t_prompt_processing = 0;       // 当前时段prompt处理时间
    uint64_t n_tokens_predicted = 0;        // 当前时段生成的token数
    uint64_t t_tokens_generation = 0;       // 当前时段token生成时间

    // 系统负载统计
    uint64_t n_decode_total = 0;     // 解码调用总次数
    uint64_t n_busy_slots_total = 0; // 繁忙槽位累计数
    uint64_t n_requests_total = 0;   // 总请求
    uint64_t n_requests_failed = 0;  // 失败请求数

    // KV缓存统计
    uint64_t kv_cache_tokens_count = 0; // KV缓存中的token数量
    uint64_t kv_cache_used_cells = 0;   // KV缓存使用的单元数

    /**
     * @brief 初始化指标监控
     * 记录服务启动时间
     */
    void init()
    {
        t_start = ggml_time_us(); // 记录启动时间
    }

    /**
     * @brief 记录prompt处理指标
     * @param slot 处理槽位信息
     */
    void on_prompt_eval(const server_slot &slot)
    {
        if (slot.n_prompt_tokens_processed > 0 && slot.t_prompt_processing > 0)
        {
            // 更新当前时段统计
            n_prompt_tokens_processed = slot.n_prompt_tokens_processed;
            t_prompt_processing = slot.t_prompt_processing;

            // 更新总计统计
            n_prompt_tokens_processed_total += slot.n_prompt_tokens_processed;
            t_prompt_processing_total += slot.t_prompt_processing;
        }
    }

    /**
     * @brief 记录token生成指标
     * @param slot 处理槽位信息
     */
    void on_prediction(const server_slot &slot)
    {
        n_tokens_predicted += slot.n_decoded;
        t_tokens_generation += slot.t_token_generation;
        n_tokens_predicted_total += slot.n_decoded;
        t_tokens_generation_total += slot.t_token_generation;
    }

    // 记录系统负载指标
    void on_decoded(const std::vector<server_slot> &slots)
    {
        n_decode_total++;
        for (const auto &slot : slots)
        {
            if (slot.is_processing())
            {
                n_busy_slots_total++;
            }
        }
    }

    // 记录请求指标
    void on_request()
    {
        n_requests_total++;
    }

    void on_request_failed()
    {
        n_requests_failed++;
    }

    // 更新KV缓存指标
    void update_kv_cache_metrics(llama_context *ctx)
    {
        if (ctx)
        {
            kv_cache_tokens_count = llama_get_kv_cache_token_count(ctx);
            kv_cache_used_cells = llama_get_kv_cache_used_cells(ctx);
        }
    }

    // 重置前时段统计
    void reset_bucket()
    {
        printf("[DEBUG] Resetting metrics bucket\n");
        printf("  - Previous prompt tokens: %lu\n", n_prompt_tokens_processed);
        printf("  - Previous prompt time: %.6f ms\n", t_prompt_processing);
        printf("  - Previous prompt speed: %.2f tokens/s\n",
               t_prompt_processing > 0 ? (n_prompt_tokens_processed * 1000.0) / t_prompt_processing : 0);
    }

    // 获取性能指标JSON
    json get_metrics()
    {
        json metrics = {
            {"uptime_seconds", (ggml_time_us() - t_start) / 1e6},

            // 吞吐量指标
            {"tokens_per_second", t_tokens_generation > 0 ? (n_tokens_predicted * 1000.0) / t_tokens_generation : 0.0},

            // 延迟指标
            {"avg_prompt_latency_ms", n_prompt_tokens_processed > 0 ? t_prompt_processing / static_cast<double>(n_prompt_tokens_processed) : 0.0},
            {"avg_generation_latency_ms", n_tokens_predicted > 0 ? static_cast<double>(t_tokens_generation) / n_tokens_predicted : 0.0},

            // 负载指标
            {"busy_slots_ratio", n_decode_total ? static_cast<double>(n_busy_slots_total) / n_decode_total : 0},

            // 请求统计
            {"total_requests", n_requests_total},
            {"failed_requests", n_requests_failed},
            {"success_rate", n_requests_total ? static_cast<double>(n_requests_total - n_requests_failed) / n_requests_total : 0},

            // KV缓存统计
            {"kv_cache_tokens", kv_cache_tokens_count},
            {"kv_cache_used_cells", kv_cache_used_cells},

            // 累计统计
            {"total_prompt_tokens", n_prompt_tokens_processed_total},
            {"total_generated_tokens", n_tokens_predicted_total},
            {"total_decode_calls", n_decode_total}};

        printf("[DEBUG] Metrics response:\n");
        printf("  Tokens processed: %lu\n", n_prompt_tokens_processed);
        printf("  Processing time: %.2f ms\n", t_prompt_processing);

        return metrics;
    }
};

// LLaMA模型管理器类
class LLaMAServer
{
private:
    llama_model *model = nullptr;
    llama_context *ctx = nullptr;
    llama_sampler *smpl = nullptr;
    std::vector<llama_chat_message> messages;
    std::vector<char> formatted;
    int prev_len = 0;
    int client_socket = -1;
    server_metrics metrics;
    server_slot slot;
    std::string active_backend; // 添加后端状态记录

public:
    /**
     * @brief 初始化后端并检查状态
     * @return 是否成功初始化后端
     */
    bool initialize_backends()
    {
        try
        {
            // 加载所有可用的计算后端
            ggml_backend_load_all();

            // 检查后端可用性
            bool has_cuda = llama_backend_has_cuda();
            bool has_metal = llama_backend_has_metal();

            // 打印后端信息
            printf("\n=== Backend Initialization ===\n");
            printf("Available backends:\n");
            printf("- CPU: Always available\n");
            printf("- CUDA: %s\n", has_cuda ? "Available" : "Not available");
            printf("- Metal: %s\n", has_metal ? "Available" : "Not available");

            // 验证至少有基础后端可用
            if (!ggml_backend_is_available())
            {
                fprintf(stderr, "Error: No computation backends available\n");
                return false;
            }

            return true;
        }
        catch (const std::exception &e)
        {
            fprintf(stderr, "Backend initialization failed: %s\n", e.what());
            return false;
        }
    }

    /**
     * @brief 初始化LLaMA模型和相关资源
     * @param model_path 模型文件路
     * @param n_ctx 上下文窗口大小
     * @param n_gpu_layers GPU加速层数
     * @return 初始化是否成功
     */
    bool initialize(const std::string &model_path, int n_ctx = 2048, int n_gpu_layers = 99)
    {
        // 首先初始化后端
        if (!initialize_backends())
        {
            fprintf(stderr, "Failed to initialize computation backends\n");
            return false;
        }

        // 初始化模型参数
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = n_gpu_layers;

        // 加载模型
        model = llama_load_model_from_file(model_path.c_str(), model_params);
        if (!model)
        {
            fprintf(stderr, "Failed to load model: %s\n", model_path.c_str());
            return false;
        }

        // 初始化上下文
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = n_ctx;
        ctx_params.n_batch = n_ctx;

        ctx = llama_new_context_with_model(model, ctx_params);
        if (!ctx)
        {
            fprintf(stderr, "Failed to create context\n");
            llama_free_model(model);
            return false;
        }

        // 记录当前使用的后端
        active_backend = ggml_backend_get_name(llama_get_context_backend(ctx));
        printf("Using backend: %s\n", active_backend.c_str());

        // 初始化采样器
        smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        formatted.resize(llama_n_ctx(ctx));

        slot.id = 0;
        metrics.init();

        return true;
    }

    /**
     * @brief 生成对用户输入的响应
     * @param user_input 用户输入本
     * @return 模型生成的响应文本
     */
    std::string generate_response(const std::string &user_input)
    {
        // 设置槽位状态为处理中
        slot.set_state(server_slot::SLOT_STATE_PROCESSING);

        // 记录开始时间（使用更高精度的时间计算）
        int64_t start_time = ggml_time_us();

        // 将用户输入添加到消息列表
        messages.push_back({"user", strdup(user_input.c_str())});

        // 应用聊天模板
        int new_len = llama_chat_apply_template(model, nullptr, messages.data(), messages.size(), true,
                                                formatted.data(), formatted.size());
        if (new_len > (int)formatted.size())
        {
            formatted.resize(new_len);
            new_len = llama_chat_apply_template(model, nullptr, messages.data(), messages.size(), true,
                                                formatted.data(), formatted.size());
        }
        if (new_len < 0)
        {
            slot.set_state(server_slot::SLOT_STATE_ERROR);
            return "Error: Failed to apply chat template";
        }

        // 获取提示文本
        std::string prompt(formatted.begin() + prev_len, formatted.begin() + new_len);

        // 记录 prompt 处理时间和 token 数量
        int64_t prompt_time = ggml_time_us() - start_time;
        slot.t_prompt_processing = static_cast<double>(prompt_time) / 1000.0;
        slot.n_prompt_tokens_processed = new_len - prev_len;

        printf("[DEBUG] Prompt processing details:\n");
        printf("  Start time: %ld us\n", start_time);
        printf("  End time: %ld us\n", ggml_time_us());
        printf("  Processing time: %ld us (%.2f ms)\n", prompt_time, slot.t_prompt_processing);
        printf("  Tokens processed: %d\n", slot.n_prompt_tokens_processed);

        // 生成回复
        int64_t generation_start = ggml_time_us();
        std::string response = generate(prompt);

        // 记录生成时间
        int64_t generation_time = ggml_time_us() - generation_start;
        slot.t_token_generation = generation_time / 1000.0; // 转换为毫秒

        printf("[DEBUG] Token generation: %d tokens in %.2f ms\n",
               slot.n_decoded, slot.t_token_generation);

        // 更新指标
        metrics.on_prompt_eval(slot);
        metrics.on_prediction(slot);
        metrics.update_kv_cache_metrics(ctx);
        metrics.on_request();

        // 将回复添加到消息列表
        messages.push_back({"assistant", strdup(response.c_str())});
        prev_len = llama_chat_apply_template(model, nullptr, messages.data(), messages.size(), false, nullptr, 0);

        // 处理完成，设置槽位状态为空闲
        slot.set_state(server_slot::SLOT_STATE_IDLE);

        return response;
    }

    void set_client_socket(int socket) { client_socket = socket; }

private:
    /**
     * @brief 核生成数
     * @param prompt 输入提示文本
     * @return 成的文本响应
     * @note 包含token处理、批处理、采样等核心逻辑
     */
    std::string generate(const std::string &prompt)
    {
        std::string response;
        slot.n_decoded = 0; // 重置解码计数

        // 1.分词处理
        // 第一次调用llama_tokenize，用于获取token数量
        const int n_prompt_tokens = -llama_tokenize(model, prompt.c_str(), prompt.size(), NULL, 0, true, true);
        std::vector<llama_token> prompt_tokens(n_prompt_tokens);

        // 第二次调用llama_tokenize，用于获取token
        if (llama_tokenize(model, prompt.c_str(), prompt.size(), prompt_tokens.data(), prompt_tokens.size(),
                           llama_get_kv_cache_used_cells(ctx) == 0, true) < 0)
        {
            // 设置槽位状态为错误
            slot.set_state(server_slot::SLOT_STATE_ERROR);
            if (client_socket != -1)
            {
                send_sse_error(client_socket, "Failed to tokenize prompt");
            }
            return "Error: Failed to tokenize prompt";
        }

        // 2.准备批处理
        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        llama_token new_token_id;

        // 3.生成回复
        while (true)
        {
            // 3.1 检查上下文空间
            int n_ctx = llama_n_ctx(ctx);
            int n_ctx_used = llama_get_kv_cache_used_cells(ctx); // 获取已使用单元数
            if (n_ctx_used + batch.n_tokens > n_ctx)
            {
                if (client_socket != -1)
                {
                    send_sse_error(client_socket, "Context size exceeded");
                }
                return response + "\nContext size exceeded";
            }

            // 3.2 解码处理
            if (llama_decode(ctx, batch))
            {
                // 设置槽位状态为错误
                slot.set_state(server_slot::SLOT_STATE_ERROR);
                if (client_socket != -1)
                {
                    send_sse_error(client_socket, "Failed to decode");
                }
                return response + "\nFailed to decode";
            }

            // 3.3 采样下一个token
            new_token_id = llama_sampler_sample(smpl, ctx, -1);

            // 检查是否结束
            if (llama_token_is_eog(model, new_token_id))
            {
                break;
            }

            // 转换token为文本
            char buf[256];
            int n = llama_token_to_piece(model, new_token_id, buf, sizeof(buf), 0, true);
            if (n < 0)
            {
                if (client_socket != -1)
                {
                    send_sse_error(client_socket, "Failed to convert token to text");
                }
                return response + "\nFailed to convert token to text";
            }

            std::string piece(buf, n);
            response += piece;

            // 如果设置了client_socket，发送SSE消息
            if (client_socket != -1)
            {
                send_sse_message(client_socket, piece);
            }

            // 准备下一个token的批理
            batch = llama_batch_get_one(&new_token_id, 1);

            slot.n_decoded++;                                   // 增加解码计数
            metrics.on_decoded(std::vector<server_slot>{slot}); // 更新解码统计
        }

        // 发送完成信号
        if (client_socket != -1)
        {
            send_sse_done(client_socket);
        }

        return response;
    }

public:
    ~LLaMAServer()
    {
        // 清理资源
        for (auto &msg : messages)
        {
            free(const_cast<char *>(msg.content));
        }
        if (smpl)
        {
            llama_sampler_free(smpl);
        }
        if (ctx)
        {
            llama_free(ctx);
        }
        if (model)
        {
            llama_free_model(model);
        }
    }

    // 添加获取指标的方法
    json get_metrics()
    {
        return metrics.get_metrics();
    }
};

/**
 * @brief HTTP请求处理函数
 * @param client_socket 客户端socket
 * @param request_body 请求体数据
 * @param llama LLaMA服务器实例
 * @note 处理HTTP请求，支持普通请求和流式请求
 */
void handle_http_request(int client_socket, const char *request_body, LLaMAServer &llama)
{
    // 检查是否是metrics请求（GET方法）
    if (strstr(request_body, "GET /metrics") != nullptr)
    {
        handle_metrics_request(client_socket, llama);
        return;
    }

    // 检查是否是 OPTIONS 请求
    if (strstr(request_body, "OPTIONS") != NULL)
    {
        const char *cors_response =
            "HTTP/1.1 200 OK\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
            "Content-Length: 0\r\n"
            "\r\n";
        send(client_socket, cors_response, strlen(cors_response), 0);
        return;
    }

    // 解析JSON请求
    cJSON *request = cJSON_Parse(request_body);
    if (!request)
    {
        std::string error_response = format_error_response("Invalid JSON", ERROR_TYPE_INVALID_REQUEST);
        send(client_socket, error_response.c_str(), error_response.length(), 0);
        return;
    }

    // 获取prompt字段
    cJSON *prompt_item = cJSON_GetObjectItem(request, "prompt");
    if (!cJSON_IsString(prompt_item))
    {
        std::string error_response = format_error_response("Missing prompt field", ERROR_TYPE_INVALID_REQUEST);
        send(client_socket, error_response.c_str(), error_response.length(), 0);
        cJSON_Delete(request);
        return;
    }

    // 检查是否请求流式输出
    cJSON *stream_item = cJSON_GetObjectItem(request, "stream");
    bool use_stream = stream_item && cJSON_IsTrue(stream_item);

    if (use_stream)
    {
        // 发送SSE头
        send_sse_headers(client_socket);

        // 设置client_socket并生成响应
        llama.set_client_socket(client_socket);
        llama.generate_response(prompt_item->valuestring);
        llama.set_client_socket(-1); // 重置socket
    }
    else
    {
        // 生成回复
        std::string response = llama.generate_response(prompt_item->valuestring);

        // 构建JSON响应
        cJSON *json_response = cJSON_CreateObject();
        cJSON_AddStringToObject(json_response, "response", response.c_str());
        char *response_str = cJSON_Print(json_response);

        // 使用统一的成功响应格式
        std::string http_response = format_success_response(response_str);

        // 发送响应
        send(client_socket, http_response.c_str(), http_response.length(), 0);

        free(response_str);
        cJSON_Delete(json_response);
    }

    cJSON_Delete(request);
}

/**
 * @brief SSE(Server-Sent Events)相关工具函数
 */
/**
 * @brief 发送SSE头部信息
 * @param client_socket 客户端socket
 */
void send_sse_headers(int client_socket)
{
    const char *headers =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    send(client_socket, headers, strlen(headers), 0);
}

/**
 * @brief 发送SSE消息
 * @param client_socket 客户端socket
 * @param data 消息内容
 */
void send_sse_message(int client_socket, const std::string &data)
{
    std::string message = "data: " + data + "\n\n";
    send(client_socket, message.c_str(), message.length(), 0);
}

/**
 * @brief 发送SSE错误消息
 * @param client_socket 客户端socket
 * @param error 错误信息
 */
void send_sse_error(int client_socket, const std::string &error)
{
    std::string message = "event: error\ndata: " + error + "\n\n";
    send(client_socket, message.c_str(), message.length(), 0);
}

/**
 * @brief 发送SSE完成消息
 * @param client_socket 客户端socket
 */
void send_sse_done(int client_socket)
{
    const char *message = "data: [DONE]\n\n";
    send(client_socket, message, strlen(message), 0);
}

/**
 * @brief 处理metrics请求
 * @param client_socket 客户端socket
 * @param llama LLaMA服务器实例
 */
void handle_metrics_request(int client_socket, LLaMAServer &llama)
{
    printf("\n=== New Metrics Request ===\n");
    time_t now = time(NULL);
    printf("Time: %s", ctime(&now));

    try
    {
        // 获取指标数据
        json metrics = llama.get_metrics();

        // 构造响应数据
        json response_data = {
            {"status", "success"},
            {"data", metrics}};

        std::string response_str = response_data.dump();
        printf("[DEBUG] Response data: %s\n", response_str.c_str());

        // 构造 HTTP 响应头
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(response_str.length()) + "\r\n"
                                                    "\r\n" +
            response_str;

        // 发送响应
        ssize_t sent = send(client_socket, response.c_str(), response.length(), 0);
        printf("[DEBUG] Sent %zd bytes\n", sent);
    }
    catch (const std::exception &e)
    {
        printf("[ERROR] Failed to get metrics: %s\n", e.what());

        // 构造错误响应
        json error_data = {
            {"status", "error"},
            {"message", e.what()},
            {"code", 500}};

        std::string error_str = error_data.dump();
        std::string response =
            "HTTP/1.1 500 Internal Server Error\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type, Accept\r\n"
            "Connection: close\r\n"
            "Content-Length: " +
            std::to_string(error_str.length()) + "\r\n"
                                                 "\r\n" +
            error_str;

        send(client_socket, response.c_str(), response.length(), 0);
    }
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <model_path> [-c context_size] [-ngl n_gpu_layers]\n", argv[0]);
        return 1;
    }

    std::string model_path;
    int ngl = 99;
    int n_ctx = 2048;

    // 解析命令行参数
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc)
        {
            model_path = argv[++i];
        }
        else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc)
        {
            n_ctx = std::stoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-ngl") == 0 && i + 1 < argc)
        {
            ngl = std::stoi(argv[++i]);
        }
        else if (model_path.empty())
        {
            model_path = argv[i];
        }
    }

    // 初始化LLaMA服务器
    LLaMAServer llama;
    if (!llama.initialize(model_path, n_ctx, ngl))
    {
        fprintf(stderr, "Failed to initialize LLaMA server\n");
        return 1;
    }

    // 创建服务器socket
    int server_fd;
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        return 1;
    }

    // 设置socket选项
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        return 1;
    }

    // 配置服务器地址
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        return 1;
    }

    // 监听连接
    if (listen(server_fd, 3) < 0)
    {
        perror("listen failed");
        return 1;
    }

    printf("Server is running on port %d...\n", PORT);

    // 主循环
    while (1)
    {
        int client_socket;
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        // 接受新连接
        if ((client_socket = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len)) < 0)
        {
            perror("accept failed");
            continue;
        }

        // 设置keep-alive
        int keep_alive = 1;
        setsockopt(client_socket, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive));

        // 接收请求
        char buffer[BUFFER_SIZE] = {0};
        ssize_t bytes_received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received > 0)
        {
            printf("[DEBUG] Received request:\n%s\n", buffer);

            // 检查是否是 GET /metrics 请求
            if (strstr(buffer, "GET /metrics") != NULL)
            {
                handle_metrics_request(client_socket, llama);
            }
            else
            {
                // 查找HTTP请求体
                char *body = strstr(buffer, "\r\n\r\n");
                if (body)
                {
                    body += 4; // 跳过\r\n\r\n
                    handle_http_request(client_socket, body, llama);
                }
            }
        }

        // 对于非流式请求，关闭连接
        if (strstr(buffer, "\"stream\":true") == NULL)
        {
            close(client_socket);
        }
    }

    close(server_fd);
    return 0;
}
