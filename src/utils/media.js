export const availableQualities = (asset) => Array.isArray(asset?.qualities) ? asset.qualities : []

export const preferredQuality = (asset) => {
  const values = availableQualities(asset)
  if (!values.length) return ''
  const connection = navigator.connection || navigator.mozConnection || navigator.webkitConnection
  const constrained = connection?.saveData || ['slow-2g', '2g', '3g'].includes(connection?.effectiveType)
  if (constrained || window.innerWidth < 720) return values.includes('360') ? '360' : values[0]
  return values.includes('720') ? '720' : values.at(-1)
}

export const mediaUrl = (asset, quality = 'auto') => {
  if (!asset?.url) return ''
  const selected = quality === 'auto' ? preferredQuality(asset) : quality
  if (!selected || !availableQualities(asset).includes(selected)) return asset.url
  const separator = asset.url.includes('?') ? '&' : '?'
  return `${asset.url}${separator}quality=${encodeURIComponent(selected)}`
}
