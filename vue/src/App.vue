<script setup>
import { ref, onMounted } from 'vue'

const isDarkMode = ref(false)

const initTheme = () => {
  const savedTheme = localStorage.getItem('theme')
  if (savedTheme) {
    isDarkMode.value = savedTheme === 'dark'
    document.documentElement.classList.toggle('dark', isDarkMode.value)
  } else if (window.matchMedia('(prefers-color-scheme: dark)').matches) {
    isDarkMode.value = true
    document.documentElement.classList.add('dark')
    localStorage.setItem('theme', 'dark')
  }
}

const toggleDarkMode = () => {
  isDarkMode.value = !isDarkMode.value
  document.documentElement.classList.toggle('dark')
  localStorage.setItem('theme', isDarkMode.value ? 'dark' : 'light')
}

onMounted(() => {
  initTheme()
})
</script>

<template>
  <div class="min-h-screen bg-white dark:bg-gray-900">
    <!-- 顶部导航栏 -->
    <header class="fixed top-0 right-0 z-50 p-4 flex items-center gap-3">
      <button
        @click="toggleDarkMode"
        class="p-2.5 rounded-lg 
                bg-gray-100/80 dark:bg-gray-800/80 
                hover:bg-gray-200/90 dark:hover:bg-gray-700/90 
                text-gray-600 dark:text-gray-300
                backdrop-blur-sm
                border border-gray-200/50 dark:border-gray-700/50
                transition-all duration-300"
        title="切换主题"
      >
        <svg v-if="!isDarkMode" xmlns="http://www.w3.org/2000/svg" class="h-5 w-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M20.354 15.354A9 9 0 018.646 3.646 9.003 9.003 0 0012 21a9.003 9.003 0 008.354-5.646z" />
        </svg>
        <svg v-else xmlns="http://www.w3.org/2000/svg" class="h-5 w-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
        </svg>
      </button>
      <router-link
        to="/metrics"
        class="p-2.5 rounded-lg 
                bg-gray-100/80 dark:bg-gray-800/80 
                hover:bg-gray-200/90 dark:hover:bg-gray-700/90 
                text-gray-600 dark:text-gray-300
                backdrop-blur-sm
                border border-gray-200/50 dark:border-gray-700/50
                transition-all duration-300
                flex items-center justify-center"
        title="性能监控"
      >
        <svg xmlns="http://www.w3.org/2000/svg" class="h-5 w-5" fill="none" viewBox="0 0 24 24" stroke="currentColor">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
        </svg>
      </router-link>
    </header>

    <!-- 路由视图 -->
    <router-view></router-view>
  </div>
</template>

<style>
/* 保留原有的样式 */
</style>