import { useEffect, useMemo, useState } from 'react'
import { IoClose, IoNotificationsOutline } from 'react-icons/io5'
import { Link } from 'react-router-dom'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'

const descriptions = {
  follow: 'started following you',
  post_like: 'liked your video',
  comment: 'commented on your video',
  reply: 'replied to your comment',
  comment_like: 'liked your comment',
  comment_pin: 'pinned your comment',
  post_share: 'shared your video',
  message: 'sent you a message',
  new_post: 'posted a new video',
  mention: 'mentioned you',
  follow_request: 'requested to follow you',
  follow_accepted: 'accepted your follow request',
}

const notificationLink = (notification) => {
  if (notification.type === 'follow' || notification.type === 'follow_accepted') return `/profile/${encodeURIComponent(notification.actor._id)}`
  if (notification.type === 'follow_request') return '/settings'
  if (notification.type === 'message') return `/messages/${encodeURIComponent(notification.actor._id)}`
  if (notification.postId) return `/detail/${encodeURIComponent(notification.postId)}`
  return '/'
}

const formatTime = (value) => {
  if (!value) return ''
  return new Intl.DateTimeFormat(undefined, {
    month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit',
  }).format(new Date(value))
}

const Notifications = () => {
  const { userProfile, status, csrfToken, refreshNotifications } = useAuthStore()
  const [items, setItems] = useState(null)
  const [error, setError] = useState('')
  const [filter, setFilter] = useState('all')
  const visibleItems = useMemo(() => (items || []).filter((item) => {
    if (filter === 'unread') return !item.read
    if (filter === 'social') return ['follow', 'follow_request', 'follow_accepted', 'mention'].includes(item.type)
    if (filter === 'videos') return ['post_like', 'post_share', 'comment', 'reply', 'comment_like', 'comment_pin', 'new_post'].includes(item.type)
    return true
  }), [filter, items])

  useEffect(() => {
    if (!userProfile) return
    let active = true
    api.notifications()
      .then((data) => {
        if (!active) return
        setItems(data)
        setError('')
      })
      .catch((requestError) => active && setError(requestError.message))
    return () => { active = false }
  }, [csrfToken, refreshNotifications, userProfile])

  if (status === 'loading') return <Loading text="Checking your login..." />
  if (!userProfile) {
    return <StatusMessage title="Login required" message="Sign in to see your notifications." />
  }
  if (error) return <StatusMessage title="Could not load notifications" message={error} />
  if (!items) return <Loading text="Loading notifications..." />

  const markAllRead = async () => {
    await api.markNotificationsRead(csrfToken)
    setItems((current) => current.map((item) => ({ ...item, read: true })))
    await refreshNotifications()
  }

  const openNotification = async (notification) => {
    if (!notification.read) {
      await api.markNotificationRead(notification._id, csrfToken)
      setItems((current) => current.map((item) => item._id === notification._id ? { ...item, read: true } : item))
      await refreshNotifications()
    }
  }

  const removeNotification = async (event, notification) => {
    event.preventDefault()
    await api.deleteNotification(notification._id, csrfToken)
    setItems((current) => current.filter((item) => item._id !== notification._id))
    await refreshNotifications()
  }

  return (
    <section className="notifications-page">
      <header className="notifications-header">
        <div>
          <p className="eyebrow">Activity</p>
          <h1>Notifications</h1>
        </div>
        <div className="notification-header-actions">
          {items.some((item) => !item.read) && <button type="button" onClick={markAllRead}>Mark all read</button>}
          <Link to="/settings">Preferences</Link>
          <IoNotificationsOutline aria-hidden="true" />
        </div>
      </header>
      <div className="notification-filters" role="tablist" aria-label="Notification filters">{[['all', 'All'], ['unread', 'Unread'], ['social', 'Social'], ['videos', 'Videos']].map(([value, label]) => <button key={value} type="button" role="tab" aria-selected={filter === value} onClick={() => setFilter(value)}>{label}</button>)}</div>
      {visibleItems.length ? (
        <div className="notification-list">
          {visibleItems.map((notification, index) => {
            const day = new Date(notification.createdAt).toDateString()
            const previousDay = index ? new Date(visibleItems[index - 1].createdAt).toDateString() : ''
            return <div key={notification._id}>{day !== previousDay && <h2 className="notification-day">{new Date(notification.createdAt).toLocaleDateString(undefined, { weekday: 'long', month: 'long', day: 'numeric' })}</h2>}
            <article
              className={`notification-item ${notification.read ? '' : 'notification-unread'}`}
            >
              <Link to={notificationLink(notification)} onClick={() => openNotification(notification)} className="notification-main-link">
                <Avatar src={notification.actor.image} alt="" className="notification-avatar" />
                <div className="notification-copy">
                  <p><strong>{notification.actor.userName}</strong>{' '}{descriptions[notification.type] || 'interacted with you'}</p>
                  <small>{formatTime(notification.createdAt)}</small>
                </div>
              </Link>
              {!notification.read && <span className="notification-dot" aria-label="Unread" />}
              <button className="notification-delete" aria-label="Delete notification" onClick={(event) => removeNotification(event, notification)}><IoClose /></button>
            </article>
            </div>
          })}
        </div>
      ) : (
        <div className="notification-empty">
          <IoNotificationsOutline />
          <h2>No notifications yet</h2>
          <p>New follows, likes, comments, replies, shares, messages, and videos will appear here.</p>
        </div>
      )}
    </section>
  )
}

export default Notifications
