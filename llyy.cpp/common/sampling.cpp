#include "sampling.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "common.h"

// the ring buffer works similarly to std::deque, but with a fixed capacity
// TODO: deduplicate with llama-impl.h
template <typename T> struct ring_buffer {
    ring_buffer(size_t cap) : capacity(cap), data(cap) {}

    T & front() {
        if (sz == 0) {
            throw std::runtime_error("ring buffer is empty");
        }
        return data[first];
    }

    const T & front() const {
        if (sz == 0) {
            throw std::runtime_error("ring buffer is empty");
        }
        return data[first];
    }

    T & back() {
        if (sz == 0) {
            throw std::runtime_error("ring buffer is empty");
        }
        return data[pos];
    }

    const T & back() const {
        if (sz == 0) {
            throw std::runtime_error("ring buffer is empty");
        }
        return data[pos];
    }

    void push_back(const T & value) {
        if (sz == capacity) {
            // advance the start when buffer is full
            first = (first + 1) % capacity;
        } else {
            sz++;
        }
        data[pos] = value;
        pos       = (pos + 1) % capacity;
    }

    T pop_front() {
        if (sz == 0) {
            throw std::runtime_error("ring buffer is empty");
        }
        T value = data[first];
        first   = (first + 1) % capacity;
        sz--;
        return value;
    }

    const T & rat(size_t i) const {
        if (i >= sz) {
            throw std::runtime_error("ring buffer: index out of bounds");
        }
        return data[(first + sz - i - 1) % capacity];
    }

    std::vector<T> to_vector() const {
        std::vector<T> result;
        result.reserve(sz);
        for (size_t i = 0; i < sz; i++) {
            result.push_back(data[(first + i) % capacity]);
        }
        return result;
    }

    void clear() {
        // here only reset the status of the buffer
        sz    = 0;
        first = 0;
        pos   = 0;
    }

    bool empty() const { return sz == 0; }

    size_t size() const { return sz; }

    size_t         capacity = 0;
    size_t         sz       = 0;
    size_t         first    = 0;
    size_t         pos      = 0;
    std::vector<T> data;
};

struct common_sampler {
    common_params_sampling params;

    struct llama_sampler * grmr;
    struct llama_sampler * chain;

    ring_buffer<llama_token> prev;

    std::vector<llama_token_data> cur;

    llama_token_data_array cur_p;

    void set_logits(struct llama_context * ctx, int idx) {
        const auto * logits = llama_get_logits_ith(ctx, idx);

        const llama_model * model = llama_get_model(ctx);
        const llama_vocab * vocab = llama_model_get_vocab(model);

        const int n_vocab = llama_vocab_n_tokens(vocab);

        cur.resize(n_vocab);

        for (llama_token token_id = 0; token_id < n_vocab; token_id++) {
            cur[token_id] = llama_token_data{ token_id, logits[token_id], 0.0f };
        }

        cur_p = { cur.data(), cur.size(), -1, false };
    }
};

std::string common_params_sampling::print() const {
    char result[1024];

    snprintf(result, sizeof(result),
             "\trepeat_last_n = %d, repeat_penalty = %.3f, frequency_penalty = %.3f, presence_penalty = %.3f\n"
             "\tdry_multiplier = %.3f, dry_base = %.3f, dry_allowed_length = %d, dry_penalty_last_n = %d\n"
             "\ttop_k = %d, top_p = %.3f, min_p = %.3f, xtc_probability = %.3f, xtc_threshold = %.3f, typical_p = "
             "%.3f, top_n_sigma = %.3f, temp = %.3f\n"
             "\tmirostat = %d, mirostat_lr = %.3f, mirostat_ent = %.3f",
             penalty_last_n, penalty_repeat, penalty_freq, penalty_present, dry_multiplier, dry_base,
             dry_allowed_length, dry_penalty_last_n, top_k, top_p, min_p, xtc_probability, xtc_threshold, typ_p,
             top_n_sigma, temp, mirostat, mirostat_eta, mirostat_tau);

    return std::string(result);
}

struct common_sampler * common_sampler_init(const struct llama_model *            model,
                                            const struct common_params_sampling & params) {
    // 获取模型的词汇表
    const llama_vocab * vocab = llama_model_get_vocab(model);

    // 初始化采样器链参数，设置默认值
    llama_sampler_chain_params lparams = llama_sampler_chain_default_params();

    // 设置是否禁用性能统计
    lparams.no_perf = params.no_perf;

    // 声明语法采样器指针
    struct llama_sampler * grmr;

    // 判断是否使用llguidance语法
    if (params.grammar.compare(0, 11, "%llguidance") == 0) {
#ifdef LLAMA_USE_LLGUIDANCE
        // 如果启用了llguidance，则初始化llguidance采样器
        grmr = llama_sampler_init_llg(vocab, "lark", params.grammar.c_str());
#else
        // 如果未启用llguidance，则中止程序
        GGML_ABORT("llguidance (cmake -DLLAMA_LLGUIDANCE=ON) is not enabled");
#endif  // LLAMA_USE_LLGUIDANCE
    } else {
        // 使用常规语法解析
        // 创建存储不同类型模式的容器
        std::vector<std::string> patterns_at_start;  // 在文本开始处匹配的模式
        std::vector<std::string> patterns_anywhere;  // 在文本任意位置匹配的模式
        std::vector<llama_token> trigger_tokens;     // 触发语法检查的特定token

        // 处理所有的语法触发器
        for (const auto & trigger : params.grammar_triggers) {
            switch (trigger.type) {
                case COMMON_GRAMMAR_TRIGGER_TYPE_WORD:
                    {
                        // 如果是单词类型的触发器，则将其转义为正则表达式并添加到任意位置匹配模式中
                        const auto & word = trigger.value;
                        patterns_anywhere.push_back(regex_escape(word));
                        break;
                    }
                case COMMON_GRAMMAR_TRIGGER_TYPE_PATTERN:
                case COMMON_GRAMMAR_TRIGGER_TYPE_PATTERN_START:
                    {
                        // 根据模式类型，将模式添加到相应的容器中
                        const auto & pattern = trigger.value;
                        (trigger.type == COMMON_GRAMMAR_TRIGGER_TYPE_PATTERN_START ? patterns_at_start :
                                                                                     patterns_anywhere)
                            .push_back(pattern);
                        break;
                    }
                case COMMON_GRAMMAR_TRIGGER_TYPE_TOKEN:
                    {
                        // 如果是token类型的触发器，则将其添加到token列表中
                        const auto token = trigger.token;
                        trigger_tokens.push_back(token);
                        break;
                    }
                default:
                    // 未知的触发器类型，断言失败
                    GGML_ASSERT(false && "unknown trigger type");
            }
        }

        // 构建触发模式的正则表达式
        std::vector<std::string> trigger_patterns;
        if (!patterns_at_start.empty()) {
            // 创建匹配开头模式的正则表达式：以某些模式开始，后跟任意字符
            trigger_patterns.push_back("^(" + string_join(patterns_at_start, "|") + ")[\\s\\S]*");
        }
        if (!patterns_anywhere.empty()) {
            // 创建匹配任意位置模式的正则表达式：任意字符后跟某些模式，再跟任意字符
            trigger_patterns.push_back("^[\\s\\S]*?(" + string_join(patterns_anywhere, "|") + ")[\\s\\S]*");
        }

        // 将C++字符串转换为C风格字符串数组，用于语法初始化
        std::vector<const char *> trigger_patterns_c;
        trigger_patterns_c.reserve(trigger_patterns.size());
        for (const auto & regex : trigger_patterns) {
            trigger_patterns_c.push_back(regex.c_str());
        }

        // 根据是否使用懒加载模式初始化语法采样器
        grmr = params.grammar_lazy ? llama_sampler_init_grammar_lazy_patterns(
                                         vocab, params.grammar.c_str(), "root", trigger_patterns_c.data(),
                                         trigger_patterns_c.size(), trigger_tokens.data(), trigger_tokens.size()) :
                                     llama_sampler_init_grammar(vocab, params.grammar.c_str(), "root");
        // 检查语法采样器是否初始化成功
        if (!grmr) {
            return nullptr;
        }
    }

    // 创建common_sampler结构体并初始化成员
    auto * result = new common_sampler{
        /* .params = */ params,                                                 // 采样参数
        /* .grmr   = */ grmr,                                                   // 语法采样器
        /* .chain  = */ llama_sampler_chain_init(lparams),                      // 初始化采样器链
        /* .prev   = */ ring_buffer<llama_token>(std::max(32, params.n_prev)),  // 初始化token历史环形缓冲区
        /* .cur    = */ {},                                                     // 当前token数据容器
        /* .cur_p  = */ {},                                                     // 当前token数据数组
    };

    // 添加logit偏置采样器到采样器链
    llama_sampler_chain_add(
        result->chain,
        llama_sampler_init_logit_bias(llama_vocab_n_tokens(vocab), params.logit_bias.size(), params.logit_bias.data()));

    // 根据mirostat参数配置不同的采样策略
    if (params.mirostat == 0) {
        // 不使用mirostat算法
        if (params.top_n_sigma >= 0) {
            // 如果指定了top_n_sigma，使用固定的top_k、温度和top_n_sigma采样
            llama_sampler_chain_add(result->chain, llama_sampler_init_top_k(params.top_k));
            llama_sampler_chain_add(result->chain, llama_sampler_init_temp(params.temp));
            llama_sampler_chain_add(result->chain, llama_sampler_init_top_n_sigma(params.top_n_sigma));
        } else {
            // 根据指定的采样器列表添加采样器
            for (const auto & cnstr : params.samplers) {
                switch (cnstr) {
                    case COMMON_SAMPLER_TYPE_DRY:
                        {
                            // 处理干预(dry)采样器，将序列中断器转换为C风格字符串数组
                            std::vector<const char *> c_breakers;
                            c_breakers.reserve(params.dry_sequence_breakers.size());
                            for (const auto & str : params.dry_sequence_breakers) {
                                c_breakers.push_back(str.c_str());
                            }

                            // 添加干预采样器
                            llama_sampler_chain_add(
                                result->chain, llama_sampler_init_dry(
                                                   vocab, llama_model_n_ctx_train(model), params.dry_multiplier,
                                                   params.dry_base, params.dry_allowed_length,
                                                   params.dry_penalty_last_n, c_breakers.data(), c_breakers.size()));
                        }
                        break;
                    case COMMON_SAMPLER_TYPE_TOP_K:
                        // 添加top-k采样器，保留概率最高的k个token
                        llama_sampler_chain_add(result->chain, llama_sampler_init_top_k(params.top_k));
                        break;
                    case COMMON_SAMPLER_TYPE_TOP_P:
                        // 添加top-p (nucleus)采样器，保留累积概率达到p的token
                        llama_sampler_chain_add(result->chain, llama_sampler_init_top_p(params.top_p, params.min_keep));
                        break;
                    case COMMON_SAMPLER_TYPE_MIN_P:
                        // 添加min-p采样器，排除概率低于最高概率乘以min_p的token
                        llama_sampler_chain_add(result->chain, llama_sampler_init_min_p(params.min_p, params.min_keep));
                        break;
                    case COMMON_SAMPLER_TYPE_XTC:
                        // 添加XTC采样器(扩展模板控制)
                        llama_sampler_chain_add(result->chain,
                                                llama_sampler_init_xtc(params.xtc_probability, params.xtc_threshold,
                                                                       params.min_keep, params.seed));
                        break;
                    case COMMON_SAMPLER_TYPE_TYPICAL_P:
                        // 添加typical-p采样器，偏向选择具有平均log概率的token
                        llama_sampler_chain_add(result->chain,
                                                llama_sampler_init_typical(params.typ_p, params.min_keep));
                        break;
                    case COMMON_SAMPLER_TYPE_TEMPERATURE:
                        // 添加温度采样器，调整logits的分布
                        llama_sampler_chain_add(
                            result->chain,
                            llama_sampler_init_temp_ext(params.temp, params.dynatemp_range, params.dynatemp_exponent));
                        break;
                    case COMMON_SAMPLER_TYPE_INFILL:
                        // 添加填充采样器，用于文本填充任务
                        llama_sampler_chain_add(result->chain, llama_sampler_init_infill(vocab));
                        break;
                    case COMMON_SAMPLER_TYPE_PENALTIES:
                        // 添加惩罚采样器，用于惩罚重复、频繁和已存在的token
                        llama_sampler_chain_add(
                            result->chain, llama_sampler_init_penalties(params.penalty_last_n, params.penalty_repeat,
                                                                        params.penalty_freq, params.penalty_present));
                        break;
                    default:
                        // 未知的采样器类型，断言失败
                        GGML_ASSERT(false && "unknown sampler type");
                }
            }
        }
        // 最后添加分布采样器，根据修改后的概率分布进行采样
        llama_sampler_chain_add(result->chain, llama_sampler_init_dist(params.seed));
    } else if (params.mirostat == 1) {
        // 使用mirostat v1算法
        llama_sampler_chain_add(result->chain, llama_sampler_init_temp(params.temp));
        llama_sampler_chain_add(
            result->chain, llama_sampler_init_mirostat(llama_vocab_n_tokens(vocab), params.seed, params.mirostat_tau,
                                                       params.mirostat_eta, 100));
    } else if (params.mirostat == 2) {
        // 使用mirostat v2算法
        llama_sampler_chain_add(result->chain, llama_sampler_init_temp(params.temp));
        llama_sampler_chain_add(result->chain,
                                llama_sampler_init_mirostat_v2(params.seed, params.mirostat_tau, params.mirostat_eta));
    } else {
        // 无效的mirostat版本，断言失败
        GGML_ASSERT(false && "unknown mirostat version");
    }

    // 返回初始化好的采样器
    return result;
}

void common_sampler_free(struct common_sampler * gsmpl) {
    if (gsmpl) {
        llama_sampler_free(gsmpl->grmr);

        llama_sampler_free(gsmpl->chain);

        delete gsmpl;
    }
}

void common_sampler_accept(struct common_sampler * gsmpl, llama_token token, bool accept_grammar) {
    if (accept_grammar) {
        llama_sampler_accept(gsmpl->grmr, token);
    }

    llama_sampler_accept(gsmpl->chain, token);

    gsmpl->prev.push_back(token);
}

void common_sampler_reset(struct common_sampler * gsmpl) {
    llama_sampler_reset(gsmpl->grmr);

    llama_sampler_reset(gsmpl->chain);
}

struct common_sampler * common_sampler_clone(common_sampler * gsmpl) {
    return new common_sampler{
        /* .params = */ gsmpl->params,
        /* .grmr   = */ llama_sampler_clone(gsmpl->grmr),
        /* .chain  = */ llama_sampler_clone(gsmpl->chain),
        /* .prev   = */ gsmpl->prev,
        /* .cur    = */ gsmpl->cur,
        /* .cur_p  = */ gsmpl->cur_p,
    };
}

void common_perf_print(const struct llama_context * ctx, const struct common_sampler * gsmpl) {
    // TODO: measure grammar performance

    if (gsmpl) {
        llama_perf_sampler_print(gsmpl->chain);
    }
    if (ctx) {
        llama_perf_context_print(ctx);
    }
}

llama_token common_sampler_sample(struct common_sampler * gsmpl, struct llama_context * ctx, int idx,
                                  bool grammar_first) {
    gsmpl->set_logits(ctx, idx);

    auto & grmr  = gsmpl->grmr;
    auto & chain = gsmpl->chain;
    auto & cur_p = gsmpl->cur_p;  // initialized by set_logits

    if (grammar_first) {
        llama_sampler_apply(grmr, &cur_p);
    }

    llama_sampler_apply(chain, &cur_p);

    GGML_ASSERT(cur_p.selected != -1 && "no selected token during sampling - check your sampling configuration");

    const llama_token id = cur_p.data[cur_p.selected].id;

    if (grammar_first) {
        return id;
    }

    // check if it the sampled token fits the grammar
    {
        llama_token_data       single_token_data       = { id, 1.0f, 0.0f };
        llama_token_data_array single_token_data_array = { &single_token_data, 1, -1, false };

        llama_sampler_apply(grmr, &single_token_data_array);

        const bool is_valid = single_token_data_array.data[0].logit != -INFINITY;
        if (is_valid) {
            return id;
        }
    }

    // resampling:
    // if the token is not valid, sample again, but first apply the grammar sampler and then the sampling chain
    gsmpl->set_logits(ctx, idx);

    llama_sampler_apply(grmr, &cur_p);
    llama_sampler_apply(chain, &cur_p);

    GGML_ASSERT(cur_p.selected != -1 && "no selected token during re-sampling - check your sampling configuration");

    return cur_p.data[cur_p.selected].id;
}

std::vector<llama_token> common_sampler_sample_and_accept_n(struct common_sampler * gsmpl, struct llama_context * ctx,
                                                            const std::vector<int> & idxs, const llama_tokens & draft,
                                                            bool grammar_first) {
    GGML_ASSERT(idxs.size() == draft.size() + 1 && "idxs.size() must be draft.size() + 1");

    std::vector<llama_token> result;
    result.reserve(idxs.size());

    size_t i = 0;
    for (; i < draft.size(); i++) {
        const llama_token id = common_sampler_sample(gsmpl, ctx, idxs[i], grammar_first);

        common_sampler_accept(gsmpl, id, true);

        result.push_back(id);

        if (draft[i] != id) {
            break;
        }
    }

    if (i == draft.size()) {
        const llama_token id = common_sampler_sample(gsmpl, ctx, idxs[i], grammar_first);

        common_sampler_accept(gsmpl, id, true);

        result.push_back(id);
    }

    return result;
}

std::vector<llama_token> common_sampler_sample_and_accept_n(struct common_sampler * gsmpl, struct llama_context * ctx,
                                                            const llama_tokens & draft, bool grammar_first) {
    std::vector<int> idxs(draft.size() + 1);
    for (size_t i = 0; i < idxs.size(); ++i) {
        idxs[i] = i;
    }

    return common_sampler_sample_and_accept_n(gsmpl, ctx, idxs, draft, grammar_first);
}

uint32_t common_sampler_get_seed(const struct common_sampler * gsmpl) {
    return llama_sampler_get_seed(gsmpl->chain);
}

// helpers

llama_token_data_array * common_sampler_get_candidates(struct common_sampler * gsmpl) {
    return &gsmpl->cur_p;
}

llama_token common_sampler_last(const struct common_sampler * gsmpl) {
    return gsmpl->prev.rat(0);
}

std::string common_sampler_print(const struct common_sampler * gsmpl) {
    std::string result = "logits ";

    for (int i = 0; i < llama_sampler_chain_n(gsmpl->chain); i++) {
        const auto * smpl = llama_sampler_chain_get(gsmpl->chain, i);
        result += std::string("-> ") + llama_sampler_name(smpl) + " ";
    }

    return result;
}

std::string common_sampler_prev_str(common_sampler * gsmpl, llama_context * ctx_main, int n) {
    n = std::min(n, (int) gsmpl->prev.size());

    if (n <= 0) {
        return "";
    }

    std::string result;
    result.reserve(8 * n);  // 8 is the average length of a token [citation needed], TODO: compute this from the vocab

    for (int i = n - 1; i >= 0; i--) {
        const llama_token id = gsmpl->prev.rat(i);

        GGML_ASSERT(id != LLAMA_TOKEN_NULL && "null token in the sampling history - should not happen");

        result += common_token_to_piece(ctx_main, id);
    }

    return result;
}

char common_sampler_type_to_chr(enum common_sampler_type cnstr) {
    switch (cnstr) {
        case COMMON_SAMPLER_TYPE_DRY:
            return 'd';
        case COMMON_SAMPLER_TYPE_TOP_K:
            return 'k';
        case COMMON_SAMPLER_TYPE_TYPICAL_P:
            return 'y';
        case COMMON_SAMPLER_TYPE_TOP_P:
            return 'p';
        case COMMON_SAMPLER_TYPE_MIN_P:
            return 'm';
        case COMMON_SAMPLER_TYPE_TEMPERATURE:
            return 't';
        case COMMON_SAMPLER_TYPE_XTC:
            return 'x';
        case COMMON_SAMPLER_TYPE_INFILL:
            return 'i';
        case COMMON_SAMPLER_TYPE_PENALTIES:
            return 'e';
        default:
            return '?';
    }
}

std::string common_sampler_type_to_str(enum common_sampler_type cnstr) {
    switch (cnstr) {
        case COMMON_SAMPLER_TYPE_DRY:
            return "dry";
        case COMMON_SAMPLER_TYPE_TOP_K:
            return "top_k";
        case COMMON_SAMPLER_TYPE_TYPICAL_P:
            return "typ_p";
        case COMMON_SAMPLER_TYPE_TOP_P:
            return "top_p";
        case COMMON_SAMPLER_TYPE_MIN_P:
            return "min_p";
        case COMMON_SAMPLER_TYPE_TEMPERATURE:
            return "temperature";
        case COMMON_SAMPLER_TYPE_XTC:
            return "xtc";
        case COMMON_SAMPLER_TYPE_INFILL:
            return "infill";
        case COMMON_SAMPLER_TYPE_PENALTIES:
            return "penalties";
        default:
            return "";
    }
}

std::vector<common_sampler_type> common_sampler_types_from_names(const std::vector<std::string> & names,
                                                                 bool                             allow_alt_names) {
    std::unordered_map<std::string, common_sampler_type> sampler_canonical_name_map{
        { "dry",         COMMON_SAMPLER_TYPE_DRY         },
        { "top_k",       COMMON_SAMPLER_TYPE_TOP_K       },
        { "top_p",       COMMON_SAMPLER_TYPE_TOP_P       },
        { "typ_p",       COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { "min_p",       COMMON_SAMPLER_TYPE_MIN_P       },
        { "temperature", COMMON_SAMPLER_TYPE_TEMPERATURE },
        { "xtc",         COMMON_SAMPLER_TYPE_XTC         },
        { "infill",      COMMON_SAMPLER_TYPE_INFILL      },
        { "penalties",   COMMON_SAMPLER_TYPE_PENALTIES   },
    };

    // since samplers names are written multiple ways
    // make it ready for both system names and input names
    std::unordered_map<std::string, common_sampler_type> sampler_alt_name_map{
        { "top-k",     COMMON_SAMPLER_TYPE_TOP_K       },
        { "top-p",     COMMON_SAMPLER_TYPE_TOP_P       },
        { "nucleus",   COMMON_SAMPLER_TYPE_TOP_P       },
        { "typical-p", COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { "typical",   COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { "typ-p",     COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { "typ",       COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { "min-p",     COMMON_SAMPLER_TYPE_MIN_P       },
        { "temp",      COMMON_SAMPLER_TYPE_TEMPERATURE },
    };

    std::vector<common_sampler_type> samplers;
    samplers.reserve(names.size());

    for (const auto & name : names) {
        auto sampler = sampler_canonical_name_map.find(name);
        if (sampler != sampler_canonical_name_map.end()) {
            samplers.push_back(sampler->second);
        } else {
            if (allow_alt_names) {
                sampler = sampler_alt_name_map.find(name);
                if (sampler != sampler_alt_name_map.end()) {
                    samplers.push_back(sampler->second);
                }
            }
        }
    }

    return samplers;
}

std::vector<common_sampler_type> common_sampler_types_from_chars(const std::string & chars) {
    std::unordered_map<char, common_sampler_type> sampler_name_map = {
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_DRY),         COMMON_SAMPLER_TYPE_DRY         },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_TOP_K),       COMMON_SAMPLER_TYPE_TOP_K       },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_TYPICAL_P),   COMMON_SAMPLER_TYPE_TYPICAL_P   },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_TOP_P),       COMMON_SAMPLER_TYPE_TOP_P       },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_MIN_P),       COMMON_SAMPLER_TYPE_MIN_P       },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_TEMPERATURE), COMMON_SAMPLER_TYPE_TEMPERATURE },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_XTC),         COMMON_SAMPLER_TYPE_XTC         },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_INFILL),      COMMON_SAMPLER_TYPE_INFILL      },
        { common_sampler_type_to_chr(COMMON_SAMPLER_TYPE_PENALTIES),   COMMON_SAMPLER_TYPE_PENALTIES   },
    };

    std::vector<common_sampler_type> samplers;
    samplers.reserve(chars.size());

    for (const auto & c : chars) {
        const auto sampler = sampler_name_map.find(c);
        if (sampler != sampler_name_map.end()) {
            samplers.push_back(sampler->second);
        }
    }

    return samplers;
}
