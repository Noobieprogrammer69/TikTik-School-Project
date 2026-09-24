import { useEffect, useState } from 'react'
import { GoVerified } from 'react-icons/go'
import { Link, useParams } from 'react-router-dom'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import NoResults from '../components/NoResults'
import StatusMessage from '../components/StatusMessage'
import VideoCard from '../components/VideoCard'

const Search = () => {
  const { searchTerm } = useParams()
  const [showAccounts, setShowAccounts] = useState(false)
  const [result, setResult] = useState(null)
  const [error, setError] = useState('')

  useEffect(() => {
    let active = true
    setResult(null)
    setError('')
    api.search(searchTerm)
      .then((data) => active && setResult(data))
      .catch((requestError) => active && setError(requestError.message))
    return () => { active = false }
  }, [searchTerm])

  if (error) return <StatusMessage title="Search failed" message={error} />
  if (!result) return <Loading text={`Searching for ${searchTerm}...`} />

  return (
    <div className="w-full">
      <div className="z-50 mb-10 flex w-full gap-10 border-b-2 border-gray-200 bg-white md:sticky md:top-0" role="tablist">
        <button role="tab" aria-selected={showAccounts} onClick={() => setShowAccounts(true)} className={`mt-2 text-xl font-semibold ${showAccounts ? 'border-b-2 border-black' : 'text-gray-400'}`}>Accounts</button>
        <button role="tab" aria-selected={!showAccounts} onClick={() => setShowAccounts(false)} className={`mt-2 text-xl font-semibold ${!showAccounts ? 'border-b-2 border-black' : 'text-gray-400'}`}>Videos</button>
      </div>
      {showAccounts ? (
        <div>
          {result.users.length ? result.users.map((user) => (
            <Link to={`/profile/${encodeURIComponent(user._id)}`} key={user._id}>
              <div className="flex cursor-pointer gap-3 rounded border-b-2 border-gray-200 p-2 font-semibold">
                <Avatar src={user.image} width="48" height="48" className="h-12 w-12 rounded-full object-cover" alt="profile" />
                <p className="flex items-center gap-1 text-[18px] font-bold leading-6 text-primary">{user.userName} <GoVerified className="text-red-500" /></p>
              </div>
            </Link>
          )) : <NoResults text={`No Account Results Found for ${searchTerm}`} />}
        </div>
      ) : (
        <div className="flex flex-wrap gap-6 md:justify-start">
          {result.videos.length ? result.videos.map((video) => <VideoCard post={video} key={video._id} />) : <NoResults text={`No Video Results Found for ${searchTerm}`} />}
        </div>
      )}
    </div>
  )
}

export default Search
