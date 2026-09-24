import { BsCode, BsEmojiSunglasses } from 'react-icons/bs'
import { FaGamepad, FaMedal, FaPaw } from 'react-icons/fa'
import { GiCakeSlice, GiGalaxy, GiLipstick } from 'react-icons/gi'

export const topics = [
  { name: 'Anime', icon: <BsCode /> },
  { name: 'Hentai', icon: <BsEmojiSunglasses /> },
  { name: 'Gaming', icon: <FaGamepad /> },
  { name: 'Good', icon: <GiCakeSlice /> },
  { name: 'Dance', icon: <GiGalaxy /> },
  { name: 'Edits', icon: <GiLipstick /> },
  { name: 'Animals', icon: <FaPaw /> },
  { name: 'Sports', icon: <FaMedal /> },
]

export const formatBytes = (bytes) => {
  if (!Number.isFinite(bytes) || bytes <= 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  const exponent = Math.min(Math.floor(Math.log(bytes) / Math.log(1024)), units.length - 1)
  return `${(bytes / 1024 ** exponent).toFixed(exponent === 0 ? 0 : 1)} ${units[exponent]}`
}

