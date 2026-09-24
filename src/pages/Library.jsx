import { useEffect, useState } from 'react'

import { api } from '../api/client'
import Loading from '../components/Loading'
import NoResults from '../components/NoResults'
import StatusMessage from '../components/StatusMessage'
import VideoCard from '../components/VideoCard'
import useAuthStore from '../store/authStore'

const Library = () => {
  const { userProfile, status } = useAuthStore()
  const [tab, setTab] = useState('saved')
  const [items, setItems] = useState(null)
  const [error, setError] = useState('')

  useEffect(() => {
    if (!userProfile) return
    setItems(null)
    setError('')
    const request = tab === 'saved' ? api.savedVideos() : api.history()
    request.then(setItems).catch((requestError) => setError(requestError.message))
  }, [tab, userProfile])

  if (status === 'loading') return <Loading text="Checking your login..." />
  if (!userProfile) return <StatusMessage title="Login required" message="Sign in to see your private library." />
  if (error) return <StatusMessage title="Could not load your library" message={error} />

  return (
    <section className="library-page">
      <header className="page-heading"><p className="eyebrow">Private to you</p><h1>Your library</h1></header>
      <div className="segmented-tabs" role="tablist">
        <button role="tab" aria-selected={tab === 'saved'} onClick={() => setTab('saved')}>Saved</button>
        <button role="tab" aria-selected={tab === 'history'} onClick={() => setTab('history')}>Watch history</button>
      </div>
      {!items ? <Loading text="Loading videos..." /> : items.length ? (
        <div className="videos flex flex-col gap-10">{items.map((post) => <VideoCard key={post._id} post={post} />)}</div>
      ) : <NoResults text={tab === 'saved' ? 'No Saved Videos Yet' : 'No Watch History Yet'} />}
    </section>
  )
}

export default Library
