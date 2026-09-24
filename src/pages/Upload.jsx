import { useEffect, useRef, useState } from 'react'
import { BsBookmark, BsCameraVideo, BsCheckCircle, BsImage } from 'react-icons/bs'
import { FaCloudUploadAlt } from 'react-icons/fa'
import { useNavigate } from 'react-router-dom'

import { api } from '../api/client'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'
import { formatBytes, topics } from '../utils/constants'
import { clearUploadDraft, loadUploadDraft, saveUploadDraft } from '../utils/uploadDraft'

const acceptedTypes = ['video/mp4', 'video/webm', 'video/ogg', 'application/ogg']

const Upload = ({ maxUploadBytes }) => {
  const [selectedFile, setSelectedFile] = useState(null)
  const [previewUrl, setPreviewUrl] = useState('')
  const [caption, setCaption] = useState('')
  const [category, setCategory] = useState(topics[0].name)
  const [subtitles, setSubtitles] = useState(null)
  const [cover, setCover] = useState(null)
  const [coverUrl, setCoverUrl] = useState('')
  const [savingPost, setSavingPost] = useState(false)
  const [progress, setProgress] = useState(0)
  const [error, setError] = useState('')
  const [draftStatus, setDraftStatus] = useState('')
  const controller = useRef(null)
  const videoRef = useRef(null)
  const { userProfile, csrfToken, status } = useAuthStore()
  const navigate = useNavigate()

  useEffect(() => {
    loadUploadDraft().then((draft) => {
      if (!draft) return
      setSelectedFile(draft.video || null); setCaption(draft.caption || ''); setCategory(draft.category || topics[0].name)
      setSubtitles(draft.subtitles || null); setCover(draft.cover || null); setDraftStatus('Draft restored from this device.')
      if (draft.video) setPreviewUrl(URL.createObjectURL(draft.video))
      if (draft.cover) setCoverUrl(URL.createObjectURL(draft.cover))
    }).catch(() => {})
  }, [])

  useEffect(() => () => { if (previewUrl) URL.revokeObjectURL(previewUrl) }, [previewUrl])
  useEffect(() => () => { if (coverUrl) URL.revokeObjectURL(coverUrl) }, [coverUrl])
  useEffect(() => () => controller.current?.abort(), [])

  const selectVideo = (event) => {
    const file = event.target.files?.[0]; setError('')
    if (!file) return
    if (!acceptedTypes.includes(file.type)) { setError('Please select an MP4, WebM, or Ogg video file.'); return }
    if (file.size > maxUploadBytes) { setError(`The selected file is larger than ${formatBytes(maxUploadBytes)}.`); return }
    if (previewUrl) URL.revokeObjectURL(previewUrl)
    setSelectedFile(file); setPreviewUrl(URL.createObjectURL(file)); setProgress(0); setDraftStatus('')
  }

  const selectCover = (file) => {
    if (!file) return
    if (!['image/jpeg', 'image/png', 'image/webp'].includes(file.type) || file.size > 5 * 1024 * 1024) { setError('Cover must be JPEG, PNG, or WebP and at most 5 MiB.'); return }
    if (coverUrl) URL.revokeObjectURL(coverUrl)
    setCover(file); setCoverUrl(URL.createObjectURL(file)); setError('')
  }

  const captureCover = () => {
    const video = videoRef.current
    if (!video?.videoWidth) { setError('Play or seek the preview first, then capture the frame.'); return }
    const canvas = document.createElement('canvas'); canvas.width = video.videoWidth; canvas.height = video.videoHeight
    canvas.getContext('2d').drawImage(video, 0, 0)
    canvas.toBlob((blob) => blob && selectCover(new window.File([blob], 'cover.jpg', { type: 'image/jpeg' })), 'image/jpeg', 0.88)
  }

  const discard = async () => {
    controller.current?.abort()
    if (previewUrl) URL.revokeObjectURL(previewUrl)
    if (coverUrl) URL.revokeObjectURL(coverUrl)
    setSelectedFile(null); setPreviewUrl(''); setCaption(''); setCategory(topics[0].name); setSubtitles(null); setCover(null); setCoverUrl(''); setError(''); setProgress(0); setDraftStatus('')
    await clearUploadDraft().catch(() => {})
  }

  const saveDraft = async () => {
    if (!selectedFile) { setError('Select a video before saving a draft.'); return }
    setDraftStatus('Saving draft…')
    try { await saveUploadDraft({ video: selectedFile, caption, category, subtitles, cover, savedAt: new Date().toISOString() }); setDraftStatus('Draft saved privately on this device.') }
    catch (draftError) { setError(draftError.message) }
  }

  const submit = async () => {
    if (!selectedFile || !caption.trim()) { setError('Choose a video and enter a caption before posting.'); return }
    const form = new FormData(); form.append('video', selectedFile); form.append('caption', caption.trim()); form.append('topic', category)
    if (subtitles) form.append('subtitles', subtitles)
    if (cover) form.append('cover', cover)
    controller.current = new window.AbortController(); setSavingPost(true); setProgress(0); setError('')
    try {
      const post = await api.upload(form, csrfToken, { signal: controller.current.signal, onProgress: setProgress })
      await clearUploadDraft().catch(() => {}); navigate(`/detail/${encodeURIComponent(post._id)}`)
    } catch (uploadError) { setError(uploadError.message) }
    finally { setSavingPost(false); controller.current = null }
  }

  if (status === 'loading') return <p className="m-auto text-gray-500">Checking your login...</p>
  if (!userProfile) return <StatusMessage title="Login required" message="Use the Google login button before uploading a video." />

  return (
    <section className="upload-page">
      <header className="page-heading"><p className="eyebrow">Creator studio</p><h1>Upload Video</h1><p>Preview your post, choose a cover, add accessible captions, or save it as a private draft.</p></header>
      <div className="upload-workspace">
        <div className="upload-preview-panel">
          <label className={`upload-dropzone ${previewUrl ? 'has-preview' : ''}`}>
            {previewUrl ? <video ref={videoRef} src={previewUrl} controls className="upload-video-preview" aria-label="Upload preview" onLoadedMetadata={(event) => { if (event.currentTarget.duration > 600) setError('Videos must be 10 minutes or shorter.') }} /> : <div className="upload-empty"><FaCloudUploadAlt /><h2>Select a video</h2><p>MP4, WebM, or Ogg · Up to 10 minutes<br />Maximum {formatBytes(maxUploadBytes)}</p><span>Browse files</span></div>}
            <input type="file" name="video" accept="video/mp4,video/webm,video/ogg,.ogv" onChange={selectVideo} disabled={savingPost} hidden />
          </label>
          {selectedFile && <div className="selected-file"><BsCameraVideo /><span><strong>{selectedFile.name}</strong><small>{formatBytes(selectedFile.size)}</small></span><BsCheckCircle /></div>}
        </div>

        <div className="upload-form-panel">
          <label htmlFor="caption">Caption <span>{caption.length}/150</span></label>
          <textarea id="caption" aria-label="Caption" rows={4} maxLength={150} value={caption} onChange={(event) => setCaption(event.target.value)} placeholder="Describe your video. Use #hashtags and @mentions…" />
          <label htmlFor="category">Choose A Category</label><select id="category" value={category} onChange={(event) => setCategory(event.target.value)}>{topics.map((topic) => <option key={topic.name} value={topic.name}>{topic.name}</option>)}</select>
          <div className="upload-cover-section"><div><label>Video cover</label><p>Upload an image or seek the preview and capture the current frame.</p></div>{coverUrl && <img src={coverUrl} alt="Selected video cover" />}<div className="upload-inline-actions"><label className="secondary-button"><BsImage /> Choose image<input type="file" accept="image/jpeg,image/png,image/webp" onChange={(event) => selectCover(event.target.files?.[0])} hidden /></label><button type="button" className="secondary-button" onClick={captureCover} disabled={!selectedFile}>Capture frame</button></div></div>
          <label htmlFor="subtitles">Captions (optional)</label><input id="subtitles" type="file" accept="text/vtt,.vtt" onChange={(event) => setSubtitles(event.target.files?.[0] || null)} />
          <p className="upload-help">Captions improve accessibility. The server validates your files, creates playback sizes, and keeps large videos outside MongoDB.</p>
          {savingPost && <div className="upload-progress-wrap" aria-live="polite"><div className="upload-progress-label"><span>{progress < 100 ? 'Uploading' : 'Processing video'}</span><strong>{progress}%</strong></div><progress max="100" value={progress} /><p>{progress < 100 ? 'Keep this page open while the file uploads.' : 'Creating optimized playback versions and thumbnail…'}</p></div>}
          {draftStatus && <p className="draft-status" role="status">{draftStatus}</p>}
          {error && <p className="upload-error" role="alert">{error}</p>}
          <div className="upload-actions"><button onClick={discard} type="button" disabled={savingPost} className="secondary-button">Discard</button><button onClick={saveDraft} type="button" disabled={savingPost || !selectedFile} className="secondary-button"><BsBookmark /> Save draft</button>{savingPost ? <button onClick={() => controller.current?.abort()} type="button" className="danger-button">Cancel upload</button> : <button onClick={submit} type="button" className="primary-button" aria-label="Post" disabled={!selectedFile || !caption.trim()}>Post video</button>}</div>
        </div>
      </div>
    </section>
  )
}

export default Upload
