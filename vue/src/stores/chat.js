import { defineStore } from 'pinia'
import { API_URL } from '../config'

export const useChatStore = defineStore('chat', {
    state: () => ({
        conversations: [
            // 默认创建一个对话
            {
                id: 'default',
                name: '对话 1',
                messages: [],
                timestamp: new Date().toISOString()
            }
        ],
        currentConversationId: 'default',
        isLoading: false,
        error: null,
        currentStreamingMessage: '',
        isGenerating: false,
        controller: null,
        conversationCount: 1, // 添加对话计数器
    }),

    getters: {
        currentConversation: (state) =>
            state.conversations.find(conv => conv.id === state.currentConversationId),
        currentMessages: (state) =>
            state.currentConversation?.messages || []
    },

    actions: {
        createNewConversation() {
            // 增加对话计数
            this.conversationCount++

            const newId = `conv-${Date.now()}`
            const newConversation = {
                id: newId,
                name: `对话 ${this.conversationCount}`,
                messages: [],
                timestamp: new Date().toISOString()
            }

            // 使用数组方法添加新对话
            this.conversations = [newConversation, ...this.conversations]
            this.currentConversationId = newId
            this.error = null
            this.currentStreamingMessage = ''
            this.isLoading = false
        },

        stopGenerating() {
            if (this.controller) {
                this.controller.abort()
                this.controller = null
                this.isGenerating = false
            }
        },

        async sendMessage(content) {
            if (!this.currentConversation) return

            this.isLoading = true
            this.error = null
            this.isGenerating = true
            this.controller = new AbortController()
            let reader = null

            try {
                console.log('Sending message to:', API_URL)

                // 添加用户消息
                this.currentConversation.messages.push({
                    role: 'user',
                    content: content,
                    timestamp: new Date().toISOString()
                })

                // 创建AI响应消息
                const aiMessageIndex = this.currentConversation.messages.length
                this.currentConversation.messages.push({
                    role: 'assistant',
                    content: '',
                    timestamp: new Date().toISOString()
                })

                const response = await fetch(API_URL, {
                    method: 'POST',
                    headers: {
                        'Content-Type': 'application/json',
                        'Accept': 'text/event-stream',
                    },
                    body: JSON.stringify({
                        prompt: content,
                        stream: true
                    }),
                    signal: this.controller.signal
                })

                if (!response.ok) {
                    throw new Error(`HTTP error! status: ${response.status}`)
                }

                reader = response.body.getReader()
                const decoder = new TextDecoder()

                while (true) {
                    try {
                        const { value, done } = await reader.read()
                        if (done) {
                            console.log('Stream complete')
                            break
                        }

                        const chunk = decoder.decode(value)
                        console.log('Received chunk:', chunk)

                        const lines = chunk.split('\n')
                        for (const line of lines) {
                            if (line.startsWith('data: ')) {
                                const data = line.slice(6).trim()
                                if (data === '[DONE]') {
                                    console.log('Received [DONE] signal')
                                    return
                                } else if (data) {
                                    this.currentConversation.messages[aiMessageIndex].content += data
                                }
                            }
                        }
                    } catch (error) {
                        console.error('Error reading stream:', error)
                        break
                    }
                }

            } catch (error) {
                if (error.name === 'AbortError') {
                    console.log('Generation aborted')
                } else {
                    console.error('Error details:', error)
                    this.error = `发送消息失败：${error.message}`
                }
            } finally {
                this.isLoading = false
                this.currentStreamingMessage = ''
                this.isGenerating = false
                this.controller = null
                if (reader) {
                    try {
                        await reader.cancel()
                    } catch (e) {
                        console.error('Error closing reader:', e)
                    }
                }
            }
        },

        switchConversation(conversationId) {
            this.currentConversationId = conversationId
            this.error = null
            this.currentStreamingMessage = ''
            this.isLoading = false
        },

        deleteConversation(conversationId) {
            const index = this.conversations.findIndex(conv => conv.id === conversationId)
            if (index !== -1) {
                // 使用新数组替换原数组
                this.conversations = this.conversations.filter(conv => conv.id !== conversationId)

                // 如果删除的是当前对话，切换到最新的对话
                if (conversationId === this.currentConversationId) {
                    if (this.conversations.length > 0) {
                        this.currentConversationId = this.conversations[0].id
                    } else {
                        // 如果没有对话了，创建一个新的
                        this.createNewConversation()
                    }
                }
            }
        },

        updateConversationName(conversationId, newName) {
            const conversation = this.conversations.find(conv => conv.id === conversationId)
            if (conversation && newName.trim()) {
                // 创建新的数组以触发响应式更新
                this.conversations = this.conversations.map(conv =>
                    conv.id === conversationId
                        ? { ...conv, name: newName.trim() }
                        : conv
                )
            }
        }
    }
}) 