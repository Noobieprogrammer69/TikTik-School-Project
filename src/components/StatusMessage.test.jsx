import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'

import StatusMessage from './StatusMessage'

describe('StatusMessage', () => {
  it('renders a useful error and invokes retry', () => {
    const retry = vi.fn()
    render(<StatusMessage title="Could not load videos" message="Database is unavailable" onRetry={retry} />)
    expect(screen.getByRole('alert')).toHaveTextContent('Database is unavailable')
    fireEvent.click(screen.getByRole('button', { name: 'Try Again' }))
    expect(retry).toHaveBeenCalledOnce()
  })
})
