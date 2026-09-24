import { GoogleOAuthProvider } from '@react-oauth/google'
import { useEffect, useState } from 'react'
import { BrowserRouter, Route, Routes } from 'react-router-dom'

import { api } from './api/client'
import Layout from './components/Layout'
import Loading from './components/Loading'
import StatusMessage from './components/StatusMessage'
import Detail from './pages/Detail'
import Admin from './pages/Admin'
import Home from './pages/Home'
import Library from './pages/Library'
import Messages from './pages/Messages'
import Notifications from './pages/Notifications'
import NotFound from './pages/NotFound'
import Profile from './pages/Profile'
import Search from './pages/Search'
import Settings from './pages/Settings'
import Upload from './pages/Upload'

const RoutedApp = ({ config }) => (
  <BrowserRouter>
    <Routes>
      <Route element={<Layout googleLoginEnabled={Boolean(config.googleClientId)} />}>
        <Route path="/" element={<Home />} />
        <Route path="/upload" element={<Upload maxUploadBytes={config.maxUploadBytes} />} />
        <Route path="/detail/:id" element={<Detail />} />
        <Route path="/profile/:id" element={<Profile />} />
        <Route path="/messages" element={<Messages />} />
        <Route path="/messages/:userId" element={<Messages />} />
        <Route path="/notifications" element={<Notifications />} />
        <Route path="/library" element={<Library />} />
        <Route path="/settings" element={<Settings />} />
        <Route path="/admin" element={<Admin />} />
        <Route path="/search/:searchTerm" element={<Search />} />
        <Route path="*" element={<NotFound />} />
      </Route>
    </Routes>
  </BrowserRouter>
)

const App = () => {
  const [config, setConfig] = useState(null)
  const [error, setError] = useState('')
  const [attempt, setAttempt] = useState(0)

  useEffect(() => {
    let active = true
    api.publicConfig()
      .then((value) => active && setConfig(value))
      .catch((requestError) => active && setError(requestError.message))
    return () => { active = false }
  }, [attempt])

  if (error) {
    return (
      <div className="flex min-h-screen items-center justify-center p-6">
        <StatusMessage
          title="Application server is not ready"
          message={error}
          onRetry={() => { setError(''); setAttempt((value) => value + 1) }}
        />
      </div>
    )
  }
  if (!config) return <div className="min-h-screen"><Loading text="Starting TikTik..." /></div>

  const app = <RoutedApp config={config} />
  return config.googleClientId ? (
    <GoogleOAuthProvider clientId={config.googleClientId}>{app}</GoogleOAuthProvider>
  ) : app
}

export default App
