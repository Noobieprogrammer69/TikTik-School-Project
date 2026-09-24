import { useEffect, useMemo, useState } from 'react'
import { BsCheck2Circle, BsDownload, BsLaptop, BsShieldCheck, BsTrash } from 'react-icons/bs'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'

const preferenceLabels = {
  follow: 'New followers', follow_request: 'Follow requests', follow_accepted: 'Accepted follow requests', new_post: 'Videos from followed accounts',
  post_like: 'Video likes', comment: 'New comments', reply: 'Comment replies', mention: 'Mentions',
  comment_like: 'Comment likes', comment_pin: 'Pinned comments', post_share: 'Video shares', message: 'Private messages',
}

const Settings = () => {
  const { userProfile, status, csrfToken, setUserProfile, logout } = useAuthStore()
  const [name, setName] = useState('')
  const [bio, setBio] = useState('')
  const [preferences, setPreferences] = useState(null)
  const [privacy, setPrivacy] = useState(null)
  const [followRequests, setFollowRequests] = useState([])
  const [sessions, setSessions] = useState([])
  const [securityEvents, setSecurityEvents] = useState([])
  const [analytics, setAnalytics] = useState(null)
  const [saving, setSaving] = useState('')
  const [message, setMessage] = useState('')
  const [profileSaved, setProfileSaved] = useState(false)
  const [deleteText, setDeleteText] = useState('')
  const [installPrompt, setInstallPrompt] = useState(null)

  const profileChanged = useMemo(() => (
    name.trim() !== (userProfile?.userName || '') || bio.trim() !== (userProfile?.bio || '')
  ), [bio, name, userProfile])

  useEffect(() => {
    if (!userProfile) return
    setName(userProfile.userName || '')
    setBio(userProfile.bio || '')
    Promise.all([api.notificationPreferences(), api.privacy(), api.followRequests(), api.sessions(), api.securityEvents(), api.analytics()])
      .then(([nextPreferences, nextPrivacy, requests, nextSessions, events, nextAnalytics]) => {
        setPreferences(nextPreferences); setPrivacy(nextPrivacy); setFollowRequests(requests)
        setSessions(nextSessions); setSecurityEvents(events); setAnalytics(nextAnalytics)
      })
      .catch((error) => setMessage(error.message))
  }, [userProfile])

  useEffect(() => {
    const rememberPrompt = (event) => { event.preventDefault(); setInstallPrompt(event) }
    window.addEventListener('beforeinstallprompt', rememberPrompt)
    return () => window.removeEventListener('beforeinstallprompt', rememberPrompt)
  }, [])

  if (status === 'loading') return <Loading text="Checking your login..." />
  if (!userProfile) return <StatusMessage title="Login required" message="Sign in to edit your account." />

  const saveProfile = async (event) => {
    event.preventDefault(); setSaving('profile'); setProfileSaved(false); setMessage('')
    try {
      const updated = await api.updateProfile(name.trim(), bio.trim(), csrfToken)
      setUserProfile(updated); setMessage('Profile saved.'); setProfileSaved(true)
      window.setTimeout(() => setProfileSaved(false), 2200)
    } catch (error) { setMessage(error.message) } finally { setSaving('') }
  }

  const uploadAvatar = async (event) => {
    const file = event.target.files?.[0]
    if (!file) return
    const form = new FormData(); form.append('avatar', file); setSaving('avatar'); setMessage('')
    try { const updated = await api.updateAvatar(form, csrfToken); setUserProfile(updated); setMessage('Profile photo updated.') }
    catch (error) { setMessage(error.message) }
    finally { setSaving(''); event.target.value = '' }
  }

  const togglePreference = async (key) => {
    const previous = preferences; const updated = { ...preferences, [key]: !preferences[key] }; setPreferences(updated)
    try { setPreferences(await api.updateNotificationPreferences(updated, csrfToken)) }
    catch (error) { setPreferences(previous); setMessage(error.message) }
  }

  const updatePrivacy = async (updates) => {
    const previous = privacy; const updated = { ...privacy, ...updates }; setPrivacy(updated); setSaving('privacy')
    try { setPrivacy(await api.updatePrivacy(updated, csrfToken)); setMessage('Privacy settings saved.') }
    catch (error) { setPrivacy(previous); setMessage(error.message) }
    finally { setSaving('') }
  }

  const respondRequest = async (requesterId, accept) => {
    setSaving(`request:${requesterId}`)
    try { await api.respondFollowRequest(requesterId, accept, csrfToken); setFollowRequests((items) => items.filter((item) => item.user._id !== requesterId)) }
    catch (error) { setMessage(error.message) } finally { setSaving('') }
  }

  const exportData = async () => {
    setSaving('export')
    try {
      const data = await api.exportAccount()
      const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }))
      const link = document.createElement('a'); link.href = url; link.download = 'tiktik-account-export.json'; link.click(); URL.revokeObjectURL(url)
    } catch (error) { setMessage(error.message) } finally { setSaving('') }
  }

  const enableNotifications = async () => {
    if (!('Notification' in window)) { setMessage('This browser does not support desktop notifications.'); return }
    const permission = await window.Notification.requestPermission()
    setMessage(permission === 'granted' ? 'Desktop notifications enabled while TikTik is open.' : 'Notification permission was not granted.')
  }

  const removeAccount = async () => {
    if (deleteText !== 'DELETE' || !window.confirm('Permanently delete your account, videos, messages, and social activity?')) return
    setSaving('delete')
    try { await api.deleteAccount(deleteText, csrfToken); await logout() }
    catch (error) { setMessage(error.message); setSaving('') }
  }

  return (
    <section className="settings-page">
      <header className="page-heading"><p className="eyebrow">Your account</p><h1>Settings & creator center</h1></header>
      <div className={`settings-card profile-settings-grid ${saving ? 'settings-card-busy' : ''}`} aria-busy={Boolean(saving)}>
        {saving && <div className="settings-progress" role="progressbar" aria-label={saving === 'profile' ? 'Saving profile' : 'Saving account changes'} />}
        <div className="avatar-editor">
          <div className="settings-avatar-wrap"><Avatar src={userProfile.image} className="settings-avatar" alt="Your profile" />{saving === 'avatar' && <span className="avatar-upload-overlay"><span className="button-spinner" /></span>}</div>
          <label className={`secondary-button ${saving ? 'button-disabled' : ''}`}>{saving === 'avatar' ? 'Uploading...' : 'Change photo'}<input type="file" accept="image/jpeg,image/png,image/webp" onChange={uploadAvatar} disabled={Boolean(saving)} hidden /></label>
          <small>JPEG, PNG, or WebP. Maximum 5 MiB.</small>
        </div>
        <form onSubmit={saveProfile} className="settings-form">
          <label htmlFor="settings-name">Display name</label><input id="settings-name" minLength={2} maxLength={50} value={name} onChange={(event) => setName(event.target.value)} disabled={saving === 'profile'} required />
          <label htmlFor="settings-bio">Bio</label><textarea id="settings-bio" maxLength={160} rows={4} value={bio} onChange={(event) => setBio(event.target.value)} disabled={saving === 'profile'} />
          <div className="settings-form-meta"><small>{bio.length}/160</small>{profileChanged && !saving && <small className="unsaved-indicator">Unsaved changes</small>}</div>
          <button className={`primary-button ${profileSaved ? 'save-complete' : ''}`} disabled={saving === 'profile' || !profileChanged}>{saving === 'profile' ? <><span className="button-spinner" /> Saving changes...</> : profileSaved ? <><BsCheck2Circle /> Saved</> : 'Save profile'}</button>
        </form>
      </div>

      <div className="settings-two-column">
        <div className="settings-card"><h2>Privacy</h2><p className="settings-help">Choose who can find and interact with you.</p>{privacy ? <div className="preference-list">
          <label className="preference-row"><span>Private account<small>Approve new followers before they can see your videos.</small></span><input type="checkbox" checked={privacy.privateAccount} onChange={(event) => updatePrivacy({ privateAccount: event.target.checked })} /></label>
          <label className="preference-row"><span>Show liked videos</span><input type="checkbox" checked={privacy.showLikedVideos} onChange={(event) => updatePrivacy({ showLikedVideos: event.target.checked })} /></label>
          <label className="preference-row"><span>Messages</span><select value={privacy.allowMessages} onChange={(event) => updatePrivacy({ allowMessages: event.target.value })}><option value="everyone">Everyone</option><option value="followers">People you follow</option><option value="none">No one</option></select></label>
          <label className="preference-row"><span>Comments</span><select value={privacy.allowComments} onChange={(event) => updatePrivacy({ allowComments: event.target.value })}><option value="everyone">Everyone</option><option value="followers">People you follow</option><option value="none">No one</option></select></label>
        </div> : <Loading text="Loading privacy..." />}</div>
        <div className="settings-card"><h2>Follow requests</h2><p className="settings-help">Requests appear here when your account is private.</p>{followRequests.length ? followRequests.map((request) => <div className="request-row" key={request.requester._id}><Avatar src={request.requester.image} className="request-avatar" alt="" /><strong>{request.requester.userName}</strong><button onClick={() => respondRequest(request.requester._id, true)} disabled={saving === `request:${request.requester._id}`}>Accept</button><button className="secondary-button" onClick={() => respondRequest(request.requester._id, false)}>Decline</button></div>) : <p className="settings-empty">No pending requests.</p>}</div>
      </div>

      <div className="settings-card"><h2>Notification preferences</h2><div className="preference-grid">{preferences ? Object.entries(preferenceLabels).map(([key, label]) => <label key={key} className="preference-row"><span>{label}</span><input type="checkbox" checked={Boolean(preferences[key])} onChange={() => togglePreference(key)} /></label>) : <Loading text="Loading preferences..." />}</div></div>

      {analytics && <div className="settings-card"><h2>Creator analytics</h2><div className="analytics-grid">{Object.entries(analytics.totals || {}).map(([key, value]) => <div key={key}><strong>{Number(value).toLocaleString()}</strong><span>{key}</span></div>)}</div></div>}

      <div className="settings-two-column">
        <div className="settings-card"><h2><BsLaptop /> Active sessions</h2>{sessions.map((session) => <div className="compact-row" key={session.id}><span>{session.current ? 'This device' : 'Signed-in device'}<small>Expires {new Date(session.expiresAt).toLocaleString()}</small></span>{session.current && <strong>Current</strong>}</div>)}<button className="secondary-button" onClick={async () => { const result = await api.revokeOtherSessions(csrfToken); setSessions((items) => items.filter((item) => item.current)); setMessage(`${result.revoked} other session(s) signed out.`) }}>Sign out other devices</button></div>
        <div className="settings-card"><h2><BsShieldCheck /> Security activity</h2>{securityEvents.length ? securityEvents.slice(0, 6).map((event, index) => <div className="compact-row" key={`${event.createdAt}-${index}`}><span>{event.type}<small>{event.detail || 'Account security event'}</small></span><small>{new Date(event.createdAt).toLocaleString()}</small></div>) : <p className="settings-empty">No recent security alerts.</p>}</div>
      </div>

      <div className="settings-card"><h2>App & your data</h2><div className="settings-action-grid"><button className="secondary-button" onClick={enableNotifications}>Enable desktop notifications</button>{installPrompt && <button className="secondary-button" onClick={async () => { await installPrompt.prompt(); setInstallPrompt(null) }}>Install TikTik app</button>}<button className="secondary-button" onClick={exportData} disabled={saving === 'export'}><BsDownload /> {saving === 'export' ? 'Preparing...' : 'Download my data'}</button></div></div>

      <div className="settings-card danger-zone"><h2><BsTrash /> Delete account</h2><p>This permanently deletes your profile, videos, messages, follows, and activity. This cannot be undone.</p><div className="delete-account-row"><input value={deleteText} onChange={(event) => setDeleteText(event.target.value)} placeholder="Type DELETE" aria-label="Type DELETE to confirm" /><button onClick={removeAccount} disabled={deleteText !== 'DELETE' || saving === 'delete'}>{saving === 'delete' ? 'Deleting...' : 'Delete permanently'}</button></div></div>
      {message && <p className="settings-status" role="status">{message}</p>}
    </section>
  )
}

export default Settings
