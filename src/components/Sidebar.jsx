import { useState } from 'react'
import { AiFillHome, AiOutlineMenu } from 'react-icons/ai'
import { BsBookmark, BsChatDots, BsGear, BsShieldCheck } from 'react-icons/bs'
import { ImCancelCircle } from 'react-icons/im'
import { IoNotificationsOutline } from 'react-icons/io5'
import { Link } from 'react-router-dom'

import useAuthStore from '../store/authStore'
import Discover from './Discover'
import Footer from './Footer'
import SuggestedAccounts from './SuggestedAccounts'

const Sidebar = () => {
  const [showSidebar, setShowSidebar] = useState(true)
  const { userProfile, unreadCount, notificationCount } = useAuthStore()

  return (
    <div>
      <button
        aria-label="Toggle sidebar"
        className="icon-button m-2 ml-4 mt-3 block xl:hidden"
        onClick={() => setShowSidebar((visible) => !visible)}
      >
        {showSidebar ? <ImCancelCircle /> : <AiOutlineMenu />}
      </button>
      {showSidebar && (
        <div className="sidebar-panel">
          <div className="sidebar-primary-links">
            <Link to="/" className="sidebar-link">
              <AiFillHome className="text-2xl" />
              <span className="hidden text-xl xl:block">For You</span>
            </Link>
            {userProfile && (
              <>
                <Link to="/messages" className="sidebar-link">
                  <span className="relative text-2xl">
                    <BsChatDots />
                    {unreadCount > 0 && <span className="sidebar-unread">{Math.min(unreadCount, 99)}</span>}
                  </span>
                  <span className="hidden text-xl xl:block">Messages</span>
                </Link>
                <Link to="/notifications" className="sidebar-link">
                  <span className="relative text-2xl">
                    <IoNotificationsOutline />
                    {notificationCount > 0 && (
                      <span className="sidebar-unread">{Math.min(notificationCount, 99)}</span>
                    )}
                  </span>
                  <span className="hidden text-xl xl:block">Notifications</span>
                </Link>
                <Link to="/library" className="sidebar-link">
                  <BsBookmark className="text-2xl" />
                  <span className="hidden text-xl xl:block">Library</span>
                </Link>
                <Link to="/settings" className="sidebar-link">
                  <BsGear className="text-2xl" />
                  <span className="hidden text-xl xl:block">Settings</span>
                </Link>
                {userProfile.isAdmin && (
                  <Link to="/admin" className="sidebar-link">
                    <BsShieldCheck className="text-2xl" />
                    <span className="hidden text-xl xl:block">Moderation</span>
                  </Link>
                )}
              </>
            )}
          </div>
          <Discover />
          <SuggestedAccounts />
          <Footer />
        </div>
      )}
    </div>
  )
}

export default Sidebar
