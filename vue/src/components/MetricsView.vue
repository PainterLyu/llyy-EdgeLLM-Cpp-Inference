<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { useRouter } from 'vue-router'
import axios from 'axios'
import { API_URL } from '../config'

const router = useRouter()
const metrics = ref(null)
const updateInterval = ref(null)
const error = ref(null)

// 获取指标数据
const fetchMetrics = async () => {
  try {
    console.log('[DEBUG] Sending metrics request...');
    const response = await axios.get(`${API_URL}/metrics`);
    console.log('[DEBUG] Received metrics:', response.data);
    
    // 检查响应数据是否有效
    if (response.data?.data) {
      metrics.value = {
        ...response.data.data,
        uptime_seconds: response.data.data.uptime_seconds / 3600,
      };
      console.log('[DEBUG] Metrics updated:', metrics.value);
    } else {
      console.warn('[WARN] Invalid metrics response format');
    }
  } catch (err) {
    console.error('[ERROR] Failed to fetch metrics:', err);
    error.value = err.message;
  }
};

// 修改 onMounted 钩子，添加错误重试
onMounted(async () => {
  await fetchMetrics()
  updateInterval.value = setInterval(async () => {
    try {
      await fetchMetrics()
    } catch (err) {
      console.error('Failed to update metrics:', err)
    }
  }, 1000)
})

onUnmounted(() => {
  if (updateInterval.value) {
    clearInterval(updateInterval.value)
  }
})

// 返回主页
const goBack = () => {
  router.push('/')
}
</script>

<template>
  <div class="min-h-screen bg-white dark:bg-gray-900">
    <!-- 顶部导航 -->
    <header class="bg-white dark:bg-gray-800 shadow">
      <div class="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8 h-16 flex items-center justify-between">
        <h1 class="text-xl font-semibold text-gray-900 dark:text-white">系统性能监控</h1>
        <button
          @click="goBack"
          class="px-4 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 transition-colors"
        >
          返回聊天
        </button>
      </div>
    </header>

    <!-- 主要内容 -->
    <main class="max-w-7xl mx-auto py-6 px-4 sm:px-6 lg:px-8">
      <div v-if="error" class="text-red-500 mb-4">{{ error }}</div>
      
      <div v-if="metrics" class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
        <!-- 基础信息卡片 -->
        <div class="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 class="text-lg font-medium text-gray-900 dark:text-white mb-4">基础信息</h2>
          <div class="space-y-2">
            <p class="text-gray-600 dark:text-gray-300">
              运行时间: {{ (metrics.uptime_seconds || 0).toFixed(2) }} 小时
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              总请求数: {{ metrics.total_requests || 0 }}
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              成功率: {{ ((metrics.success_rate || 0) * 100).toFixed(2) }}%
            </p>
          </div>
        </div>

        <!-- 性能指标卡片 -->
        <div class="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 class="text-lg font-medium text-gray-900 dark:text-white mb-4">性能指标</h2>
          <div class="space-y-2">
            <p class="text-gray-600 dark:text-gray-300">
              Token生成速度: {{ (metrics.tokens_per_second || 0).toFixed(2) }} tokens/s
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              平均延迟: {{ (metrics.avg_generation_latency_ms || 0).toFixed(2) }} ms
            </p>
          </div>
        </div>

        <!-- 资源使用卡片 -->
        <div class="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 class="text-lg font-medium text-gray-900 dark:text-white mb-4">资源使用</h2>
          <div class="space-y-2">
            <p class="text-gray-600 dark:text-gray-300">
              KV缓存Token数: {{ metrics.kv_cache_tokens || 0 }}
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              KV缓存使用单元: {{ metrics.kv_cache_used_cells || 0 }}
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              槽位使用率: {{ ((metrics.busy_slots_ratio || 0) * 100).toFixed(2) }}%
            </p>
          </div>
        </div>

        <!-- 累计统计卡片 -->
        <div class="bg-white dark:bg-gray-800 rounded-lg shadow p-6">
          <h2 class="text-lg font-medium text-gray-900 dark:text-white mb-4">累计统计</h2>
          <div class="space-y-2">
            <p class="text-gray-600 dark:text-gray-300">
              总处理Token数: {{ metrics.total_prompt_tokens || 0 }}
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              总生成Token数: {{ metrics.total_generated_tokens || 0 }}
            </p>
            <p class="text-gray-600 dark:text-gray-300">
              总解码调用次数: {{ metrics.total_decode_calls || 0 }}
            </p>
          </div>
        </div>
      </div>
    </main>
  </div>
</template> 