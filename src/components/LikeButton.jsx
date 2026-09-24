import { MdFavorite } from 'react-icons/md'

const LikeButton = ({ liked, count, onToggle, disabled }) => (
  <div className="flex gap-6">
    <button
      type="button"
      aria-label={liked ? 'Unlike video' : 'Like video'}
      aria-pressed={liked}
      disabled={disabled}
      onClick={onToggle}
      className="mt-4 flex cursor-pointer flex-col items-center justify-center disabled:cursor-wait disabled:opacity-60"
    >
      <span className={`rounded-full bg-primary p-2 md:p-4 ${liked ? 'text-[#F51997]' : ''}`}>
        <MdFavorite className="text-lg md:text-2xl" />
      </span>
      <span className="text-base font-semibold">{count || 0}</span>
    </button>
  </div>
)

export default LikeButton

