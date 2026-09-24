const Loading = ({ text = 'Loading...' }) => (
  <div className="flex h-full w-full items-center justify-center py-12" role="status">
    <p className="animate-pulse text-lg font-semibold text-gray-500">{text}</p>
  </div>
)

export default Loading

