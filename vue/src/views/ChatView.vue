<script setup>
import { ref, onMounted, watch, nextTick } from 'vue'
import { useChatStore } from '../stores/chat'
import { storeToRefs } from 'pinia'
import MarkdownIt from 'markdown-it'
import hljs from 'highlight.js'
import 'highlight.js/styles/github-dark.css'

const chatStore = useChatStore()
const { currentMessages: messages, isLoading, error, conversations } = storeToRefs(chatStore)
const inputMessage = ref('')
const chatContainer = ref(null)
const isSidebarOpen = ref(true)
const editingId = ref(null)
const editingName = ref('')
const editInput = ref(null)

const md = new MarkdownIt({
  highlight: function (str, lang) {
    if (lang && hljs.getLanguage(lang)) {
      try {
        return hljs.highlight(str, { language: lang }).value
      } catch (__) {}
    }
    return ''
  },
  breaks: true,
  linkify: true
})

const sendMessage = async () => {
  if (!inputMessage.value.trim()) return
  
  const messageContent = inputMessage.value
  inputMessage.value = ''
  
  await chatStore.sendMessage(messageContent)
  scrollToBottom()
}

const scrollToBottom = () => {
  if (chatContainer.value) {
    setTimeout(() => {
      chatContainer.value.scrollTop = chatContainer.value.scrollHeight
    }, 100)
  }
}

const formatTime = (timestamp) => {
  return new Date(timestamp).toLocaleTimeString('zh-CN', {
    hour: '2-digit',
    minute: '2-digit'
  })
}

const shouldShowTime = (currentMsg, prevMsg) => {
  if (!prevMsg) return true
  
  const currentTime = new Date(currentMsg.timestamp)
  const prevTime = new Date(prevMsg.timestamp)
  
  return (currentTime - prevTime) > 10 * 60 * 1000
}

const formatMessageTime = (timestamp) => {
  const now = new Date()
  const msgTime = new Date(timestamp)
  
  if (now.toDateString() === msgTime.toDateString()) {
    return msgTime.toLocaleTimeString('zh-CN', {
      hour: '2-digit',
      minute: '2-digit'
    })
  }
  
  const yesterday = new Date(now)
  yesterday.setDate(yesterday.getDate() - 1)
  if (yesterday.toDateString() === msgTime.toDateString()) {
    return '昨天 ' + msgTime.toLocaleTimeString('zh-CN', {
      hour: '2-digit',
      minute: '2-digit'
    })
  }
  
  return msgTime.toLocaleDateString('zh-CN', {
    year: 'numeric',
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit'
  })
}

const toggleSidebar = () => {
  isSidebarOpen.value = !isSidebarOpen.value
}

watch(messages, () => {
  scrollToBottom()
}, { deep: true })

onMounted(() => {
  scrollToBottom()
})

const startEditing = (conversation) => {
  editingId.value = conversation.id
  editingName.value = conversation.name
  nextTick(() => {
    if (editInput.value) {
      editInput.value.focus()
    }
  })
}

const saveConversationName = (id) => {
  if (editingName.value.trim()) {
    chatStore.updateConversationName(id, editingName.value)
  }
  editingId.value = null
  editingName.value = ''
}
</script>

<template>
  <div class="h-screen flex dark:bg-gray-900 transition-colors duration-300">
    <!-- 遮罩层 -->
    <div
      v-if="isSidebarOpen"
      class="fixed inset-0 bg-black/30 backdrop-blur-sm z-20 transition-opacity duration-300 ease-in-out"
      @click="toggleSidebar"
    ></div>

    <!-- 侧边栏 -->
    <div 
      :class="[
        'fixed w-[260px] bg-gray-50 dark:bg-gray-900',
        'border-r border-gray-200 dark:border-gray-700',
        'transform transition-all duration-300 ease-in-out z-30',
        'h-full overflow-hidden',
        isSidebarOpen ? 'translate-x-0 shadow-lg' : '-translate-x-full'
      ]"
    >
      <div class="flex flex-col h-full">
        <!-- 侧边栏头部 -->
        <div class="h-16 flex items-center px-4 border-b border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800">
          <div class="flex flex-col">
            <h1 class="text-xl font-semibold text-gray-800 dark:text-white">AI Chat</h1>
          </div>
        </div>
        <!-- 侧边栏内容区域 -->
        <div class="flex-1 overflow-y-auto">
          <div class="p-4">
            <button
              @click="chatStore.createNewConversation"
              class="w-full flex items-center justify-center gap-2 px-4 py-2 rounded-lg
                     border border-gray-300 dark:border-gray-600 
                     text-gray-700 dark:text-gray-200
                     hover:bg-gray-100 dark:hover:bg-gray-800
                     transition-colors duration-200"
            >
              <svg xmlns="http://www.w3.org/2000/svg" 
                   class="h-5 w-5 text-gray-600 dark:text-gray-300" 
                   viewBox="0 0 20 20" 
                   fill="currentColor"
              >
                <path fill-rule="evenodd" d="M10 3a1 1 0 011 1v5h5a1 1 0 110 2h-5v5a1 1 0 11-2 0v-5H4a1 1 0 110-2h5V4a1 1 0 011-1z" clip-rule="evenodd" />
              </svg>
              新对话
            </button>
          </div>
          <!-- 对话列表 -->
          <div class="mt-2 space-y-1 px-2">
            <div
              v-for="conv in conversations"
              :key="conv.id"
              class="relative group"
            >
              <div
                :class="[
                  'p-3 rounded-lg cursor-pointer transition-all duration-200',
                  'hover:pr-20',
                  conv.id === chatStore.currentConversationId
                    ? 'bg-gray-200 dark:bg-gray-800'
                    : 'hover:bg-gray-100 dark:hover:bg-gray-800'
                ]"
                @click="chatStore.switchConversation(conv.id)"
              >
                <div class="text-sm font-medium text-gray-900 dark:text-white truncate">
                  <input
                    v-if="editingId === conv.id"
                    v-model="editingName"
                    @blur="saveConversationName(conv.id)"
                    @keyup.enter="saveConversationName(conv.id)"
                    class="w-full px-2 py-1
                          bg-gray-50 dark:bg-gray-700
                          border-0 
                          rounded-md
                          focus:outline-none focus:ring-1
                          focus:ring-blue-400 dark:focus:ring-blue-500
                          text-gray-900 dark:text-white
                          placeholder-gray-400 dark:placeholder-gray-500
                          transition-all duration-200"
                    ref="editInput"
                    @click.stop
                    placeholder="输入对话名称"
                  />
                  <span v-else class="block py-1">{{ conv.name }}</span>
                </div>
                <div class="text-xs text-gray-500 dark:text-gray-400 mt-1">
                  {{ new Date(conv.timestamp).toLocaleString() }}
                </div>
                <!-- 操作按钮 -->
                <div
                  class="absolute right-2 top-1/2 -translate-y-1/2 hidden group-hover:flex gap-2"
                  @click.stop
                >
                  <button
                    @click="startEditing(conv)"
                    class="p-1.5 rounded-full
                          text-gray-500 hover:text-blue-500 
                          dark:text-gray-400 dark:hover:text-blue-400
                          hover:bg-gray-100 dark:hover:bg-gray-700
                          transition-all duration-200"
                    title="编辑对话名称"
                  >
                    <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 20 20" fill="currentColor">
                      <path d="M13.586 3.586a2 2 0 112.828 2.828l-.793.793-2.828-2.828.793-.793zM11.379 5.793L3 14.172V17h2.828l8.38-8.379-2.83-2.828z" />
                    </svg>
                  </button>
                  <button
                    @click="chatStore.deleteConversation(conv.id)"
                    class="p-1.5 rounded-full
                          text-gray-500 hover:text-red-500
                          dark:text-gray-400 dark:hover:text-red-400
                          hover:bg-gray-100 dark:hover:bg-gray-700
                          transition-all duration-200"
                    title="删除对话"
                  >
                    <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 20 20" fill="currentColor">
                      <path fill-rule="evenodd" d="M9 2a1 1 0 00-.894.553L7.382 4H4a1 1 0 000 2v10a2 2 0 002 2h8a2 2 0 002-2V6a1 1 0 100-2h-3.382l-.724-1.447A1 1 0 0011 2H9zM7 8a1 1 0 012 0v6a1 1 0 11-2 0V8zm5-1a1 1 0 00-1 1v6a1 1 0 102 0V8a1 1 0 00-1-1z" clip-rule="evenodd" />
                    </svg>
                  </button>
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 主聊天区域 -->
    <div 
      :class="[
        'flex-1 flex flex-col transition-all duration-300 ease-in-out',
        isSidebarOpen ? 'ml-[260px]' : 'ml-0'
      ]"
    >
      <!-- 顶部导航 -->
      <header class="h-16 flex items-center justify-between px-4 border-b border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800">
        <div class="flex items-center gap-2">
          <button 
            class="p-2 rounded-lg hover:bg-gray-100 dark:hover:bg-gray-800 text-gray-600 dark:text-gray-200"
            @click="toggleSidebar"
          >
            <svg 
              xmlns="http://www.w3.org/2000/svg" 
              class="h-6 w-6 transition-transform duration-300"
              :class="{ 'rotate-180': !isSidebarOpen }"
              fill="none" 
              viewBox="0 0 24 24" 
              stroke="currentColor"
              stroke-width="2"
            >
              <path 
                stroke-linecap="round" 
                stroke-linejoin="round" 
                d="M4 6h16M4 12h16M4 18h16"
              />
            </svg>
          </button>
          <div class="flex flex-col">
            <h2 class="text-lg font-medium dark:text-white">Qwen 2.5</h2>
            <p class="text-xs text-gray-500 dark:text-gray-400">0.5B Instruct</p>
          </div>
        </div>
      </header>

      <!-- 消息列表 -->
      <div
        ref="chatContainer"
        class="flex-1 overflow-y-auto bg-white dark:bg-gray-800"
      >
        <div
          class="max-w-4xl mx-auto py-6"
        >
          <div
            v-for="(message, index) in messages"
            :key="index"
            class="mb-6 px-4 md:px-12 lg:px-24"
          >
            <!-- 时间显示 -->
            <div
              v-if="shouldShowTime(message, messages[index - 1])"
              class="flex justify-center my-6"
            >
              <span class="px-3 py-1 text-xs text-gray-500 dark:text-gray-400 bg-gray-100 dark:bg-gray-700 rounded-full">
                {{ formatMessageTime(message.timestamp) }}
              </span>
            </div>

            <div 
              :class="[
                'flex items-start gap-4 w-full',
                message.role === 'user' ? 'flex-row-reverse' : 'flex-row'
              ]"
            >
              <!-- AI像 -->
              <div
                v-if="message.role === 'assistant'"
                class="flex-shrink-0 w-8 h-8 rounded-full bg-gradient-to-r from-purple-500 to-pink-500 flex items-center justify-center -ml-12"
              >
                <span class="text-white text-sm font-medium">AI</span>
              </div>

              <!-- 消息内容 -->
              <div
                :class="[
                  'px-4 py-3 rounded-lg',
                  'flex-shrink',
                  message.role === 'user'
                    ? 'bg-blue-500 text-white'
                    : 'bg-gray-100 dark:bg-gray-700'
                ]"
                :style="{
                  width: message.content.length < 20 ? 'fit-content' : '100%',
                  minWidth: '4rem'
                }"
              >
                <div
                  class="prose dark:prose-invert break-words overflow-hidden min-w-0"
                  v-html="md.render(message.content || '正在思考...')"
                ></div>
                <!-- 加载状态 -->
                <div v-if="isLoading && index === messages.length - 1" 
                     class="text-xs mt-2 text-gray-400 dark:text-gray-500 animate-pulse">
                  正在输入...
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- 输入区域 -->
      <div class="border-t border-gray-200 dark:border-gray-700 bg-white dark:bg-gray-800 p-4">
        <div class="max-w-4xl mx-auto px-4 md:px-12 lg:px-24 relative">
          <div class="relative">
            <textarea
              v-model="inputMessage"
              @keydown.enter.prevent="sendMessage"
              rows="3"
              class="w-full rounded-xl border border-gray-300 dark:border-gray-600 px-4 py-3 pr-24 focus:outline-none focus:ring-2 focus:ring-blue-500 dark:focus:ring-blue-400 bg-gray-50 dark:bg-gray-700 dark:text-white resize-none"
              placeholder="输入消息..."
              :disabled="isLoading"
            ></textarea>
            <button
              v-if="!chatStore.isGenerating"
              @click="sendMessage"
              :disabled="isLoading || !inputMessage.trim()"
              class="absolute right-2 bottom-2 px-4 py-2 bg-blue-500 text-white rounded-lg hover:bg-blue-600 disabled:opacity-50 disabled:cursor-not-allowed transition-colors duration-200 flex items-center gap-2"
            >
              <span>发送</span>
              <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 20 20" fill="currentColor">
                <path d="M10.894 2.553a1 1 0 00-1.788 0l-7 14a1 1 0 001.169 1.409l5-1.429A1 1 0 009 15.571V11a1 1 0 112 0v4.571a1 1 0 00.725.962l5 1.428a1 1 0 001.17-1.408l-7-14z" />
              </svg>
            </button>
            <button
              v-else
              @click="chatStore.stopGenerating"
              class="absolute right-2 bottom-2 px-4 py-2 bg-red-500 text-white rounded-lg hover:bg-red-600 transition-colors duration-200 flex items-center gap-2"
            >
              <span>停止</span>
              <svg xmlns="http://www.w3.org/2000/svg" class="h-4 w-4" viewBox="0 0 20 20" fill="currentColor">
                <path fill-rule="evenodd" d="M10 18a8 8 0 100-16 8 8 0 000 16zM8 7a1 1 0 00-1 1v4a1 1 0 002 0V8a1 1 0 00-1-1zm4 0a1 1 0 00-1 1v4a1 1 0 002 0V8a1 1 0 00-1-1z" clip-rule="evenodd" />
              </svg>
            </button>
          </div>
          <!-- 错误提示 -->
          <div v-if="error" class="mt-2 text-red-500 text-sm">
            {{ error }}
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style>
/* 保留原有的样式 */
</style>