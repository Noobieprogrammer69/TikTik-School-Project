import { useEffect, useRef, useState } from 'react'
import { BsBookmark, BsBookmarkFill, BsFillPlayFill, BsShare } from 'react-icons/bs'
import { FiFlag } from 'react-icons/fi'
import { GoVerified } from 'react-icons/go'
import { HiVolumeOff, HiVolumeUp } from 'react-icons/hi'
import { MdDeleteOutline, MdOutlineCancel } from 'react-icons/md'
import { Link, useNavigate, useParams } from 'react-router-dom'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Comments from '../components/Comments'
import LikeButton from '../components/LikeButton'
import Loading from '../components/Loading'
import RichText from '../components/RichText'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'
import { availableQualities, mediaUrl } from '../utils/media'

const Detail = () => {
  const { id } = useParams()
  const navigate = useNavigate()
  const videoRef = useRef(null)
  const { userProfile, csrfToken } = useAuthStore()
  const [post, setPost] = useState(null)
  const [playing, setPlaying] = useState(false)
  const [muted, setMuted] = useState(false)
  const [comment, setComment] = useState('')
  const [pending, setPending] = useState('')
  const [error, setError] = useState('')
  const [saved, setSaved] = useState(false)
  const [quality, setQuality] = useState('auto')
  const openedViewFor = useRef('')

  useEffect(() => {
    let active = true
    setPost(null)
    setError('')
    api.post(id)
      .then((data) => active && setPost(data))
      .catch((requestError) => active && setError(requestError.message))
    return () => { active = false }
  }, [id])

  useEffect(() => {
    if (!post || !userProfile) return
    api.savedVideos()
      .then((items) => setSaved(items.some((item) => item._id === post._id)))
      .catch(() => {})
  }, [post?._id, userProfile]) // eslint-disable-line react-hooks/exhaustive-deps

  useEffect(() => {
    if (videoRef.current) videoRef.current.muted = muted
  }, [muted])

  useEffect(() => {
    if (!post?._id || openedViewFor.current === post._id) return
    openedViewFor.current = post._id
    api.recordView(post._id, 0, csrfToken)
      .then((result) => setPost((current) => current?._id === post._id
        ? { ...current, viewCount: result.viewCount }
        : current))
      .catch(() => {
        if (openedViewFor.current === post._id) openedViewFor.current = ''
      })
  }, [post?._id, csrfToken])

  const togglePlayback = async () => {
    if (!videoRef.current) return
    if (videoRef.current.paused) await videoRef.current.play()
    else videoRef.current.pause()
  }

  const liked = Boolean(userProfile && post?.likes?.some((like) => like._ref === userProfile._id))

  const toggleLike = async () => {
    if (!userProfile) return
    setPending('like')
    setError('')
    try {
      setPost(await api.setLike(post._id, !liked, csrfToken))
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setPending('')
    }
  }

  const addComment = async (event) => {
    event.preventDefault()
    if (!comment.trim() || !userProfile) return
    setPending('comment')
    setError('')
    try {
      setPost(await api.addComment(post._id, comment.trim(), csrfToken))
      setComment('')
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setPending('')
    }
  }

  const toggleCommentLike = async (commentId, like) => {
    setPending(`comment-like-${commentId}`)
    setError('')
    try {
      setPost(await api.setCommentLike(post._id, commentId, like, csrfToken))
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setPending('')
    }
  }

  const addReply = async (commentId, reply) => {
    setPending(`reply-${commentId}`)
    setError('')
    try {
      setPost(await api.addReply(post._id, commentId, reply, csrfToken))
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setPending('')
    }
  }

  const togglePinnedComment = async (commentId, pinned) => {
    setPending(`comment-pin-${commentId}`)
    setError('')
    try {
      setPost(await api.setCommentPinned(post._id, commentId, pinned, csrfToken))
    } catch (requestError) {
      setError(requestError.message)
    } finally {
      setPending('')
    }
  }

  const editComment = async (commentId, text) => {
    setPost(await api.editComment(post._id, commentId, text, csrfToken))
  }

  const deleteComment = async (commentId) => {
    setPost(await api.deleteComment(post._id, commentId, csrfToken))
  }

  const editReply = async (commentId, replyId, text) => {
    setPost(await api.editReply(post._id, commentId, replyId, text, csrfToken))
  }

  const deleteReply = async (commentId, replyId) => {
    setPost(await api.deleteReply(post._id, commentId, replyId, csrfToken))
  }

  const toggleSaved = async () => {
    const value = !saved
    await api.setSaved(post._id, value, csrfToken)
    setSaved(value)
  }

  const share = async () => {
    const url = window.location.href
    setError('')
    try {
      if (navigator.share) await navigator.share({ title: post.caption, url })
      else if (navigator.clipboard?.writeText) await navigator.clipboard.writeText(url)
      else throw new Error('Sharing is not supported by this browser.')
      if (userProfile) setPost(await api.recordShare(post._id, csrfToken))
    } catch (shareError) {
      if (shareError?.name !== 'AbortError') setError(shareError.message || 'The video could not be shared.')
    }
  }

  const loopVideo = () => {
    const video = videoRef.current
    if (!video) return
    api.recordView(post._id, Math.floor(video.duration || video.currentTime || 0), csrfToken)
      .then((result) => setPost((current) => ({ ...current, viewCount: result.viewCount })))
      .catch(() => {})
    video.currentTime = 0
    video.play().catch(() => setPlaying(false))
  }

  const deletePost = async () => {
    if (!window.confirm('Delete this video permanently?')) return
    setPending('delete')
    setError('')
    try {
      await api.deletePost(post._id, csrfToken)
      navigate('/')
    } catch (requestError) {
      setError(requestError.message)
      setPending('')
    }
  }

  const reportVideo = async () => {
    const details = window.prompt('Tell the moderators what is wrong with this video (optional):', '')
    if (details === null) return
    try { await api.reportContent('video', post._id, 'inappropriate', details, csrfToken); setError('Report submitted privately to moderators.') }
    catch (requestError) { setError(requestError.message) }
  }

  if (error && !post) return <StatusMessage title="Could not load video" message={error} />
  if (!post) return <Loading text="Loading video..." />

  return (
    <div className="absolute left-0 top-0 flex w-full flex-wrap bg-white lg:flex-nowrap">
      <div className="bg-blurred-img relative flex w-[1000px] flex-2 items-center justify-center bg-cover bg-center bg-no-repeat lg:w-9/12">
        <button aria-label="Go back" className="absolute left-2 top-6 z-50 opacity-90 lg:left-6" onClick={() => navigate(-1)}>
          <MdOutlineCancel className="text-[35px] text-white" />
        </button>
        <div className="relative h-[60vh] lg:h-[100vh]">
          <video
            ref={videoRef}
            controls
            preload="metadata"
            src={mediaUrl(post.video.asset, quality)}
            onPlay={() => setPlaying(true)}
            onPause={() => setPlaying(false)}
            onEnded={loopVideo}
            poster={post.thumbnailUrl || undefined}
            className="h-full max-w-full cursor-pointer bg-black object-contain"
            aria-label={`Video: ${post.caption}`}
          >
            {post.subtitlesUrl && <track kind="captions" src={post.subtitlesUrl} srcLang="en" label="English" default />}
          </video>
          {!playing && (
            <button aria-label="Play video" onClick={togglePlayback} className="absolute left-[40%] top-[45%]">
              <BsFillPlayFill className="text-6xl text-white lg:text-8xl" />
            </button>
          )}
          {availableQualities(post.video.asset).length > 0 && <label className="video-quality-picker">Quality<select value={quality} onChange={(event) => { const time = videoRef.current?.currentTime || 0; setQuality(event.target.value); window.setTimeout(() => { if (videoRef.current) videoRef.current.currentTime = time }, 0) }}><option value="auto">Auto</option>{availableQualities(post.video.asset).map((value) => <option key={value} value={value}>{value}p</option>)}</select></label>}
        </div>
        <button aria-label={muted ? 'Unmute video' : 'Mute video'} onClick={() => setMuted((value) => !value)} className="absolute bottom-20 right-5 cursor-pointer lg:bottom-24 lg:right-10">
          {muted ? <HiVolumeOff className="text-2xl text-white lg:text-4xl" /> : <HiVolumeUp className="text-2xl text-white lg:text-4xl" />}
        </button>
      </div>

      <div className="relative w-[1000px] md:w-[900px] lg:w-[700px]">
        <div className="mt-10 lg:mt-20">
          <div className="flex gap-3 rounded p-2 font-semibold">
            <Link className="ml-4 h-16 w-16 md:h-20 md:w-20" to={`/profile/${encodeURIComponent(post.postedBy._id)}`}>
              <Avatar className="h-full w-full rounded-full object-cover" src={post.postedBy.image} alt="profile" />
            </Link>
            <Link to={`/profile/${encodeURIComponent(post.postedBy._id)}`}>
              <div className="mt-3 flex flex-col gap-2">
                <p className="flex items-center gap-2 font-bold text-primary">{post.postedBy.userName} <GoVerified className="text-blue-400" /></p>
                <p className="hidden text-xs font-medium capitalize text-gray-500 md:block">{post.postedBy.userName}</p>
              </div>
            </Link>
          </div>
          <p className="px-10 text-lg text-gray-600">
            <RichText text={post.caption} />
          </p>
          <div className="mt-6 flex items-center justify-between px-10">
            {userProfile ? (
              <LikeButton liked={liked} count={post.likes?.length} onToggle={toggleLike} disabled={pending === 'like'} />
            ) : (
              <p className="text-sm text-gray-500">Log in to like or comment.</p>
            )}
            <div className="detail-secondary-actions">
              {userProfile && <button type="button" aria-label={saved ? 'Remove from saved videos' : 'Save video'} onClick={toggleSaved}>{saved ? <BsBookmarkFill /> : <BsBookmark />} {saved ? 'Saved' : 'Save'}</button>}
              <button type="button" aria-label="Share video" onClick={share}><BsShare /> Share</button>
              {userProfile && userProfile._id !== post.userId && <button type="button" aria-label="Report video" onClick={reportVideo}><FiFlag /> Report</button>}
              <span>{post.viewCount || 0} views · {post.shareCount || 0} shares</span>
            </div>
            {userProfile?._id === post.userId && (
              <button aria-label="Delete video" disabled={pending === 'delete'} onClick={deletePost} className="flex items-center gap-1 rounded border border-red-300 px-3 py-2 text-sm text-red-600 disabled:opacity-50">
                <MdDeleteOutline /> {pending === 'delete' ? 'Deleting...' : 'Delete'}
              </button>
            )}
          </div>
          {error && <p className="mx-10 mt-2 text-sm text-red-600" role="alert">{error}</p>}
          <Comments
            comment={comment}
            setComment={setComment}
            addComment={addComment}
            pending={pending}
            comments={post.comments}
            postOwnerId={post.userId}
            onLikeComment={toggleCommentLike}
            onReply={addReply}
            onPin={togglePinnedComment}
            onEditComment={editComment}
            onDeleteComment={deleteComment}
            onEditReply={editReply}
            onDeleteReply={deleteReply}
          />
        </div>
      </div>
    </div>
  )
}

export default Detail
