import { Link, useSearchParams } from 'react-router-dom'

import { topics } from '../utils/constants'

const Discover = () => {
  const [searchParams] = useSearchParams()
  const topic = searchParams.get('topic')
  const activeTopicStyle =
    'xl:border-2 hover:bg-primary xl:border-[#F51997] px-3 py-2 rounded xl:rounded-full flex items-center gap-2 justify-center cursor-pointer text-[#F51997]'
  const topicStyle =
    'xl:border-2 bg-white hover:bg-primary xl:border-gray-300 px-3 py-2 rounded xl:rounded-full flex items-center gap-2 justify-center cursor-pointer text-black'

  return (
    <div className="border-gray-200 pb-6 xl:border-b-2">
      <p className="m-3 mt-4 hidden font-semibold text-gray-600 xl:block">Popular Topics</p>
      <div className="flex flex-wrap gap-3">
        {topics.map((item) => (
          <Link to={`/?topic=${encodeURIComponent(item.name)}`} key={item.name}>
            <div className={topic === item.name ? activeTopicStyle : topicStyle}>
              <span className="text-2xl font-bold xl:text-base">{item.icon}</span>
              <span className="hidden text-base font-medium capitalize xl:block">{item.name}</span>
            </div>
          </Link>
        ))}
      </div>
    </div>
  )
}

export default Discover

