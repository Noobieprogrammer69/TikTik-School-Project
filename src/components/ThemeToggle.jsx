import { useEffect, useState } from 'react'
import { BsMoonStarsFill, BsSunFill } from 'react-icons/bs'

const currentTheme = () => (
  document.documentElement.classList.contains('dark') ? 'dark' : 'light'
)

const applyTheme = (theme) => {
  const dark = theme === 'dark'
  document.documentElement.classList.toggle('dark', dark)
  localStorage.setItem('tiktik-theme', theme)
  const color = document.querySelector('meta[name="theme-color"]')
  if (color) color.setAttribute('content', dark ? '#09090f' : '#ffffff')
}

const ThemeToggle = () => {
  const [theme, setTheme] = useState(currentTheme)

  useEffect(() => applyTheme(theme), [theme])

  const dark = theme === 'dark'
  return (
    <button
      type="button"
      aria-label={dark ? 'Switch to light mode' : 'Switch to dark mode'}
      title={dark ? 'Light mode' : 'Dark mode'}
      onClick={() => setTheme(dark ? 'light' : 'dark')}
      className="icon-button"
    >
      {dark ? <BsSunFill /> : <BsMoonStarsFill />}
    </button>
  )
}

export default ThemeToggle
