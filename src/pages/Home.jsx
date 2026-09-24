import { useCallback, useEffect, useRef, useState } from 'react'
import { useSearchParams } from 'react-router-dom'

import { api } from '../api/client'
import Loading from '../components/Loading'
import NoResults from '../components/NoResults'
import StatusMessage from '../components/StatusMessage'
import VideoCard from '../components/VideoCard'
import useAuthStore from '../store/authStore'

const Home = () => {
  const [searchParams, setSearchParams] = useSearchParams()
  const { userProfile, csrfToken } = useAuthStore()
  const topic = searchParams.get('topic') || ''
  const hashtag = searchParams.get('hashtag') || ''
  const requestedMode = searchParams.get('feed') || 'for_you'
  const mode = requestedMode === 'following' && userProfile ? 'following' : 'for_you'
  const [videos, setVideos] = useState([])
  const [nextCursor, setNextCursor] = useState('')
  const [state, setState] = useState({ loading: true, loadingMore: false, error: '' })
  const [attempt, setAttempt] = useState(0)
  const sentinel = useRef(null)
  const [undo, setUndo] = useState(null)

  useEffect(() => {
    let active = true
    setState({ loading: true, loadingMore: false, error: '' })
    api.feed({ mode, topic, hashtag })
      .then((data) => {
        if (!active) return
        setVideos(data.items)
        setNextCursor(data.nextCursor)
        setState({ loading: false, loadingMore: false, error: '' })
      })
      .catch((error) => active && setState({ loading: false, loadingMore: false, error: error.message }))
    return () => { active = false }
  }, [attempt, hashtag, mode, topic, userProfile])

  const loadMore = useCallback(async () => {
    if (!nextCursor || state.loadingMore) return
    setState((current) => ({ ...current, loadingMore: true }))
    try {
      const data = await api.feed({ mode, topic, hashtag, cursor: nextCursor })
      setVideos((current) => [...current, ...data.items.filter((item) => !current.some((old) => old._id === item._id))])
      setNextCursor(data.nextCursor)
      setState((current) => ({ ...current, loadingMore: false, error: '' }))
    } catch (error) {
      setState((current) => ({ ...current, loadingMore: false, error: error.message }))
    }
  }, [hashtag, mode, nextCursor, state.loadingMore, topic])

  useEffect(() => {
    if (!sentinel.current || !nextCursor) return undefined
    const observer = new window.IntersectionObserver((entries) => {
      if (entries.some((entry) => entry.isIntersecting)) loadMore()
    }, { rootMargin: '300px' })
    observer.observe(sentinel.current)
    return () => observer.disconnect()
  }, [loadMore, nextCursor])

  const chooseMode = (nextMode) => {
    const params = new window.URLSearchParams(searchParams)
    if (nextMode === 'for_you') params.delete('feed')
    else params.set('feed', nextMode)
    setSearchParams(params)
  }

  const applyFeedback = async (post, type) => {
    if (!userProfile) return
    const previous = videos
    setVideos((items) => type === 'hide_creator' ? items.filter((item) => item.userId !== post.userId) : items.filter((item) => item._id !== post._id))
    setUndo({ post, type, previous })
    try { await api.feedback(post._id, type, true, csrfToken) }
    catch (feedbackError) { setVideos(previous); setUndo(null); setState((current) => ({ ...current, error: feedbackError.message })) }
  }

  const undoFeedback = async () => {
    if (!undo) return
    try { await api.feedback(undo.post._id, undo.type, false, csrfToken); setVideos(undo.previous) }
    catch (feedbackError) { setState((current) => ({ ...current, error: feedbackError.message })) }
    finally { setUndo(null) }
  }

  if (state.loading) return <Loading text={topic ? `Loading ${topic} videos...` : 'Loading videos...'} />
  if (state.error && !videos.length) {
    return <StatusMessage title="Could not load videos" message={state.error} onRetry={() => setAttempt((value) => value + 1)} />
  }

  return (
    <section className="feed-page">
      <div className="feed-toolbar">
        <div className="segmented-tabs" role="tablist" aria-label="Video feed">
          <button role="tab" aria-selected={mode === 'for_you'} onClick={() => chooseMode('for_you')}>For You</button>
          <button role="tab" aria-selected={mode === 'following'} disabled={!userProfile} onClick={() => chooseMode('following')}>Following</button>
        </div>
        {(topic || hashtag) && <p className="feed-filter">Showing {topic || `#${hashtag}`}</p>}
      </div>
      <div className="videos flex h-full flex-col gap-10">
        {videos.length ? videos.map((video) => <VideoCard post={video} key={video._id} onNotInterested={userProfile ? (post) => applyFeedback(post, 'not_interested') : null} onHideCreator={userProfile ? (post) => applyFeedback(post, 'hide_creator') : null} />) : <NoResults text={mode === 'following' ? 'Follow Accounts To Build This Feed' : 'No Videos'} />}
      </div>
      {undo && <div className="undo-toast" role="status"><span>{undo.type === 'hide_creator' ? 'Creator hidden from your feed.' : 'We’ll show fewer videos like this.'}</span><button type="button" onClick={undoFeedback}>Undo</button><button type="button" aria-label="Dismiss" onClick={() => setUndo(null)}>×</button></div>}
      {state.error && <p className="feed-error" role="alert">{state.error}</p>}
      <div ref={sentinel} className="feed-sentinel">
        {state.loadingMore && <Loading text="Loading more videos..." />}
        {!nextCursor && videos.length > 0 && <p>You’re all caught up.</p>}
      </div>
    </section>
  )
}

export default Home
