import { BiCommentX } from 'react-icons/bi'
import { MdOutlineVideocamOff } from 'react-icons/md'

const NoResults = ({ text }) => (
  <div className="flex h-full w-full flex-col items-center justify-center py-12" role="status">
    <p className="text-8xl">{text === 'No Comments Yet' ? <BiCommentX /> : <MdOutlineVideocamOff />}</p>
    <p className="text-center text-2xl">{text}</p>
  </div>
)

export default NoResults

