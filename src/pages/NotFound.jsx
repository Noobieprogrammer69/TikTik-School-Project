import { Link } from 'react-router-dom'

const NotFound = () => (
  <div className="m-auto text-center">
    <p className="text-5xl font-bold">404</p>
    <p className="mt-2 text-gray-500">That page does not exist.</p>
    <Link className="mt-4 inline-block rounded bg-[#F51997] px-4 py-2 text-white" to="/">Return Home</Link>
  </div>
)

export default NotFound

