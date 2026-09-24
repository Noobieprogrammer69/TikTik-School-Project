import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { BsArchive, BsArrowLeft, BsBellSlash, BsChatDots, BsImage, BsPencil, BsReply, BsSearch, BsSendFill, BsSlashCircle, BsTrash } from 'react-icons/bs'
import { FiFlag } from 'react-icons/fi'
import { useNavigate, useParams } from 'react-router-dom'

import { api } from '../api/client'
import { realtime } from '../api/realtime'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'

const timeLabel = (value) => {
  if (!value) return ''
  return new Intl.DateTimeFormat(undefined, {
    hour: 'numeric',
    minute: '2-digit',
    month: 'short',
    day: 'numeric',
  }).format(new Date(value))
}

const Messages = () => {
  const { userId } = useParams()
  const navigate = useNavigate()
  const endRef = useRef(null)
  const messageListRef = useRef(null)
  const composerRef = useRef(null)
  const previousMessageCount = useRef(0)
  const stickToLatest = useRef(true)
  const typingTimer = useRef(null)
  const { userProfile, status, csrfToken, allUsers, fetchAllUsers, refreshUnread } = useAuthStore()
  const [conversations, setConversations] = useState([])
  const [messages, setMessages] = useState([])
  const [message, setMessage] = useState('')
  const [search, setSearch] = useState('')
  const [loading, setLoading] = useState(true)
  const [loadingMessages, setLoadingMessages] = useState(false)
  const [sending, setSending] = useState(false)
  const [error, setError] = useState('')
  const [online, setOnline] = useState(false)
  const [typing, setTyping] = useState(false)
  const [replyTo, setReplyTo] = useState(null)
  const [attachment, setAttachment] = useState(null)
  const [showArchived, setShowArchived] = useState(false)
  const [atLatest, setAtLatest] = useState(true)
  const [newMessageCount, setNewMessageCount] = useState(0)
  const [hasOlder, setHasOlder] = useState(true)
  const [loadingOlder, setLoadingOlder] = useState(false)
  const [messageSearch, setMessageSearch] = useState('')
  const [searchResults, setSearchResults] = useState(null)
  const [lightbox, setLightbox] = useState(null)

  const selectedId = userId ? decodeURIComponent(userId) : ''
  const conversationUsers = useMemo(
    () => new Map(conversations.map((conversation) => [conversation.user._id, conversation.user])),
    [conversations],
  )
  const selectedUser = conversationUsers.get(selectedId)
    || allUsers.find((user) => user._id === selectedId)
  const selectedConversation = conversations.find((item) => item.user._id === selectedId)

  const loadConversations = useCallback(async (silent = false) => {
    if (!userProfile) return
    if (!silent) setLoading(true)
    try {
      const data = await api.conversations()
      setConversations(data)
      setError('')
      await refreshUnread()
    } catch (requestError) {
      if (!silent) setError(requestError.message)
    } finally {
      if (!silent) setLoading(false)
    }
  }, [refreshUnread, userProfile])

  const loadMessages = useCallback(async (silent = false) => {
    if (!selectedId || !userProfile) return
    if (!silent) setLoadingMessages(true)
    try {
      const data = await api.messages(selectedId)
      setMessages(data)
      setError('')
      if (data.some((item) => item.senderId === selectedId && !item.read)) {
        await api.markConversationRead(selectedId, csrfToken)
        setConversations((current) => current.map((conversation) => (
          conversation.user._id === selectedId
            ? { ...conversation, unreadCount: 0 }
            : conversation
        )))
        await refreshUnread()
      }
    } catch (requestError) {
      if (!silent) setError(requestError.message)
    } finally {
      if (!silent) setLoadingMessages(false)
    }
  }, [csrfToken, refreshUnread, selectedId, userProfile])

  useEffect(() => {
    if (!userProfile) return
    fetchAllUsers()
    loadConversations()
  }, [fetchAllUsers, loadConversations, userProfile])

  useEffect(() => {
    previousMessageCount.current = 0
    stickToLatest.current = true
    setAtLatest(true)
    setNewMessageCount(0)
    setMessages([])
    setHasOlder(true)
    setMessageSearch('')
    setSearchResults(null)
    if (selectedId) loadMessages()
  }, [loadMessages, selectedId])

  useEffect(() => {
    if (!userProfile) return undefined
    const timer = window.setInterval(() => {
      loadConversations(true)
      if (selectedId) loadMessages(true)
    }, 4_000)
    return () => window.clearInterval(timer)
  }, [loadConversations, loadMessages, selectedId, userProfile])

  useEffect(() => {
    if (!selectedId || !userProfile) return undefined
    realtime.send({ type: 'presence_request', userId: selectedId })
    const unsubscribe = realtime.subscribe((event) => {
      if (event.type === 'presence' && event.userId === selectedId) setOnline(event.online)
      if (event.type === 'typing' && event.userId === selectedId) {
        setTyping(event.typing)
        window.clearTimeout(typingTimer.current)
        typingTimer.current = window.setTimeout(() => setTyping(false), 2500)
      }
      if (event.type === 'message' && event.message?.senderId === selectedId) {
        setMessages((current) => current.some((item) => item._id === event.message._id)
          ? current : [...current, event.message])
        api.markConversationRead(selectedId, csrfToken).then(refreshUnread).catch(() => {})
      }
      if (event.type === 'message_updated' && event.message) {
        setMessages((current) => current.map((item) => item._id === event.message._id ? event.message : item))
      }
    })
    return () => {
      unsubscribe()
      window.clearTimeout(typingTimer.current)
    }
  }, [csrfToken, refreshUnread, selectedId, userProfile])

  const scrollToLatest = useCallback((behavior = 'smooth') => {
    const list = messageListRef.current
    if (!list) return
    const reducedMotion = window.matchMedia?.('(prefers-reduced-motion: reduce)').matches
    list.scrollTo({ top: list.scrollHeight, behavior: reducedMotion ? 'auto' : behavior })
    stickToLatest.current = true
    setAtLatest(true)
    setNewMessageCount(0)
  }, [])

  const trackScrollPosition = () => {
    const list = messageListRef.current
    if (!list) return
    const nearLatest = list.scrollHeight - list.scrollTop - list.clientHeight < 72
    stickToLatest.current = nearLatest
    setAtLatest(nearLatest)
    if (nearLatest) setNewMessageCount(0)
  }

  useEffect(() => {
    const previous = previousMessageCount.current
    const added = Math.max(0, messages.length - previous)
    if (messages.length && (previous === 0 || stickToLatest.current)) {
      window.requestAnimationFrame(() => scrollToLatest(previous === 0 ? 'auto' : 'smooth'))
    } else if (added > 0) {
      setNewMessageCount((count) => count + added)
    }
    previousMessageCount.current = messages.length
  }, [messages.length, scrollToLatest])

  const people = useMemo(() => {
    const term = search.trim().toLowerCase()
    const conversationIds = new Set(conversations.map((item) => item.user._id))
    return allUsers.filter((user) => (
      user._id !== userProfile?._id
      && !conversationIds.has(user._id)
      && (!term || user.userName.toLowerCase().includes(term))
    ))
  }, [allUsers, conversations, search, userProfile])

  const visibleConversations = conversations.filter((item) => (
    (showArchived ? item.archived : !item.archived)
    && (!search.trim() || item.user.userName.toLowerCase().includes(search.trim().toLowerCase()))
  ))

  const send = async (event) => {
    event.preventDefault()
    const text = message.trim()
    if ((!text && !attachment) || !selectedId || sending) return
    setSending(true)
    stickToLatest.current = true
    setError('')
    try {
      let created
      if (attachment) {
        const form = new FormData()
        form.append('attachment', attachment)
        form.append('message', text)
        if (replyTo) form.append('replyTo', replyTo._id)
        created = await api.sendAttachment(selectedId, form, csrfToken)
      } else {
        created = await api.sendMessage(selectedId, text, csrfToken, replyTo?._id || '')
      }
      setMessages((current) => [...current, created])
      setMessage('')
      if (composerRef.current) composerRef.current.style.height = ''
      setAttachment(null)
      setReplyTo(null)
      realtime.send({ type: 'typing', userId: selectedId, typing: false })
      await loadConversations(true)
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setSending(false)
    }
  }

  const react = async (item, emoji) => {
    const mine = item.reactions?.find((reaction) => reaction.userId === userProfile._id)?.emoji
    const updated = await api.reactToMessage(item._id, mine === emoji ? '' : emoji, csrfToken)
    setMessages((current) => current.map((messageItem) => messageItem._id === item._id ? updated : messageItem))
  }

  const removeMessage = async (item) => {
    if (!window.confirm('Delete this message?')) return
    const updated = await api.deleteMessage(item._id, csrfToken)
    setMessages((current) => current.map((messageItem) => messageItem._id === item._id ? updated : messageItem))
  }

  const reportMessage = async (item) => {
    const details = window.prompt('Describe why this message should be reviewed:', '')
    if (details === null) return
    try { await api.reportContent('message', item._id, 'harassment', details, csrfToken); setError('Message reported privately to moderators.') }
    catch (requestError) { setError(requestError.message) }
  }

  const editMessage = async (item) => {
    const text = window.prompt('Edit your message', item.text)
    if (!text?.trim() || text.trim() === item.text) return
    try {
      const updated = await api.editMessage(item._id, text.trim(), csrfToken)
      setMessages((current) => current.map((messageItem) => messageItem._id === item._id ? updated : messageItem))
    } catch (requestError) { setError(requestError.message) }
  }

  const loadOlder = async () => {
    if (!messages.length || loadingOlder) return
    const list = messageListRef.current
    const oldHeight = list?.scrollHeight || 0
    setLoadingOlder(true)
    try {
      const page = await api.messageHistory(selectedId, messages[0]._id)
      setHasOlder(Boolean(page.nextCursor))
      setMessages((current) => [...page.items.filter((item) => !current.some((existing) => existing._id === item._id)), ...current])
      window.requestAnimationFrame(() => { if (list) list.scrollTop += list.scrollHeight - oldHeight })
    } catch (requestError) { setError(requestError.message) }
    finally { setLoadingOlder(false) }
  }

  const searchConversation = async (event) => {
    event.preventDefault()
    if (messageSearch.trim().length < 2) { setSearchResults(null); return }
    try { setSearchResults(await api.searchMessages(selectedId, messageSearch.trim())) }
    catch (requestError) { setError(requestError.message) }
  }

  const updateConversation = async (updates) => {
    const current = conversations.find((item) => item.user._id === selectedId) || {}
    await api.conversationSettings(selectedId, updates.muted ?? current.muted ?? false, updates.archived ?? current.archived ?? false, csrfToken)
    await loadConversations(true)
    if (updates.archived) navigate('/messages')
  }

  const reportFromChat = async () => {
    if (!window.confirm(`Privately report ${selectedUser.userName} for inappropriate chat behavior?`)) return
    await api.reportUser(selectedId, 'inappropriate', 'Reported from private chat', csrfToken)
    setError('Report submitted privately to moderators.')
  }

  const blockFromChat = async () => {
    if (!window.confirm(`Block ${selectedUser.userName}? They will no longer be able to interact with you.`)) return
    await api.setBlocked(selectedId, true, csrfToken)
    navigate('/messages')
  }

  if (status === 'loading') return <Loading text="Checking your login..." />
  if (!userProfile) {
    return <StatusMessage title="Login required" message="Sign in with Google to send and receive private messages." />
  }

  return (
    <section className="chat-shell" aria-label="Messages">
      <aside className={`chat-sidebar ${selectedId ? 'chat-sidebar-hidden-mobile' : ''}`}>
        <div className="chat-sidebar-header">
          <div>
            <p className="eyebrow">Your inbox</p>
            <h1>Messages</h1>
          </div>
          <BsChatDots aria-hidden="true" />
        </div>
        <label className="sr-only" htmlFor="people-search">Find people</label>
        <input
          id="people-search"
          type="search"
          value={search}
          onChange={(event) => setSearch(event.target.value)}
          placeholder="Find someone..."
          className="chat-search"
        />
        <button type="button" className="chat-archive-toggle" onClick={() => setShowArchived((value) => !value)}>{showArchived ? 'Active conversations' : 'Archived conversations'}</button>

        <div className="chat-contact-list">
          {loading ? <Loading text="Loading conversations..." /> : (
            <>
              {visibleConversations.map((conversation) => (
                <button
                  type="button"
                  key={conversation.user._id}
                  onClick={() => navigate(`/messages/${encodeURIComponent(conversation.user._id)}`)}
                  className={`chat-contact ${selectedId === conversation.user._id ? 'chat-contact-active' : ''}`}
                >
                  <Avatar src={conversation.user.image} alt="" className="chat-avatar" />
                  <span className="chat-contact-copy">
                    <strong>{conversation.user.userName}</strong>
                    <small>{conversation.lastMessage.text}</small>
                  </span>
                  <span className="chat-contact-meta">
                    <small>{timeLabel(conversation.lastMessage.createdAt)}</small>
                    {conversation.unreadCount > 0 && (
                      <span className="unread-badge">{conversation.unreadCount}</span>
                    )}
                  </span>
                </button>
              ))}

              {people.length > 0 && <p className="chat-section-label">Start a conversation</p>}
              {people.map((user) => (
                <button
                  type="button"
                  key={user._id}
                  onClick={() => navigate(`/messages/${encodeURIComponent(user._id)}`)}
                  className="chat-contact"
                >
                  <Avatar src={user.image} alt="" className="chat-avatar" />
                  <span className="chat-contact-copy"><strong>{user.userName}</strong><small>Say hello</small></span>
                </button>
              ))}
              {!visibleConversations.length && !people.length && (
                <p className="chat-empty-small">No matching people found.</p>
              )}
            </>
          )}
        </div>
      </aside>

      <div className={`chat-panel ${selectedId ? '' : 'chat-panel-hidden-mobile'}`}>
        {selectedId && selectedUser ? (
          <>
            <header className="chat-panel-header">
              <button type="button" className="chat-back" onClick={() => navigate('/messages')} aria-label="Back to conversations">
                <BsArrowLeft />
              </button>
              <Avatar src={selectedUser.image} alt="" className="chat-avatar" />
              <div><strong>{selectedUser.userName}</strong><small>{typing ? 'Typing…' : online ? 'Online now' : 'Private conversation'}</small></div>
              <form className="chat-message-search" onSubmit={searchConversation}>
                <label className="sr-only" htmlFor="message-search">Search this conversation</label>
                <input id="message-search" type="search" value={messageSearch} onChange={(event) => { setMessageSearch(event.target.value); if (!event.target.value) setSearchResults(null) }} placeholder="Search chat" />
                <button type="submit" aria-label="Search conversation"><BsSearch /></button>
              </form>
              <div className="chat-header-actions">
                <button type="button" aria-label={selectedConversation?.muted ? 'Unmute conversation' : 'Mute conversation'} onClick={() => updateConversation({ muted: !selectedConversation?.muted })}><BsBellSlash /></button>
                <button type="button" aria-label={selectedConversation?.archived ? 'Restore conversation' : 'Archive conversation'} onClick={() => updateConversation({ archived: !selectedConversation?.archived })}><BsArchive /></button>
                <button type="button" aria-label="Report chat account" onClick={reportFromChat}><FiFlag /></button>
                <button type="button" aria-label="Block chat account" onClick={blockFromChat}><BsSlashCircle /></button>
              </div>
            </header>

            <div className="chat-message-region">
              {searchResults && <div className="chat-search-results"><div><strong>{searchResults.length} result{searchResults.length === 1 ? '' : 's'}</strong><button type="button" onClick={() => { setSearchResults(null); setMessageSearch('') }}>Close</button></div>{searchResults.map((item) => <button type="button" key={item._id} onClick={() => setSearchResults(null)}><span>{item.text}</span><small>{timeLabel(item.createdAt)}</small></button>)}</div>}
              <div ref={messageListRef} className="chat-messages" aria-live="polite" aria-busy={loadingMessages} onScroll={trackScrollPosition}>
                {!loadingMessages && messages.length > 0 && hasOlder && <button type="button" className="load-older-messages" onClick={loadOlder} disabled={loadingOlder}>{loadingOlder ? 'Loading…' : 'Load older messages'}</button>}
                {loadingMessages ? <Loading text="Loading messages..." /> : !messages.length && (
                  <div className="chat-welcome">
                    <Avatar src={selectedUser.image} alt="" className="chat-welcome-avatar" />
                    <h2>Start chatting with {selectedUser.userName}</h2>
                    <p>Messages are private between the two of you.</p>
                  </div>
                )}
                {!loadingMessages && messages.map((item, index) => {
                  const mine = item.senderId === userProfile._id
                  const previous = messages[index - 1]
                  const showDay = !previous || new Date(previous.createdAt).toDateString() !== new Date(item.createdAt).toDateString()
                  return (
                    <div key={item._id} className="message-entry">
                    {showDay && <div className="message-day-separator"><span>{new Date(item.createdAt).toLocaleDateString(undefined, { weekday: 'short', month: 'short', day: 'numeric' })}</span></div>}
                    <div className={`message-row ${mine ? 'message-row-mine' : ''}`}>
                      {!mine && <Avatar src={selectedUser.image} alt="" className="message-avatar" />}
                      <div className={`message-bubble ${mine ? 'message-bubble-mine' : ''}`}>
                        {item.replyTo && <small className="message-reply-context">Replying to {messages.find((candidate) => candidate._id === item.replyTo)?.text || 'an earlier message'}</small>}
                        <p>{item.text}</p>
                        {item.attachment && <button type="button" className="message-attachment-button" onClick={() => setLightbox(item.attachment)}><img className="message-attachment" src={item.attachment.url} alt={item.attachment.name || 'Message attachment'} /></button>}
                        <small>{timeLabel(item.createdAt)}{item.edited ? ' · Edited' : ''}{mine && item.read ? ' · Read' : ''}</small>
                        {!item.deleted && (
                          <div className="message-actions">
                            <button type="button" aria-label="Reply to message" onClick={() => setReplyTo(item)}><BsReply /></button>
                            {['❤️', '😂', '👍'].map((emoji) => <button type="button" key={emoji} aria-label={`React with ${emoji}`} onClick={() => react(item, emoji)}>{emoji}</button>)}
                            {mine && <button type="button" aria-label="Edit message" onClick={() => editMessage(item)}><BsPencil /></button>}
                            {!mine && <button type="button" aria-label="Report message" onClick={() => reportMessage(item)}><FiFlag /></button>}
                            {mine && <button type="button" aria-label="Delete message" onClick={() => removeMessage(item)}><BsTrash /></button>}
                          </div>
                        )}
                        {item.reactions?.length > 0 && <div className="message-reactions">{item.reactions.map((reaction) => <span key={`${reaction.userId}-${reaction.emoji}`}>{reaction.emoji}</span>)}</div>}
                      </div>
                    </div>
                    </div>
                  )
                })}
                <div ref={endRef} />
              </div>
              {!atLatest && (
                <button type="button" className="chat-jump-latest" onClick={() => scrollToLatest()}>
                  {newMessageCount > 0 ? `${newMessageCount} new message${newMessageCount === 1 ? '' : 's'}` : 'Jump to latest'}
                </button>
              )}
            </div>

            {error && <p className="chat-error" role="alert">{error}</p>}
            {replyTo && <div className="composer-context">Replying to: {replyTo.text}<button type="button" onClick={() => setReplyTo(null)}>Cancel</button></div>}
            {attachment && <div className="composer-context">Attached: {attachment.name}<button type="button" onClick={() => setAttachment(null)}>Remove</button></div>}
            <form className="chat-composer" onSubmit={send}>
              <label className="sr-only" htmlFor="chat-message">Message</label>
              <textarea
                ref={composerRef}
                id="chat-message"
                value={message}
                onChange={(event) => {
                  setMessage(event.target.value)
                  event.target.style.height = 'auto'
                  event.target.style.height = `${Math.min(event.target.scrollHeight, 120)}px`
                  realtime.send({ type: 'typing', userId: selectedId, typing: Boolean(event.target.value) })
                }}
                onKeyDown={(event) => {
                  if (event.key === 'Enter' && !event.shiftKey) {
                    event.preventDefault()
                    event.currentTarget.form?.requestSubmit()
                  }
                }}
                maxLength={1000}
                rows={1}
                placeholder={`Message ${selectedUser.userName}`}
              />
              <span className="chat-character-count">{message.length}/1000</span>
              <label className="chat-attach" aria-label="Attach image"><BsImage /><input type="file" accept="image/jpeg,image/png,image/webp" hidden onChange={(event) => setAttachment(event.target.files?.[0] || null)} /></label>
              <button type="submit" disabled={sending || (!message.trim() && !attachment)} aria-label={sending ? 'Sending message' : 'Send message'}>
                {sending ? <span className="button-spinner" aria-hidden="true" /> : <BsSendFill />}
              </button>
            </form>
          </>
        ) : selectedId ? (
          <StatusMessage title="User not found" message="This person is not available to chat." />
        ) : (
          <div className="chat-empty">
            <span><BsChatDots /></span>
            <h2>Your conversations</h2>
            <p>Choose someone from the list to start chatting.</p>
          </div>
        )}
      </div>
      {lightbox && <div className="media-lightbox" role="dialog" aria-modal="true" onClick={() => setLightbox(null)}><button type="button" aria-label="Close image" onClick={() => setLightbox(null)}>×</button><img src={lightbox.url} alt={lightbox.name || 'Full-size attachment'} onClick={(event) => event.stopPropagation()} /></div>}
    </section>
  )
}

export default Messages
