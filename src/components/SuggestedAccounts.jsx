import { useEffect } from 'react'
import { GoVerified } from 'react-icons/go'
import { Link } from 'react-router-dom'

import useAuthStore from '../store/authStore'
import Avatar from './Avatar'

const SuggestedAccounts = () => {
  const { fetchAllUsers, allUsers } = useAuthStore()

  useEffect(() => {
    fetchAllUsers()
  }, [fetchAllUsers])

  return (
    <div className="border-gray-200 pb-4 xl:border-b-2">
      <p className="m-3 mt-4 hidden font-semibold text-gray-500 xl:block">Suggested Accounts</p>
      <div>
        {allUsers.slice(0, 6).map((user) => (
          <Link to={`/profile/${encodeURIComponent(user._id)}`} key={user._id}>
            <div className="suggested-account m-2 flex cursor-pointer gap-3 rounded p-2 font-semibold">
              <div className="h-8 w-8">
                <Avatar src={user.image} className="h-8 w-8 rounded-full object-cover" alt="profile" />
              </div>
              <div className="hidden xl:block">
                <p className="flex items-center gap-1 text-base font-bold lowercase text-primary">
                  {user.userName.replaceAll(' ', '')}
                  <GoVerified className="text-red-500" />
                </p>
                <p className="text-xs capitalize text-gray-400">{user.userName}</p>
              </div>
            </div>
          </Link>
        ))}
      </div>
    </div>
  )
}

export default SuggestedAccounts
