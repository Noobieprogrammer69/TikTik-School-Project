import { useEffect, useState } from 'react'
import { BsBellSlash, BsChatDots, BsGear, BsPersonCheck, BsPersonPlus, BsSlashCircle } from 'react-icons/bs'
import { FiFlag } from 'react-icons/fi'
import { GoVerified } from 'react-icons/go'
import { Link, useParams } from 'react-router-dom'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import NoResults from '../components/NoResults'
import StatusMessage from '../components/StatusMessage'
import VideoCard from '../components/VideoCard'
import useAuthStore from '../store/authStore'

const Profile = () => {
  const { id } = useParams()
  const { userProfile, csrfToken } = useAuthStore()
  const [data, setData] = useState(null)
  const [showUserVideos, setShowUserVideos] = useState(true)
  const [error, setError] = useState('')
  const [socialError, setSocialError] = useState('')
  const [attempt, setAttempt] = useState(0)
  const [relationship, setRelationship] = useState(null)
  const [safety, setSafety] = useState(null)
  const [socialPending, setSocialPending] = useState(false)
  const [showReport, setShowReport] = useState(false)
  const [reportReason, setReportReason] = useState('spam')
  const [reportDetails, setReportDetails] = useState('')
  const [reportStatus, setReportStatus] = useState('')

  useEffect(() => {
    let active = true
    setData(null)
    setError('')
    api.profile(id)
      .then((result) => active && setData(result))
      .catch((requestError) => active && setError(requestError.message))
    return () => { active = false }
  }, [id, attempt])

  useEffect(() => {
    if (!userProfile || userProfile._id === id) {
      setRelationship(null)
      return
    }
    let active = true
    setRelationship(null)
    setSafety(null)
    setSocialError('')
    Promise.all([api.relationship(id), api.safety(id)])
      .then(([nextRelationship, nextSafety]) => {
        if (active) {
          setRelationship(nextRelationship)
          setSafety(nextSafety)
        }
      })
      .catch((requestError) => active && setSocialError(requestError.message))
    return () => { active = false }
  }, [id, userProfile])

  const toggleFollow = async () => {
    if (!relationship || socialPending) return
    setSocialPending(true)
    setSocialError('')
    try {
      setRelationship(await api.setFollow(id, !(relationship.following || relationship.requested), csrfToken))
    } catch (requestError) {
      setSocialError(requestError.message)
    } finally {
      setSocialPending(false)
    }
  }

  const submitReport = async (event) => {
    event.preventDefault()
    setSocialPending(true)
    setReportStatus('')
    try {
      await api.reportUser(id, reportReason, reportDetails.trim(), csrfToken)
      setReportStatus('Report submitted. Thank you for helping keep TikTik safe.')
      setReportDetails('')
    } catch (requestError) {
      setReportStatus(requestError.message)
    } finally {
      setSocialPending(false)
    }
  }

  const toggleSafety = async (kind) => {
    if (!safety || socialPending) return
    setSocialPending(true)
    setSocialError('')
    try {
      setSafety(kind === 'block'
        ? await api.setBlocked(id, !safety.blocked, csrfToken)
        : await api.setMuted(id, !safety.muted, csrfToken))
      if (kind === 'block' && !safety.blocked) {
        setRelationship((current) => current ? { ...current, following: false } : current)
      }
    } catch (requestError) {
      setSocialError(requestError.message)
    } finally {
      setSocialPending(false)
    }
  }

  if (error) return <StatusMessage title="Could not load profile" message={error} onRetry={() => setAttempt((value) => value + 1)} />
  if (!data) return <Loading text="Loading profile..." />

  const { user, userVideos, userLikedVideos } = data
  const videosList = showUserVideos ? userVideos : userLikedVideos
  const followerCount = relationship?.followerCount ?? data.followerCount ?? 0
  const followingCount = relationship?.followingCount ?? data.followingCount ?? 0

  return (
    <div className="w-full">
      <div className="profile-hero">
        <Avatar className="h-16 w-16 rounded-full object-cover md:h-32 md:w-32" src={user.image} alt="user profile" />
        <div className="flex flex-col justify-center">
          <div className="flex items-center gap-2 text-base font-bold lowercase tracking-wider md:text-2xl">
            <span>{user.userName.replaceAll(' ', '')}</span>
            <GoVerified className="text-base text-blue-400 md:text-xl" />
          </div>
          <p className="text-sm font-medium">{user.userName}</p>
          {user.bio && <p className="profile-bio">{user.bio}</p>}
          <div className="profile-counts">
            <span><strong>{followerCount}</strong> Followers</span>
            <span><strong>{followingCount}</strong> Following</span>
          </div>
          {userProfile && userProfile._id !== user._id && (
            <div className="profile-actions">
              <button
                type="button"
                className={`profile-follow-button ${relationship?.following || relationship?.requested ? 'profile-following' : ''}`}
                disabled={!relationship || socialPending || safety?.blocked || safety?.blockedBy}
                onClick={toggleFollow}
              >
                {relationship?.following || relationship?.requested ? <BsPersonCheck /> : <BsPersonPlus />}
                {relationship?.following ? 'Following' : relationship?.requested ? 'Requested' : 'Follow'}
              </button>
              <Link to={`/messages/${encodeURIComponent(user._id)}`} onClick={(event) => { if (safety?.blocked || safety?.blockedBy) event.preventDefault() }} aria-disabled={safety?.blocked || safety?.blockedBy} className={`profile-message-button ${safety?.blocked || safety?.blockedBy ? 'disabled-link' : ''}`}>
                <BsChatDots /> Message
              </Link>
              <button type="button" className={`profile-report-button ${safety?.muted ? 'active' : ''}`} aria-label={safety?.muted ? 'Unmute account' : 'Mute account'} onClick={() => toggleSafety('mute')}>
                <BsBellSlash />
              </button>
              <button type="button" className={`profile-report-button ${safety?.blocked ? 'active danger-button' : ''}`} aria-label={safety?.blocked ? 'Unblock account' : 'Block account'} onClick={() => toggleSafety('block')}>
                <BsSlashCircle />
              </button>
              <button type="button" className="profile-report-button" onClick={() => setShowReport(true)}>
                <FiFlag /> <span className="sr-only">Report account</span>
              </button>
            </div>
          )}
          {userProfile?._id === user._id && (
            <Link to="/settings" className="profile-message-button"><BsGear /> Edit profile</Link>
          )}
          {socialError && <p className="profile-social-error" role="alert">{socialError}</p>}
        </div>
      </div>
      <div className="profile-tabs" role="tablist">
        <button role="tab" aria-selected={showUserVideos} className={showUserVideos ? 'profile-tab-active' : ''} onClick={() => setShowUserVideos(true)}>Videos</button>
        {(user.showLikedVideos || userProfile?._id === user._id) && <button role="tab" aria-selected={!showUserVideos} className={!showUserVideos ? 'profile-tab-active' : ''} onClick={() => setShowUserVideos(false)}>Liked</button>}
      </div>
      <div className="flex flex-wrap gap-6 md:justify-start">
        {videosList.length ? videosList.map((post) => <VideoCard key={post._id} post={post} />) : <NoResults text={data.privateContentHidden ? 'This Account Is Private' : `No ${showUserVideos ? '' : 'Liked '}Videos Yet`} />}
      </div>
      {showReport && (
        <div className="modal-backdrop" role="presentation" onMouseDown={() => setShowReport(false)}>
          <div className="report-dialog" role="dialog" aria-modal="true" aria-labelledby="report-title" onMouseDown={(event) => event.stopPropagation()}>
            <h2 id="report-title">Report {user.userName}</h2>
            <p>Reports are private. The reported account will not be notified.</p>
            <form onSubmit={submitReport}>
              <label htmlFor="report-reason">Reason</label>
              <select id="report-reason" value={reportReason} onChange={(event) => setReportReason(event.target.value)}>
                <option value="spam">Spam</option>
                <option value="harassment">Harassment</option>
                <option value="impersonation">Impersonation</option>
                <option value="inappropriate">Inappropriate content</option>
                <option value="other">Other</option>
              </select>
              <label htmlFor="report-details">Additional details (optional)</label>
              <textarea id="report-details" maxLength={500} value={reportDetails} onChange={(event) => setReportDetails(event.target.value)} rows={4} />
              <small>{reportDetails.length}/500</small>
              {reportStatus && <p className="report-status" role="status">{reportStatus}</p>}
              <div className="report-actions">
                <button type="button" onClick={() => setShowReport(false)}>Cancel</button>
                <button type="submit" disabled={socialPending}>{socialPending ? 'Submitting...' : 'Submit report'}</button>
              </div>
            </form>
          </div>
        </div>
      )}
    </div>
  )
}

export default Profile
