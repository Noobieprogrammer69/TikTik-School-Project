const StatusMessage = ({ title, message, onRetry }) => (
  <div className="m-auto max-w-lg rounded-xl border border-red-200 bg-red-50 p-6 text-center" role="alert">
    <p className="text-lg font-bold text-red-700">{title}</p>
    <p className="mt-2 text-sm text-red-600">{message}</p>
    {onRetry && (
      <button className="mt-4 rounded bg-[#F51997] px-4 py-2 font-semibold text-white" onClick={onRetry}>
        Try Again
      </button>
    )}
  </div>
)

export default StatusMessage

