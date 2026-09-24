import { create } from 'zustand'

import { ApiError, api } from '../api/client'

let initialization

const useAuthStore = create((set, get) => ({
  userProfile: null,
  csrfToken: '',
  allUsers: [],
  unreadCount: 0,
  notificationCount: 0,
  status: 'loading',
  authError: '',

  setUserProfile: (userProfile) => set({ userProfile }),

  initialize: async () => {
    if (initialization) return initialization
    initialization = (async () => {
      try {
        const session = await api.session()
        set({
          userProfile: session.user,
          csrfToken: session.csrfToken,
          status: 'authenticated',
          authError: '',
        })
        await get().fetchAllUsers()
        await get().refreshUnread()
        await get().refreshNotifications()
      } catch (error) {
        if (!(error instanceof ApiError) || error.status !== 401) {
          set({ authError: error.message })
        }
        set({ userProfile: null, csrfToken: '', status: 'anonymous' })
      }
    })()
    await initialization
  },

  loginWithGoogle: async (credential) => {
    set({ status: 'loading', authError: '' })
    try {
      const session = await api.googleLogin(credential)
      set({
        userProfile: session.user,
        csrfToken: session.csrfToken,
        status: 'authenticated',
      })
      await get().fetchAllUsers()
      await get().refreshUnread()
      await get().refreshNotifications()
    } catch (error) {
      set({ status: 'anonymous', authError: error.message })
      throw error
    }
  },

  logout: async () => {
    const csrfToken = get().csrfToken
    try {
      if (csrfToken) await api.logout(csrfToken)
    } finally {
      initialization = undefined
      set({
        userProfile: null,
        csrfToken: '',
        unreadCount: 0,
        notificationCount: 0,
        status: 'anonymous',
        authError: '',
      })
    }
  },

  fetchAllUsers: async () => {
    try {
      const allUsers = await api.users()
      set({ allUsers })
    } catch (error) {
      set({ authError: error.message })
    }
  },

  refreshUnread: async () => {
    if (!get().userProfile) {
      set({ unreadCount: 0 })
      return []
    }
    try {
      const conversations = await api.conversations()
      set({
        unreadCount: conversations.reduce(
          (total, conversation) => total + Number(conversation.unreadCount || 0),
          0,
        ),
      })
      return conversations
    } catch {
      return []
    }
  },

  refreshNotifications: async () => {
    if (!get().userProfile) {
      set({ notificationCount: 0 })
      return []
    }
    try {
      const notifications = await api.notifications()
      set({ notificationCount: notifications.filter((item) => !item.read).length })
      return notifications
    } catch {
      return []
    }
  },
}))

export default useAuthStore
