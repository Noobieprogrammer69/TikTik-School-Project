import { useEffect, useState } from 'react'

import { api } from '../api/client'
import Avatar from '../components/Avatar'
import Loading from '../components/Loading'
import StatusMessage from '../components/StatusMessage'
import useAuthStore from '../store/authStore'

const Admin = () => {
  const { userProfile, status, csrfToken } = useAuthStore()
  const [stats, setStats] = useState(null)
  const [reports, setReports] = useState(null)
  const [metrics, setMetrics] = useState(null)
  const [filter, setFilter] = useState('open')
  const [error, setError] = useState('')

  const load = async () => {
    try {
      const [nextStats, nextReports, nextMetrics] = await Promise.all([api.adminStats(), api.adminReports(filter), api.adminMetrics()])
      setStats(nextStats)
      setReports(nextReports)
      setMetrics(nextMetrics)
      setError('')
    } catch (requestError) {
      setError(requestError.message)
    }
  }

  useEffect(() => { if (userProfile?.isAdmin) load() }, [filter, userProfile]) // eslint-disable-line react-hooks/exhaustive-deps

  if (status === 'loading') return <Loading text="Checking administrator access..." />
  if (!userProfile?.isAdmin) return <StatusMessage title="Administrator access required" message="This page is restricted to configured moderators." />
  if (error) return <StatusMessage title="Could not load moderation" message={error} onRetry={load} />
  if (!stats || !reports) return <Loading text="Loading moderation dashboard..." />

  const updateReport = async (id, nextStatus) => {
    await api.updateReportStatus(id, nextStatus, csrfToken)
    await load()
  }

  const suspend = async (user, value) => {
    await api.setSuspended(user._id, value, csrfToken)
    await load()
  }

  return (
    <section className="admin-page">
      <header className="page-heading"><p className="eyebrow">Restricted</p><h1>Moderation dashboard</h1></header>
      <div className="stats-grid">
        {['users', 'videos', 'messages', 'follows', 'views', 'saves', 'openReports'].map((key) => (
          <article key={key}><strong>{stats[key]}</strong><span>{key.replace(/([A-Z])/g, ' $1')}</span></article>
        ))}
      </div>
      <div className="settings-card">
        <h2>Videos by topic</h2>
        <div className="topic-stats">{Object.entries(stats.topics).map(([topic, count]) => <span key={topic}>{topic}: <strong>{count}</strong></span>)}</div>
      </div>
      {metrics && <div className="settings-card"><h2>System health</h2><div className="topic-stats"><span>Database: <strong>{metrics.database}</strong></span><span>Media storage: <strong>{metrics.mediaRootAvailable ? 'ready' : 'unavailable'}</strong></span><span>Uptime: <strong>{Math.floor(metrics.uptimeSeconds / 60)} min</strong></span></div></div>}
      <div className="admin-report-heading">
        <h2>Account reports</h2>
        <select value={filter} onChange={(event) => setFilter(event.target.value)}>
          <option value="open">Open</option><option value="reviewed">Reviewed</option>
          <option value="resolved">Resolved</option><option value="dismissed">Dismissed</option>
          <option value="all">All</option>
        </select>
      </div>
      <div className="report-list">
        {reports.map((report) => (
          <article className="moderation-report" key={report._id}>
            <div className="moderation-users">
              <Avatar src={report.target?.image} className="comment-avatar" alt="" />
              <div><strong>{report.target?.userName || 'Missing account'}</strong><small>Reported by {report.reporter?.userName || 'Unknown'}</small></div>
            </div>
            <p><strong>{report.reason}</strong> — {report.details || 'No additional details'}</p>
            <div className="moderation-actions">
              {['reviewed', 'resolved', 'dismissed'].map((value) => <button key={value} onClick={() => updateReport(report._id, value)}>{value}</button>)}
              {report.target && <button className="danger-button" onClick={() => suspend(report.target, !report.target.suspended)}>{report.target.suspended ? 'Restore account' : 'Suspend account'}</button>}
            </div>
          </article>
        ))}
        {!reports.length && <p className="notification-empty">No reports in this category.</p>}
      </div>
    </section>
  )
}

export default Admin
