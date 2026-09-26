import { GoogleLogin, googleLogout } from '@react-oauth/google'
import { useMemo, useState } from 'react'
import { AiOutlineLogout } from 'react-icons/ai'
import { BiSearch } from 'react-icons/bi'
import { BsChatDots } from 'react-icons/bs'
import { IoMdAdd } from 'react-icons/io'
import { IoNotificationsOutline } from 'react-icons/io5'
import { Link, useNavigate } from 'react-router-dom'

import useAuthStore from '../store/authStore'
import Avatar from './Avatar'
import ThemeToggle from './ThemeToggle'

const Navbar = ({ googleLoginEnabled }) => {
  const {
    userProfile, status, authError, loginWithGoogle, logout, unreadCount, notificationCount, allUsers,
  } = useAuthStore()
  const [searchValue, setSearchValue] = useState('')
  const [searchOpen, setSearchOpen] = useState(false)
  const [recentSearches, setRecentSearches] = useState(() => {
    try { return JSON.parse(localStorage.getItem('tiktik-searches') || '[]') }
    catch { return [] }
  })
  const navigate = useNavigate()
  const suggestions = useMemo(() => {
    const term = searchValue.trim().toLowerCase()
    if (!term) return recentSearches
    return [...new Set([...allUsers.map((user) => user.userName), ...recentSearches])]
      .filter((value) => value.toLowerCase().includes(term)).slice(0, 6)
  }, [allUsers, recentSearches, searchValue])

  const submitSearch = (value) => {
    const term = value.trim()
    if (!term) return
    const next = [term, ...recentSearches.filter((item) => item.toLowerCase() !== term.toLowerCase())].slice(0, 8)
    setRecentSearches(next); localStorage.setItem('tiktik-searches', JSON.stringify(next)); setSearchOpen(false)
    navigate(`/search/${encodeURIComponent(term)}`)
  }

  const handleSearch = (event) => {
    event.preventDefault()
    submitSearch(searchValue)
  }

  const handleLogout = async () => {
    try {
      await logout()
    } finally {
      googleLogout()
      navigate('/')
    }
  }

  return (
    <header className="app-navbar">
      <div className="flex w-full items-center justify-between gap-3">
        <Link to="/" aria-label="RippleNest home" className="brand-link">
          <div className="brand-logo"><img src="/app-icon.svg" alt="" /></div>
          <span className="hidden sm:block">RippleNest</span>
        </Link>

        <div className="relative hidden flex-1 md:block md:max-w-md">
          <form onSubmit={handleSearch} onFocus={() => setSearchOpen(true)} onBlur={() => window.setTimeout(() => setSearchOpen(false), 150)}>
            <input
              type="search"
              value={searchValue}
              onChange={(event) => setSearchValue(event.target.value)}
              placeholder="Search accounts and videos"
              aria-label="Search accounts and videos"
              className="navbar-search"
            />
            <button type="submit" aria-label="Search" className="navbar-search-button">
              <BiSearch />
            </button>
            {searchOpen && suggestions.length > 0 && <div className="search-suggestions" role="listbox">{suggestions.map((suggestion) => <button type="button" role="option" key={suggestion} onMouseDown={() => submitSearch(suggestion)}><BiSearch /><span>{suggestion}</span></button>)}{recentSearches.length > 0 && <button type="button" className="clear-search-history" onMouseDown={() => { setRecentSearches([]); localStorage.removeItem('tiktik-searches') }}>Clear search history</button>}</div>}
          </form>
        </div>

        <div className="flex items-center gap-2 md:gap-3">
          <ThemeToggle />
          {status === 'loading' ? (
            <span className="text-sm text-gray-400">Checking login...</span>
          ) : userProfile ? (
            <div className="flex items-center gap-2 md:gap-3">
              <Link to="/upload" className="navbar-action navbar-upload">
                <IoMdAdd className="text-xl" />
                <span className="hidden md:block">Upload</span>
              </Link>
              <Link to="/messages" className="icon-button relative" aria-label="Messages">
                <BsChatDots />
                {unreadCount > 0 && <span className="navbar-unread">{Math.min(unreadCount, 99)}</span>}
              </Link>
              <Link to="/notifications" className="icon-button relative" aria-label="Notifications">
                <IoNotificationsOutline />
                {notificationCount > 0 && (
                  <span className="navbar-unread">{Math.min(notificationCount, 99)}</span>
                )}
              </Link>
              <Link to={`/profile/${encodeURIComponent(userProfile._id)}`} aria-label="Your profile">
                <Avatar
                  width="40"
                  height="40"
                  className="h-10 w-10 cursor-pointer rounded-full border-2 border-white object-cover shadow-md dark:border-zinc-800"
                  src={userProfile.image}
                  alt="profile"
                />
              </Link>
              <button type="button" aria-label="Log out" className="icon-button text-red-500" onClick={handleLogout}>
                <AiOutlineLogout />
              </button>
            </div>
          ) : googleLoginEnabled ? (
            <GoogleLogin
              onSuccess={({ credential }) => credential && loginWithGoogle(credential)}
              onError={() => console.error('Google login popup failed')}
              useOneTap={false}
            />
          ) : (
            <span className="text-sm font-medium text-gray-500" title="Set GOOGLE_CLIENT_ID on the backend">
              Login not configured
            </span>
          )}
        </div>
      </div>
      {authError && <p className="navbar-error" role="alert">{authError}</p>}
    </header>
  )
}

export default Navbar
