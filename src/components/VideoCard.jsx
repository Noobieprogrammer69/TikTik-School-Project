import { useEffect, useRef, useState } from 'react'
import { BsFillPauseFill, BsFillPlayFill, BsThreeDots } from 'react-icons/bs'
import { GoVerified } from 'react-icons/go'
import { HiVolumeOff, HiVolumeUp } from 'react-icons/hi'
import { Link } from 'react-router-dom'

import { api } from '../api/client'
import useAuthStore from '../store/authStore'
import Avatar from './Avatar'
import RichText from './RichText'
import { mediaUrl } from '../utils/media'

const VideoCard = ({ post, onNotInterested, onHideCreator }) => {
  const [isHover, setIsHover] = useState(false)
  const [playing, setPlaying] = useState(false)
  const [isVideoMuted, setIsVideoMuted] = useState(false)
  const [showMenu, setShowMenu] = useState(false)
  const [viewCount, setViewCount] = useState(post.viewCount || 0)
  const videoRef = useRef(null)
  const { csrfToken } = useAuthStore()

  const recordView = (watchSeconds = 0) => {
    api.recordView(post._id, watchSeconds, csrfToken)
      .then((result) => setViewCount(result.viewCount))
      .catch(() => {})
  }

  const togglePlayback = async () => {
    if (!videoRef.current) return
    if (!videoRef.current.paused) {
      videoRef.current.pause()
      setPlaying(false)
    } else {
      try {
        await videoRef.current.play()
        setPlaying(true)
        recordView(Math.floor(videoRef.current.currentTime || 0))
      } catch {
        setPlaying(false)
      }
    }
  }

  useEffect(() => {
    if (videoRef.current) videoRef.current.muted = isVideoMuted
  }, [isVideoMuted])

  useEffect(() => {
    setViewCount(post.viewCount || 0)
  }, [post._id, post.viewCount])

  const loopVideo = () => {
    const video = videoRef.current
    if (!video) return
    recordView(Math.floor(video.duration || video.currentTime || 0))
    video.currentTime = 0
    video.play()
      .then(() => setPlaying(true))
      .catch(() => setPlaying(false))
  }

  return (
    <article className="flex flex-col border-b-2 border-gray-200 pb-6" data-testid="video-card">
      <div className="flex cursor-pointer gap-3 rounded p-2 font-semibold">
        <Link className="h-10 w-10 md:h-16 md:w-16" to={`/profile/${encodeURIComponent(post.postedBy._id)}`}>
          <Avatar
            className="h-full w-full rounded-full object-cover"
            src={post.postedBy.image}
            alt={`${post.postedBy.userName} profile`}
          />
        </Link>
        <div>
          <Link to={`/profile/${encodeURIComponent(post.postedBy._id)}`}>
            <div className="flex items-center gap-2">
              <p className="flex items-center gap-2 text-base font-bold text-primary">
                {post.postedBy.userName} <GoVerified className="text-base text-blue-400" />
              </p>
              <p className="hidden text-xs font-medium capitalize text-gray-500 md:block">
                {post.postedBy.userName}
              </p>
            </div>
          </Link>
          <p className="mt-1 max-w-[600px] text-sm font-normal text-gray-700"><RichText text={post.caption} /></p>
          {post.recommendationReason && <small className="recommendation-reason">{post.recommendationReason}</small>}
        </div>
        {(onNotInterested || onHideCreator) && <div className="video-card-menu-wrap"><button type="button" className="video-card-menu-button" aria-label="Video options" onClick={() => setShowMenu((value) => !value)}><BsThreeDots /></button>{showMenu && <div className="video-card-menu">{onNotInterested && <button type="button" onClick={() => { setShowMenu(false); onNotInterested(post) }}>Not interested</button>}{onHideCreator && <button type="button" onClick={() => { setShowMenu(false); onHideCreator(post) }}>Hide videos from this creator</button>}</div>}</div>}
      </div>

      <div className="relative flex gap-4 lg:ml-20">
        <div
          className="relative rounded-3xl"
          onMouseEnter={() => setIsHover(true)}
          onMouseLeave={() => setIsHover(false)}
        >
          <video
            ref={videoRef}
            src={mediaUrl(post.video.asset)}
            poster={post.thumbnailUrl || undefined}
            preload="metadata"
            onClick={togglePlayback}
            onEnded={loopVideo}
            aria-label={`Video: ${post.caption}`}
            className="h-[300px] w-[200px] cursor-pointer rounded-2xl bg-gray-100 object-contain md:h-[400px] lg:h-[528px] lg:w-[600px]"
          >
            {post.subtitlesUrl && <track kind="captions" src={post.subtitlesUrl} srcLang="en" label="English" default />}
          </video>

          {isHover && (
            <div className="absolute bottom-6 left-8 flex w-[100px] justify-between gap-10 p-3 md:left-14 lg:left-0 lg:w-[600px]">
              <button aria-label={playing ? 'Pause video' : 'Play video'} onClick={togglePlayback}>
                {playing ? (
                  <BsFillPauseFill className="text-2xl text-black lg:text-4xl" />
                ) : (
                  <BsFillPlayFill className="text-2xl text-black lg:text-4xl" />
                )}
              </button>
              <button
                aria-label={isVideoMuted ? 'Unmute video' : 'Mute video'}
                onClick={() => setIsVideoMuted((muted) => !muted)}
              >
                {isVideoMuted ? (
                  <HiVolumeOff className="text-2xl text-black lg:text-4xl" />
                ) : (
                  <HiVolumeUp className="text-2xl text-black lg:text-4xl" />
                )}
              </button>
            </div>
          )}
          <Link
            className="absolute right-3 top-3 rounded-full bg-black/60 px-3 py-1 text-sm text-white"
            to={`/detail/${encodeURIComponent(post._id)}`}
          >
            View
          </Link>
          <div className="video-card-stats" aria-label="Video statistics">
            <span>{viewCount} views</span><span>{post.shareCount || 0} shares</span>
          </div>
        </div>
      </div>
    </article>
  )
}

export default VideoCard
