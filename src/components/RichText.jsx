import { Link } from 'react-router-dom'

const RichText = ({ text = '' }) => text.split(/([#@][A-Za-z0-9_.]+)/g).map((part, index) => {
  if (part.startsWith('#')) return <Link key={`${part}-${index}`} className="hashtag-link" to={`/?hashtag=${encodeURIComponent(part.slice(1).toLowerCase())}`}>{part}</Link>
  if (part.startsWith('@')) return <Link key={`${part}-${index}`} className="mention-link" to={`/search/${encodeURIComponent(part.slice(1))}`}>{part}</Link>
  return part
})

export default RichText
