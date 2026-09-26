import { useEffect, useRef, useState } from 'react'
import { IoClose } from 'react-icons/io5'
import { Link, Outlet } from 'react-router-dom'

import { realtime } from '../api/realtime'
import useAuthStore from '../store/authStore'
import Navbar from './Navbar'
import Sidebar from './Sidebar'

const Layout = ({ googleLoginEnabled }) => {
  const [liveNotice, setLiveNotice] = useState('')
  const noticeTimer = useRef(null)
  const initialize = useAuthStore((state) => state.initialize)
  const userProfile = useAuthStore((state) => state.userProfile)
  const refreshUnread = useAuthStore((state) => state.refreshUnread)
  const refreshNotifications = useAuthStore((state) => state.refreshNotifications)

  useEffect(() => {
    initialize()
  }, [initialize])

  useEffect(() => {
    if (!userProfile) return undefined
    realtime.start()
    const unsubscribe = realtime.subscribe((event) => {
      if (event.type === 'message') refreshUnread()
      if (event.type === 'message' || event.type === 'notification') refreshNotifications()
      if (event.type === 'notification') {
        const labels = {
          follow: 'You have a new follower.',
          new_post: 'Someone you follow posted a video.',
          post_like: 'Someone liked your video.',
          post_share: 'Someone shared your video.',
          comment: 'Someone commented on your video.',
          reply: 'Someone replied to your comment.',
          comment_like: 'Someone liked your comment.',
          comment_pin: 'Your comment was pinned.',
          message: 'You received a new message.',
          mention: 'Someone mentioned you.',
          follow_request: 'You received a follow request.',
          follow_accepted: 'Your follow request was accepted.',
        }
        setLiveNotice(labels[event.notificationType] || 'You have a new notification.')
        if (document.hidden && window.Notification?.permission === 'granted') {
          navigator.serviceWorker?.ready.then((registration) => registration.showNotification('RippleNest activity', {
            body: labels[event.notificationType] || 'You have a new notification.',
            icon: '/app-icon.svg', tag: `ripplenest-${event.notificationType}`,
          })).catch(() => {})
        }
        window.clearTimeout(noticeTimer.current)
        noticeTimer.current = window.setTimeout(() => setLiveNotice(''), 5000)
      }
    })
    refreshUnread()
    refreshNotifications()
    const timer = window.setInterval(() => {
      refreshUnread()
      refreshNotifications()
    }, 8_000)
    return () => {
      window.clearInterval(timer)
      window.clearTimeout(noticeTimer.current)
      unsubscribe()
      realtime.stop()
    }
  }, [refreshNotifications, refreshUnread, userProfile])

  return (
    <div className="app-shell">
      <a className="skip-link" href="#main-content">Skip to main content</a>
      <Navbar googleLoginEnabled={googleLoginEnabled} />
      {liveNotice && (
        <div className="live-notification" role="status" aria-live="polite">
          <Link to="/notifications" onClick={() => setLiveNotice('')}><strong>{liveNotice}</strong><span>Open activity</span></Link>
          <button type="button" aria-label="Dismiss notification" onClick={() => setLiveNotice('')}><IoClose /></button>
        </div>
      )}
      <div className="app-content">
        <div className="sidebar-scroll"><Sidebar /></div>
        <main id="main-content" className="videos app-main" tabIndex="-1"><Outlet /></main>
      </div>
    </div>
  )
}

export default Layout
