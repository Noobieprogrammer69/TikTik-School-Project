const FALLBACK_AVATAR = '/avatar.svg'

const Avatar = ({ src, alt, ...props }) => (
  <img
    {...props}
    src={src || FALLBACK_AVATAR}
    alt={alt}
    referrerPolicy="no-referrer"
    onError={(event) => {
      if (event.currentTarget.src.endsWith(FALLBACK_AVATAR)) return
      event.currentTarget.src = FALLBACK_AVATAR
    }}
  />
)

export default Avatar
